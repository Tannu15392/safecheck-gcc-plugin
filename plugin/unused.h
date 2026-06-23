#ifndef UNUSED_H
#define UNUSED_H

/*
 * unused.h
 * Phase 1 — Unused Variable Detection
 *
 * Declares the unused-variable analysis pass.
 * Called from safecheck.c inside the GIMPLE pass execute() method.
 */

#include "gcc-plugin.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "basic-block.h"

/*
 * check_unused_variables - scan every basic block in @fun and
 * emit a warning for each local variable that is defined but
 * never read.
 *
 * Uses a simple def-use analysis over GIMPLE operands:
 *   - LHS of any statement  -> "defined"
 *   - RHS / call arguments  -> "used"
 * Variables that appear only on the LHS (never RHS) are reported.
 */
void check_unused_variables(function *fun);

#endif /* UNUSED_H */
