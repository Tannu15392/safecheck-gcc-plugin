#ifndef UNINIT_H
#define UNINIT_H

/*
 * uninit.h
 * Phase 3 — Variable Used Before Initialization Detection
 *
 * Declares the uninitialized-variable analysis pass.
 * Called from safecheck.c inside the GIMPLE pass execute() method.
 *
 * Algorithm overview:
 *   We perform a forward dataflow analysis over the SSA (Static Single
 *   Assignment) form that GCC has already built for us.
 *
 *   In SSA form, every variable definition is unique (x_1, x_2, …).
 *   A variable "use before init" occurs when an SSA name is USED whose
 *   only definition comes from an uninitialized source, i.e.:
 *     - It has no definition at all (gimple_default_def), OR
 *     - Its reaching definition is a PHI node whose ALL predecessors
 *       are themselves uninitialized.
 *
 *   We look for the simpler, high-value cases:
 *     1. Default definitions: SSA names that GCC creates as "default defs"
 *        for variables that are used without being written first.
 *        These are the clearest, most reliable signal.
 *     2. Uses where the underlying VAR_DECL was never assigned inside
 *        the function body before this USE point.
 *
 * Why SSA makes this easier:
 *   In SSA form, if `x` is never written before a USE, GCC creates a
 *   "default definition" node (recognizable via SSA_NAME_IS_DEFAULT_DEF).
 *   This is precisely the "use before any definition" case.
 */

#include "gcc-plugin.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "basic-block.h"

/*
 * check_uninit_variables - scan every basic block in @fun and
 * emit a warning for each local variable that is used before
 * it has been assigned a value.
 *
 * Relies on SSA default-definition detection, which is sound
 * (no false positives for the default-def case).
 */
void check_uninit_variables(function *fun);

#endif /* UNINIT_H */
