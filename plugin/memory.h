#ifndef MEMORY_H
#define MEMORY_H

/*
 * memory.h
 * Phase 2  — Memory Management Analysis (leak + double-free)
 * Phase 2b — Null Pointer Dereference Detection
 *
 * Both checks operate on the same GIMPLE/SSA IR and share the
 * pointer-state concept, so they live in the same translation unit.
 */

#include "gcc-plugin.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "basic-block.h"

/*
 * check_memory_management - Phase 2.
 * Detects:
 *   - Memory leaks   (malloc/calloc/realloc/strdup with no free)
 *   - Double frees   (free called twice on the same pointer)
 *
 * Uses a per-pointer state machine: UNKNOWN → ALLOCATED → FREED.
 */
void check_memory_management(function *fun);

/*
 * check_null_deref - Phase 2b.
 * Detects null pointer dereferences:
 *   - A pointer p is checked against NULL (if (!p) / if (p == NULL))
 *     which puts it in a "possibly null" state.
 *   - The same pointer is later dereferenced (*p, p->field, p[i])
 *     without an intervening NULL-check on the true path.
 *
 * Also catches the simpler pattern: pointer returned from
 * malloc/calloc used without checking the return value first.
 */
void check_null_deref(function *fun);

#endif /* MEMORY_H */
