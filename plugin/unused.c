/*
 * unused.c
 * Phase 1 — Unused Variable Detection
 *
 * Implements def-use analysis over GIMPLE to find local variables
 * that are assigned but never read inside a function.
 *
 * How it works:
 *   1. Walk every statement in every basic block.
 *   2. Collect all variables that appear on the RHS (used set).
 *   3. Collect all variables that appear on the LHS (defined set).
 *   4. Any variable in defined but NOT in used → report warning.
 *
 * False-positive guards:
 *   - DECL_ARTIFICIAL  : compiler-generated temporaries are skipped
 *   - DECL_EXTERNAL    : extern declarations are skipped
 *   - TREE_STATIC      : static/global variables are skipped
 *   - Array arguments  : &arr[0] address-of patterns are unwrapped
 */

#include "gcc-plugin.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "basic-block.h"
#include "hash-set.h"
#include "unused.h"
#include <stdio.h>
#include <stdbool.h>

/* ── Internal helpers ─────────────────────────────────────── */

/*
 * strip_ssa - unwrap an SSA_NAME to its underlying VAR_DECL.
 * GIMPLE represents variables as SSA names (e.g. x_3) after the
 * SSA pass; we need the original decl to compare variables.
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
 * is_real_var - return true only for user-written local variables.
 * Filters out compiler-generated, extern, and static declarations.
 */
static bool is_real_var(tree decl)
{
    if (!decl || TREE_CODE(decl) != VAR_DECL) return false;
    if (DECL_ARTIFICIAL(decl)) return false;   /* compiler temp  */
    if (DECL_EXTERNAL(decl))   return false;   /* extern decl    */
    if (TREE_STATIC(decl))     return false;   /* static/global  */
    return true;
}

/*
 * var_name - return the identifier string for a decl, or
 * "<anonymous>" for unnamed compiler-generated nodes.
 */
static const char *var_name(tree decl)
{
    if (DECL_NAME(decl)) return IDENTIFIER_POINTER(DECL_NAME(decl));
    return "<anonymous>";
}

/*
 * print_location - write "file:line:col: " to stderr.
 *
 * We use fprintf(stderr) directly rather than warning_at() because:
 *   - warning_at() with OPT_Wunused_variable requires -Wall to fire
 *   - warning_at() with option 0 is silently dropped on GCC 12
 *   - inform() is suppressed when there is no parent diagnostic
 * fprintf(stderr) is never filtered and is captured by 2>&1.
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

/* ── Use-collection ───────────────────────────────────────── */

/*
 * collect_uses - add every variable READ by @stmt into @used.
 *
 * Handles two special cases:
 *   1. Call arguments: gimple_call_arg() are not in gimple_op()
 *      on all GCC versions, so we scan them separately.
 *   2. Address-of array: &arr[0] — the ADDR_EXPR wraps an
 *      ARRAY_REF; we unwrap it to reach the underlying array decl.
 */
static void collect_uses(gimple *stmt, hash_set<tree> *used)
{
    /* Scan generic operands (skipping op 0 = LHS) */
    unsigned n = gimple_num_ops(stmt);
    for (unsigned i = 1; i < n; i++) {
        tree op = gimple_op(stmt, i);
        if (!op) continue;

        /* Unwrap &arr[0] → arr */
        if (TREE_CODE(op) == ADDR_EXPR) {
            tree inner = TREE_OPERAND(op, 0);
            if (inner && TREE_CODE(inner) == ARRAY_REF)
                inner = TREE_OPERAND(inner, 0);
            inner = strip_ssa(inner);
            if (inner && TREE_CODE(inner) == VAR_DECL)
                used->add(inner);
        }

        op = strip_ssa(op);
        if (op && TREE_CODE(op) == VAR_DECL)
            used->add(op);
    }

    /* Also scan call arguments explicitly */
    if (is_gimple_call(stmt)) {
        unsigned nargs = gimple_call_num_args(stmt);
        for (unsigned i = 0; i < nargs; i++) {
            tree arg = gimple_call_arg(stmt, i);
            if (arg && TREE_CODE(arg) == ADDR_EXPR) {
                tree inner = TREE_OPERAND(arg, 0);
                if (inner && TREE_CODE(inner) == ARRAY_REF)
                    inner = TREE_OPERAND(inner, 0);
                inner = strip_ssa(inner);
                if (inner && TREE_CODE(inner) == VAR_DECL)
                    used->add(inner);
            }
            arg = strip_ssa(arg);
            if (arg && TREE_CODE(arg) == VAR_DECL)
                used->add(arg);
        }
    }
}

/* ── Public API ───────────────────────────────────────────── */

/*
 * check_unused_variables - entry point called from safecheck.c.
 *
 * Algorithm:
 *   Pass 1: walk all stmts, populate `defined` (LHS) and `used` (RHS).
 *   Pass 2: for each decl in `defined` not in `used`, emit warning.
 */
void check_unused_variables(function *fun)
{
    hash_set<tree> *defined = new hash_set<tree>();
    hash_set<tree> *used    = new hash_set<tree>();

    basic_block bb;
    FOR_EACH_BB_FN(bb, fun) {
        for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
             !gsi_end_p(gsi); gsi_next(&gsi)) {
            gimple *stmt = gsi_stmt(gsi);

            /* Collect RHS / argument uses */
            collect_uses(stmt, used);

            /* Collect LHS definitions */
            tree lhs = strip_ssa(gimple_get_lhs(stmt));
            if (lhs && is_real_var(lhs))
                defined->add(lhs);
        }
    }

    /* Report variables defined but never used */
    for (tree decl : *defined) {
        if (used->contains(decl)) continue;

        location_t  loc  = DECL_SOURCE_LOCATION(decl);
        const char *name = var_name(decl);

        print_location(loc);
        fprintf(stderr, "warning: unused variable '%s'\n", name);

        print_location(loc);
        fprintf(stderr,
                "note: suggestion: rename to '_%s' to suppress,"
                " or remove the assignment\n", name);
    }

    delete defined;
    delete used;
}