/*
 * uninit.c
 * Phase 3 — Variable Used Before Initialization Detection
 *
 * Detects local variables that are READ before they are written,
 * i.e., "use before initialization" bugs.
 *
 * ─────────────────────────────────────────────────────────────────
 * THEORY: How SSA makes this straightforward
 * ─────────────────────────────────────────────────────────────────
 *
 * After the SSA pass, every variable USE in the IR is represented as
 * an SSA_NAME (e.g., x_3).  Each SSA_NAME has exactly ONE definition
 * (that's what "single assignment" means).
 *
 * GCC distinguishes two kinds of definitions:
 *
 *   a) A real definition:  x_3 = malloc(64);  — somewhere in the code.
 *
 *   b) A "default definition":  GCC implicitly creates an SSA_NAME
 *      for a variable that is USED before any write.  This node is
 *      marked SSA_NAME_IS_DEFAULT_DEF(name) == true.
 *      It represents "the value of x at function entry", which for a
 *      local variable is garbage (uninitialized).
 *
 * Algorithm:
 *   Walk every operand of every statement.
 *   If the operand is an SSA_NAME and SSA_NAME_IS_DEFAULT_DEF is true,
 *   and the underlying VAR_DECL is a real local variable (not a
 *   compiler temp / parameter / global), then the variable was used
 *   before being initialized → emit a warning.
 *
 * We also track variables that were "defined" (LHS) at least once
 * so we can suppress false positives from GCC's own internal temps
 * that happen to be marked default-def.
 *
 * ─────────────────────────────────────────────────────────────────
 * What we DO NOT catch (intentional scope limitation):
 *   - Partial initialization through struct fields
 *   - Path-sensitive cases (initialized on one branch, not another)
 *     → those require full dataflow; we handle only the trivial SSA
 *       default-def case which is zero-false-positive.
 * ─────────────────────────────────────────────────────────────────
 */

#include "gcc-plugin.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "basic-block.h"
#include "hash-set.h"
#include "ssa.h"
#include "uninit.h"
#include <stdio.h>
#include <stdbool.h>

/* ── Internal helpers ─────────────────────────────────────── */

/*
 * is_real_local_var - true only for user-written, non-parameter,
 * non-artificial local variables.
 *
 * Parameters have a value at function entry (the caller passed it),
 * so their default-defs are NOT uninitialized reads.
 * Compiler-generated temps (DECL_ARTIFICIAL) are internal GCC bookkeeping.
 */
static bool is_real_local_var(tree decl)
{
    if (!decl)                          return false;
    if (TREE_CODE(decl) != VAR_DECL)    return false;
    if (DECL_ARTIFICIAL(decl))          return false;  /* compiler temp */
    if (DECL_EXTERNAL(decl))            return false;  /* extern */
    if (TREE_STATIC(decl))              return false;  /* static/global */
    /* PARM_DECLs are VAR_DECLs with DECL_CONTEXT = function;
       distinguish them via TREE_CODE check below */
    return true;
}

/*
 * var_name - return the printable identifier for a decl.
 */
static const char *var_name(tree decl)
{
    if (decl && DECL_NAME(decl))
        return IDENTIFIER_POINTER(DECL_NAME(decl));
    return "<anonymous>";
}

/*
 * print_location - emit "file:line:col: " to stderr.
 * Direct fprintf to avoid GCC diagnostic filtering.
 */
static void print_location(location_t loc)
{
    if (loc == UNKNOWN_LOCATION) {
        fprintf(stderr, "<unknown>: ");
        return;
    }
    expanded_location x = expand_location(loc);
    fprintf(stderr, "%s:%d:%d: ",
            x.file ? x.file : "<unknown>",
            x.line, x.column);
}

/* ── Core detection ───────────────────────────────────────── */

/*
 * check_ssa_name_for_uninit - given a single tree operand @op from
 * a statement at @stmt_loc, check if it is an uninitialized-use SSA name.
 *
 * The set @reported prevents duplicate warnings for the same decl.
 * The set @defined contains decls that have a real LHS definition
 * somewhere in the function — we skip those even if they somehow
 * also have a default-def (can happen with realloc patterns).
 */
static void check_ssa_name_for_uninit(tree op,
                                       location_t stmt_loc,
                                       hash_set<tree> *reported,
                                       hash_set<tree> *defined)
{
    if (!op || TREE_CODE(op) != SSA_NAME) return;

    /* Only care about default definitions */
    if (!SSA_NAME_IS_DEFAULT_DEF(op)) return;

    /* Get the underlying VAR_DECL */
    tree decl = SSA_NAME_VAR(op);
    if (!decl) return;

    /* Must be a real user local variable (not a parameter) */
    if (!is_real_local_var(decl)) return;

    /* Skip if we've already warned about this decl */
    if (reported->contains(decl)) return;

    /* Skip if the variable has a real assignment somewhere in the fn
     * (the default-def is reachable but a real def also exists — this
     *  is the "maybe uninitialized" case we leave to compilers). */
    if (defined->contains(decl)) return;

    /* ── Emit the warning ─────────────────────────────────── */
    reported->add(decl);

    location_t decl_loc = DECL_SOURCE_LOCATION(decl);
    const char *name    = var_name(decl);

    /* Point at the USE site */
    print_location(stmt_loc);
    fprintf(stderr,
            "error: variable '%s' is used before initialization\n",
            name);

    /* Point at the DECLARATION site */
    print_location(decl_loc);
    fprintf(stderr,
            "note: '%s' was declared here but never assigned before use\n",
            name);

    /* Suggest a fix */
    print_location(decl_loc);
    fprintf(stderr,
            "note: suggestion: initialize '%s' at declaration, "
            "e.g., '%s = 0;' or '%s = NULL;'\n",
            name, name, name);
}

/*
 * collect_defined_vars - first pass: collect all VAR_DECLs that appear
 * on the LHS of any statement (i.e., they have at least one real write).
 *
 * We use this to avoid flagging "maybe uninitialized" cases which
 * require path-sensitive analysis to resolve accurately.
 */
static void collect_defined_vars(function *fun, hash_set<tree> *defined)
{
    basic_block bb;
    FOR_EACH_BB_FN(bb, fun) {
        for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
             !gsi_end_p(gsi); gsi_next(&gsi)) {
            gimple *stmt = gsi_stmt(gsi);
            tree lhs = gimple_get_lhs(stmt);
            if (!lhs) continue;

            /* Unwrap SSA_NAME to underlying VAR_DECL */
            if (TREE_CODE(lhs) == SSA_NAME) {
                tree var = SSA_NAME_VAR(lhs);
                if (var && is_real_local_var(var))
                    defined->add(var);
            } else if (TREE_CODE(lhs) == VAR_DECL && is_real_local_var(lhs)) {
                defined->add(lhs);
            }
        }
    }
}

/* ── Public API ───────────────────────────────────────────── */

/*
 * check_uninit_variables - entry point called from safecheck.c.
 *
 * Two-pass algorithm:
 *   Pass 1: collect all VAR_DECLs that have at least one real write
 *           (stored in `defined`).
 *   Pass 2: walk every statement's operands; for each SSA_NAME operand
 *           that is a "default def" of a real local variable not in
 *           `defined`, emit "used before initialization".
 */
void check_uninit_variables(function *fun)
{
    hash_set<tree> *defined  = new hash_set<tree>();
    hash_set<tree> *reported = new hash_set<tree>();

    /* Pass 1: collect all defined (written) local vars */
    collect_defined_vars(fun, defined);

    /* Pass 2: scan all operands for default-def SSA names */
    basic_block bb;
    FOR_EACH_BB_FN(bb, fun) {
        for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
             !gsi_end_p(gsi); gsi_next(&gsi)) {
            gimple    *stmt = gsi_stmt(gsi);
            location_t loc  = gimple_location(stmt);

            /* Scan all generic operands (op 0 = LHS, skip it) */
            unsigned n = gimple_num_ops(stmt);
            for (unsigned i = 1; i < n; i++) {
                tree op = gimple_op(stmt, i);
                check_ssa_name_for_uninit(op, loc, reported, defined);
            }

            /* Also scan call arguments — not always in gimple_op() */
            if (is_gimple_call(stmt)) {
                unsigned nargs = gimple_call_num_args(stmt);
                for (unsigned i = 0; i < nargs; i++) {
                    tree arg = gimple_call_arg(stmt, i);
                    check_ssa_name_for_uninit(arg, loc, reported, defined);
                }
            }

            /* PHI node inputs — GCC SSA uses PHI nodes to merge
             * values at join points (after if/else, loop back-edges).
             * A PHI input that is a default-def is also uninitialized. */
            if (gimple_code(stmt) == GIMPLE_PHI) {
                unsigned n_args = gimple_phi_num_args(stmt);
                for (unsigned i = 0; i < n_args; i++) {
                    tree arg = gimple_phi_arg_def(stmt, i);
                    check_ssa_name_for_uninit(arg, loc, reported, defined);
                }
            }
        }
    }

    delete defined;
    delete reported;
}
