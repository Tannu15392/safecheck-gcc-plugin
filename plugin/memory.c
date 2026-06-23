/*
 * memory.c
 * Phase 2  — Memory Management Analysis
 * Phase 2b — Null Pointer Dereference Detection
 *
 * ═══════════════════════════════════════════════════════════════
 * PHASE 2: Memory Leak & Double-Free (original)
 * ═══════════════════════════════════════════════════════════════
 *
 * Implements a pointer state-machine over GIMPLE to detect:
 *   1. Memory leaks   — malloc/calloc/realloc/strdup with no free()
 *   2. Double frees   — free() called twice on the same pointer
 *
 * State machine per pointer variable:
 *
 *   PTR_UNKNOWN    initial / after "ptr = NULL"
 *   PTR_ALLOCATED  after malloc / calloc / realloc / strdup
 *   PTR_FREED      after free()
 *
 * Transitions:
 *   alloc call  → PTR_ALLOCATED
 *   free(ptr)   → if already PTR_FREED: report double-free
 *               → set PTR_FREED
 *   ptr = NULL  → reset to PTR_UNKNOWN  (safe idiom)
 *   end of fn   → if still PTR_ALLOCATED: report memory leak
 *
 * ═══════════════════════════════════════════════════════════════
 * PHASE 2b: Null Pointer Dereference Detection (new)
 * ═══════════════════════════════════════════════════════════════
 *
 * Detects two patterns:
 *
 *   Pattern A — Unchecked malloc:
 *     ptr = malloc(...);
 *     *ptr = value;          ← deref without if(!ptr) check
 *
 *   Pattern B — Deref after NULL-check implies null path:
 *     ptr = malloc(...);
 *     if (!ptr) { ... }      ← compiler sees ptr CAN be null
 *     ptr->field = x;        ← but this path may still execute
 *                               (e.g., early return missing)
 *
 * Implementation approach:
 *   We track whether a pointer from malloc was checked for NULL.
 *   If the pointer is dereferenced (via INDIRECT_REF, COMPONENT_REF
 *   through a pointer, or ARRAY_REF through a pointer) before a
 *   NULL check, we report it.
 *
 *   State per pointer:
 *     NULL_UNKNOWN   — not from malloc or already checked
 *     NULL_UNCHECKED — from malloc, not yet null-checked
 *     NULL_CHECKED   — user wrote if(!ptr) or if(ptr==NULL)
 *
 * Limitation: this is intra-procedural and flow-insensitive for
 * Pattern B.  We report only Pattern A (unchecked malloc use)
 * which is zero-false-positive.
 */

#include "gcc-plugin.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "basic-block.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* ══════════════════════════════════════════════════════════════
 * Shared utilities
 * ══════════════════════════════════════════════════════════════ */

/*
 * strip_ssa - unwrap SSA_NAME to its underlying VAR_DECL.
 */
static tree strip_ssa(tree t)
{
    if (!t) return NULL_TREE;
    if (TREE_CODE(t) == SSA_NAME) {
        tree var = SSA_NAME_VAR(t);
        return var ? var : t;
    }
    return t;
}

/*
 * is_real_var - true only for user-written local variables.
 */
static bool is_real_var(tree decl)
{
    if (!decl || TREE_CODE(decl) != VAR_DECL) return false;
    if (DECL_ARTIFICIAL(decl)) return false;
    if (DECL_EXTERNAL(decl))   return false;
    if (TREE_STATIC(decl))     return false;
    return true;
}

/*
 * var_name - return the identifier string for a decl.
 */
static const char *var_name(tree decl)
{
    if (DECL_NAME(decl)) return IDENTIFIER_POINTER(DECL_NAME(decl));
    return "<anonymous>";
}

/*
 * get_fn_name - return function name in a GIMPLE_CALL, or "".
 */
static const char *get_fn_name(gimple *stmt)
{
    tree fn = gimple_call_fndecl(stmt);
    if (!fn || !DECL_NAME(fn)) return "";
    return IDENTIFIER_POINTER(DECL_NAME(fn));
}

/*
 * print_location - write "file:line:col: " to stderr.
 */
static void print_location(location_t loc)
{
    if (loc == UNKNOWN_LOCATION) {
        fprintf(stderr, "<unknown>: ");
        return;
    }
    expanded_location xloc = expand_location(loc);
    fprintf(stderr, "%s:%d:%d: ",
            xloc.file ? xloc.file : "<unknown>",
            xloc.line,
            xloc.column);
}

/*
 * is_alloc_call - true if stmt calls malloc/calloc/realloc/strdup.
 */
static bool is_alloc_call(gimple *stmt)
{
    if (!is_gimple_call(stmt)) return false;
    const char *n = get_fn_name(stmt);
    return (!strcmp(n, "malloc")  ||
            !strcmp(n, "calloc")  ||
            !strcmp(n, "realloc") ||
            !strcmp(n, "strdup"));
}

/*
 * is_free_call - true if stmt calls free().
 */
static bool is_free_call(gimple *stmt)
{
    if (!is_gimple_call(stmt)) return false;
    return !strcmp(get_fn_name(stmt), "free");
}


/* ══════════════════════════════════════════════════════════════
 * PHASE 2 — Memory Leak & Double-Free
 * ══════════════════════════════════════════════════════════════ */

typedef enum {
    PTR_UNKNOWN   = 0,
    PTR_ALLOCATED = 1,
    PTR_FREED     = 2
} ptr_state_t;

typedef struct ptr_info {
    tree            decl;
    ptr_state_t     state;
    location_t      alloc_loc;
    location_t      free_loc;
    struct ptr_info *next;
} ptr_info_t;

static ptr_info_t *g_ptrs = NULL;

static void ptr_table_reset(void)
{
    ptr_info_t *p = g_ptrs;
    while (p) {
        ptr_info_t *nx = p->next;
        free(p);
        p = nx;
    }
    g_ptrs = NULL;
}

static ptr_info_t *ptr_find(tree decl)
{
    for (ptr_info_t *p = g_ptrs; p; p = p->next)
        if (p->decl == decl) return p;
    return NULL;
}

static ptr_info_t *ptr_get(tree decl)
{
    ptr_info_t *p = ptr_find(decl);
    if (!p) {
        p = (ptr_info_t *)xmalloc(sizeof(*p));
        p->decl      = decl;
        p->state     = PTR_UNKNOWN;
        p->alloc_loc = p->free_loc = UNKNOWN_LOCATION;
        p->next      = g_ptrs;
        g_ptrs       = p;
    }
    return p;
}

void check_memory_management(function *fun)
{
    ptr_table_reset();

    basic_block bb;
    FOR_EACH_BB_FN(bb, fun) {
        for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
             !gsi_end_p(gsi); gsi_next(&gsi)) {
            gimple    *stmt = gsi_stmt(gsi);
            location_t loc  = gimple_location(stmt);

            if (is_alloc_call(stmt)) {
                tree lhs = strip_ssa(gimple_get_lhs(stmt));
                if (lhs && is_real_var(lhs)) {
                    ptr_info_t *info = ptr_get(lhs);
                    info->state     = PTR_ALLOCATED;
                    info->alloc_loc = loc;
                }
                continue;
            }

            if (is_free_call(stmt)) {
                if (gimple_call_num_args(stmt) < 1) continue;
                tree arg = strip_ssa(gimple_call_arg(stmt, 0));
                if (!arg || !is_real_var(arg)) continue;

                ptr_info_t *info = ptr_find(arg);
                if (info && info->state == PTR_FREED) {
                    print_location(loc);
                    fprintf(stderr,
                            "error: double free of '%s'\n",
                            var_name(arg));
                    print_location(info->free_loc);
                    fprintf(stderr,
                            "note: '%s' was first freed here\n",
                            var_name(arg));
                    print_location(loc);
                    fprintf(stderr,
                            "note: suggestion: set '%s = NULL' "
                            "after the first free()\n",
                            var_name(arg));
                }

                ptr_info_t *info2 = ptr_get(arg);
                info2->state    = PTR_FREED;
                info2->free_loc = loc;
                continue;
            }

            if (gimple_code(stmt) == GIMPLE_ASSIGN) {
                tree lhs = strip_ssa(gimple_get_lhs(stmt));
                tree rhs = strip_ssa(gimple_assign_rhs1(stmt));
                if (lhs && is_real_var(lhs) &&
                    POINTER_TYPE_P(TREE_TYPE(lhs)) &&
                    rhs && integer_zerop(rhs))
                    ptr_get(lhs)->state = PTR_UNKNOWN;
            }
        }
    }

    for (ptr_info_t *p = g_ptrs; p; p = p->next) {
        if (p->state != PTR_ALLOCATED) continue;
        if (DECL_ARTIFICIAL(p->decl))  continue;

        const char *name = var_name(p->decl);

        print_location(p->alloc_loc);
        fprintf(stderr,
                "error: memory leak: '%s' is allocated but never freed\n",
                name);
        print_location(p->alloc_loc);
        fprintf(stderr, "note: '%s' was allocated here\n", name);
        print_location(p->alloc_loc);
        fprintf(stderr,
                "note: suggestion: add free(%s) before all "
                "return statements\n", name);
    }

    ptr_table_reset();
}


/* ══════════════════════════════════════════════════════════════
 * PHASE 2b — Null Pointer Dereference Detection
 * ══════════════════════════════════════════════════════════════
 *
 * State machine for each heap pointer:
 *
 *   NP_UNKNOWN      — not from an allocation function
 *   NP_UNCHECKED    — returned from malloc/calloc/etc, NOT yet
 *                     checked for NULL; dereference here = bug
 *   NP_CHECKED      — user wrote if(!ptr) or if(ptr==NULL),
 *                     so we trust the happy path is guarded
 *
 * Transitions:
 *   ptr = malloc(…)          → NP_UNCHECKED
 *   if (!ptr) / if(ptr==NULL)→ NP_CHECKED  (we see the GIMPLE_COND)
 *   ptr = NULL               → NP_UNKNOWN  (user reset it)
 *   *ptr  / ptr->field       → if NP_UNCHECKED → report
 *
 * We walk basic blocks in program order.  A GIMPLE_COND that
 * compares a pointer to zero transitions it to NP_CHECKED for the
 * remainder of the function (conservative — avoids false positives
 * on the false-branch where ptr is definitely non-null).
 * ══════════════════════════════════════════════════════════════ */

typedef enum {
    NP_UNKNOWN    = 0,
    NP_UNCHECKED  = 1,   /* from malloc, not yet null-tested */
    NP_CHECKED    = 2    /* user wrote if(!ptr)              */
} np_state_t;

typedef struct np_info {
    tree            decl;
    np_state_t      state;
    location_t      alloc_loc;   /* where malloc was called      */
    struct np_info *next;
} np_info_t;

static np_info_t *g_np = NULL;

static void np_table_reset(void)
{
    np_info_t *p = g_np;
    while (p) {
        np_info_t *nx = p->next;
        free(p);
        p = nx;
    }
    g_np = NULL;
}

static np_info_t *np_find(tree decl)
{
    for (np_info_t *p = g_np; p; p = p->next)
        if (p->decl == decl) return p;
    return NULL;
}

static np_info_t *np_get(tree decl)
{
    np_info_t *p = np_find(decl);
    if (!p) {
        p = (np_info_t *)xmalloc(sizeof(*p));
        p->decl      = decl;
        p->state     = NP_UNKNOWN;
        p->alloc_loc = UNKNOWN_LOCATION;
        p->next      = g_np;
        g_np         = p;
    }
    return p;
}

/*
 * is_deref_of - check whether @expr contains a dereference of
 * the pointer variable @target_decl.
 *
 * Patterns in GIMPLE TREE nodes:
 *   INDIRECT_REF(ptr)           — *ptr
 *   COMPONENT_REF(INDIRECT_REF(ptr), field)  — ptr->field
 *   ARRAY_REF(INDIRECT_REF(ptr), idx)        — ptr[i]
 *
 * We recurse at most two levels deep (enough for all real patterns).
 */
static bool is_deref_of(tree expr, tree target_decl)
{
    if (!expr || !target_decl) return false;

    switch (TREE_CODE(expr)) {
    case INDIRECT_REF: {
        /* *ptr — the operand should resolve to target_decl */
        tree inner = strip_ssa(TREE_OPERAND(expr, 0));
        return (inner == target_decl);
    }
    case COMPONENT_REF:
    case ARRAY_REF: {
        /* ptr->field or ptr[i] — recurse on the base */
        return is_deref_of(TREE_OPERAND(expr, 0), target_decl);
    }
    default:
        return false;
    }
}

/*
 * scan_operands_for_deref - walk all operands of @stmt and check
 * whether any dereferences a pointer currently in NP_UNCHECKED state.
 *
 * If found, emit "null pointer dereference" error and mark the
 * pointer NP_CHECKED so we don't double-report.
 */
static void scan_operands_for_deref(gimple *stmt, location_t loc)
{
    unsigned n = gimple_num_ops(stmt);
    for (unsigned i = 0; i < n; i++) {
        tree op = gimple_op(stmt, i);
        if (!op) continue;

        /* Walk our null-pointer table */
        for (np_info_t *p = g_np; p; p = p->next) {
            if (p->state != NP_UNCHECKED) continue;

            if (is_deref_of(op, p->decl)) {
                const char *name = var_name(p->decl);

                print_location(loc);
                fprintf(stderr,
                        "error: null pointer dereference: "
                        "'%s' may be NULL (return value of allocation "
                        "not checked)\n",
                        name);

                print_location(p->alloc_loc);
                fprintf(stderr,
                        "note: '%s' was allocated here; "
                        "allocation functions can return NULL\n",
                        name);

                print_location(loc);
                fprintf(stderr,
                        "note: suggestion: add 'if (!%s) return;' "
                        "after the allocation\n",
                        name);

                /* Don't report again for this pointer */
                p->state = NP_CHECKED;
            }
        }
    }
}

void check_null_deref(function *fun)
{
    np_table_reset();

    basic_block bb;
    FOR_EACH_BB_FN(bb, fun) {
        for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
             !gsi_end_p(gsi); gsi_next(&gsi)) {
            gimple    *stmt = gsi_stmt(gsi);
            location_t loc  = gimple_location(stmt);

            /* ── malloc/calloc/… → NP_UNCHECKED ──────────── */
            if (is_alloc_call(stmt)) {
                tree lhs = strip_ssa(gimple_get_lhs(stmt));
                if (lhs && is_real_var(lhs)) {
                    np_info_t *info = np_get(lhs);
                    info->state     = NP_UNCHECKED;
                    info->alloc_loc = loc;
                }
                continue;
            }

            /* ── if (!ptr) / if (ptr == NULL) → NP_CHECKED ─ */
            if (gimple_code(stmt) == GIMPLE_COND) {
                /* GIMPLE_COND: condition is (op0 CMP op1)        *
                 * We look for (ptr == 0) or (ptr != 0) which is  *
                 * what GCC emits for if(!ptr) and if(ptr==NULL). */
                tree cond_lhs = gimple_cond_lhs(stmt);
                tree cond_rhs = gimple_cond_rhs(stmt);
                tree ptr_side = NULL_TREE;

                if (cond_rhs && integer_zerop(cond_rhs))
                    ptr_side = strip_ssa(cond_lhs);
                else if (cond_lhs && integer_zerop(cond_lhs))
                    ptr_side = strip_ssa(cond_rhs);

                if (ptr_side && is_real_var(ptr_side)) {
                    np_info_t *info = np_find(ptr_side);
                    if (info) info->state = NP_CHECKED;
                }
                continue;
            }

            /* ── ptr = NULL → NP_UNKNOWN (safe reset) ─────── */
            if (gimple_code(stmt) == GIMPLE_ASSIGN) {
                tree lhs = strip_ssa(gimple_get_lhs(stmt));
                tree rhs = strip_ssa(gimple_assign_rhs1(stmt));
                if (lhs && is_real_var(lhs) &&
                    POINTER_TYPE_P(TREE_TYPE(lhs)) &&
                    rhs && integer_zerop(rhs)) {
                    np_info_t *info = np_find(lhs);
                    if (info) info->state = NP_UNKNOWN;
                }
            }

            /* ── Scan all operands for dereferences ────────── */
            scan_operands_for_deref(stmt, loc);
        }
    }

    np_table_reset();
}
