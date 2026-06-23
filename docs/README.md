# safecheck — A Rust-Inspired Static Analyzer for C

A GCC plugin that performs four categories of static analysis at compile time, inspired by Rust's ownership and safety guarantees.

---

## Table of Contents

1. [What is safecheck?](#1-what-is-safecheck)
2. [Project Layout](#2-project-layout)
3. [Prerequisites & Installation](#3-prerequisites--installation)
4. [Build & Usage](#4-build--usage)
5. [What Each Phase Detects](#5-what-each-phase-detects)
6. [How GCC Plugins Work — A to Z](#6-how-gcc-plugins-work--a-to-z)
7. [Compiler Design Phases & Where We Hook In](#7-compiler-design-phases--where-we-hook-in)
8. [What is GIMPLE?](#8-what-is-gimple)
9. [What is SSA?](#9-what-is-ssa)
10. [How Each Phase Uses GIMPLE & SSA](#10-how-each-phase-uses-gimple--ssa)
11. [Architecture: How the Files Connect](#11-architecture-how-the-files-connect)
12. [safecheck.c Deep Dive — The Plugin Core](#12-safecheckc-deep-dive--the-plugin-core)
13. [Phase 1 — Unused Variables (unused.c)](#13-phase-1--unused-variables-unusedc)
14. [Phase 2 — Memory Leak & Double-Free (memory.c)](#14-phase-2--memory-leak--double-free-memoryc)
15. [Phase 2b — Null Pointer Dereference (memory.c)](#15-phase-2b--null-pointer-dereference-memoryc)
16. [Phase 3 — Variable Used Before Initialization (uninit.c)](#16-phase-3--variable-used-before-initialization-uninitc)
17. [Key GCC Internal APIs](#17-key-gcc-internal-apis)
18. [Test Suite](#18-test-suite)
19. [Expected Output Examples](#19-expected-output-examples)
20. [Common Pitfalls & FAQ](#20-common-pitfalls--faq)

---

## 1. What is safecheck?

safecheck is a **GCC compiler plugin** written in C++. It runs as an additional pass inside the GCC compilation pipeline and reports bugs **at compile time**, before the program ever runs.

It is "Rust-inspired" because Rust's compiler catches categories of bugs at compile time that C traditionally only discovers at runtime (via crashes, Valgrind, AddressSanitizer, etc.). safecheck brings some of those compile-time guarantees to C.

### What it catches:

| Phase | Category | Example |
|-------|----------|---------|
| 1 | Unused variables | `int x = 5;` — x never read |
| 2 | Memory leaks | `malloc(64)` with no `free()` |
| 2 | Double free | `free(p); free(p);` |
| 2b | Null pointer deref | `char *p = malloc(n); p[0] = 'x';` without null check |
| 3 | Use before init | `int x; printf("%d", x);` |

---

## 2. Project Layout

```
safecheck/
├── Makefile                   ← build + test runner
├── plugin/
│   ├── safecheck.c            ← plugin core: GCC glue, pass registration
│   ├── unused.c               ← Phase 1: unused variable detection
│   ├── unused.h
│   ├── memory.c               ← Phase 2 + 2b: memory management + null deref
│   ├── memory.h
│   ├── uninit.c               ← Phase 3: use before initialization
│   └── uninit.h
└── test/
    ├── test_clean.c           ← no errors expected (regression)
    ├── test_unused_basic.c    ← Phase 1 tests
    ├── test_unused_multiple.c ← Phase 1 tests
    ├── test_loops.c           ← Phase 1 false-positive guard
    ├── test_memory_leak.c     ← Phase 2 tests
    ├── test_double_free.c     ← Phase 2 tests
    ├── test_null_deref.c      ← Phase 2b tests (new)
    ├── test_uninit.c          ← Phase 3 tests (new)
    ├── test_demo.c            ← combined demo
    └── test_cpp.cpp           ← C++ compatibility test
```

---

## 3. Prerequisites & Installation

```bash
# Ubuntu / Debian / WSL
sudo apt update
sudo apt install gcc-12 g++-12 gcc-12-plugin-dev

# Verify
gcc-12 --version
ls $(gcc-12 -print-file-name=plugin)/include/gcc-plugin.h
```

The `gcc-12-plugin-dev` package installs the GCC internal C++ header files that your plugin code `#include`s. These are not part of the standard GCC distribution; they are a separate development package.

---

## 4. Build & Usage

```bash
# Build the plugin shared library
make

# Run the full test suite
make test

# Run individual phase tests
make test-unused
make test-memory
make test-null
make test-uninit

# Use the plugin on your own code
gcc-12 -fplugin=./safecheck.so your_file.c

# Use with level=warn (only warnings, no errors)
gcc-12 -fplugin=./safecheck.so -fplugin-arg-safecheck-level=warn your_file.c

# C++ files
g++-12 -fplugin=./safecheck.so your_file.cpp
```

---

## 5. What Each Phase Detects

### Phase 1 — Unused Variables
A variable that is **written to but never read**. Common causes: copy-paste errors, abandoned debug variables, forgetting to use a computed value.

```c
int compute(int a, int b) {
    int x = a * 100;   // ← warning: unused variable 'x'
    int result = a + b;
    return result;
}
```

### Phase 2 — Memory Leak
Memory obtained from `malloc`, `calloc`, `realloc`, or `strdup` that is never `free()`d before the function returns.

```c
void leaks(void) {
    char *buf = malloc(64);   // ← error: memory leak: 'buf' never freed
    strcpy(buf, "hello");
    // forgot free(buf)
}
```

### Phase 2 — Double Free
Calling `free()` twice on the same pointer. This causes heap corruption and undefined behavior.

```c
void bad(void) {
    char *p = malloc(32);
    free(p);
    free(p);   // ← error: double free of 'p'
}
```

### Phase 2b — Null Pointer Dereference
Using the return value of `malloc`/`calloc`/etc. without checking if it is NULL first. On memory-exhausted systems, allocation can fail and return NULL.

```c
void bad(void) {
    int *arr = malloc(100 * sizeof(int));
    arr[0] = 42;   // ← error: null pointer dereference: 'arr' may be NULL
}

void good(void) {
    int *arr = malloc(100 * sizeof(int));
    if (!arr) return;   // ← safe: NULL checked
    arr[0] = 42;
}
```

### Phase 3 — Variable Used Before Initialization
Reading a local variable that has never been assigned a value. Its content is undefined (whatever bytes happen to be on the stack).

```c
void bad(void) {
    int x;
    printf("%d\n", x);   // ← error: 'x' used before initialization
}

void good(void) {
    int x = 0;           // initialized
    printf("%d\n", x);   // safe
}
```

---

## 6. How GCC Plugins Work — A to Z

### What is a GCC plugin?

GCC is designed to be extensible. A **plugin** is a shared library (`.so` file on Linux) that GCC loads at startup. The plugin registers callbacks and passes that GCC calls at specific points during compilation.

### The loading mechanism

When you run:
```bash
gcc-12 -fplugin=./safecheck.so your_file.c
```

GCC does:
1. Loads `safecheck.so` with `dlopen()`.
2. Looks for the symbol `plugin_init` and calls it.
3. Your `plugin_init` function registers passes and callbacks.
4. GCC then compiles `your_file.c` normally, running your passes at the registered points.

### The GPL compatibility symbol

Every GCC plugin **must** define:
```c
int plugin_is_GPL_compatible;
```

This is a safety mechanism. GCC's runtime is GPL-licensed. By declaring this symbol, your plugin asserts it is GPL-compatible. GCC refuses to load plugins that don't declare this.

### Version checking

GCC plugin ABI changes between versions. The first thing `plugin_init` does is:
```c
if (!plugin_default_version_check(version, &gcc_version)) {
    return 1;  // reject: plugin compiled against a different GCC
}
```

This ensures you don't accidentally load a plugin compiled against GCC 11 into GCC 12, which would cause crashes from struct layout mismatches.

---

## 7. Compiler Design Phases & Where We Hook In

A classic compiler has these phases:

```
Source Text
     │
     ▼
┌─────────────┐
│   Lexer     │  Characters → tokens (keywords, identifiers, literals)
└─────────────┘
     │
     ▼
┌─────────────┐
│   Parser    │  Tokens → Abstract Syntax Tree (AST)
└─────────────┘
     │
     ▼
┌─────────────┐
│  Semantic   │  Type checking, name resolution
│  Analysis   │
└─────────────┘
     │
     ▼
┌─────────────┐
│ IR / Middle │  Convert AST to intermediate representation
│   End       │  GCC uses: GENERIC → GIMPLE → SSA-GIMPLE   ← WE HOOK HERE
└─────────────┘
     │
     ▼
┌─────────────┐
│  Optimizer  │  Dead code elimination, inlining, etc.
└─────────────┘
     │
     ▼
┌─────────────┐
│  Code Gen   │  IR → machine instructions
└─────────────┘
     │
     ▼
  Object File (.o)
```

### Where safecheck runs

We register our pass **after the `ssa` pass**:

```c
pass_info.reference_pass_name = "ssa";
pass_info.pos_op              = PASS_POS_INSERT_AFTER;
```

This means:
- The CFG (control flow graph) has been built ✓
- SSA form has been constructed ✓
- The code is still close to what the programmer wrote (before aggressive optimizations alter it) ✓

This is the ideal point: we have structured IR to analyze, but haven't lost information to optimizer transformations.

### GCC's IR pipeline in more detail

```
C Source
    │
    ▼ (cc1 front-end)
GENERIC trees     ← AST-like, high level
    │
    ▼ (gimplify pass)
GIMPLE            ← simplified, 3-address code
    │
    ▼ (CFG pass)
GIMPLE + CFG      ← basic blocks with edges
    │
    ▼ (SSA pass)   ← WE RUN HERE
GIMPLE + SSA      ← every variable def is unique
    │
    ▼ (our pass)
safecheck analyses run
    │
    ▼ (optimizer passes)
optimized GIMPLE
    │
    ▼ (RTL generation)
Register Transfer Language
    │
    ▼ (assembly generation)
.s file → .o file
```

---

## 8. What is GIMPLE?

GIMPLE is GCC's internal **Intermediate Representation (IR)** — a simplified form of C code that is much easier to analyze than the original source.

### GIMPLE rules

1. **Maximum three operands per statement** (like 3-address code in textbooks).
2. **No nested expressions** — `a = b * (c + d)` becomes two statements:
   ```
   t1 = c + d;
   a  = b * t1;
   ```
3. **No complex control flow** — all loops and conditions become `if/goto`.
4. **Explicit temporary variables** for intermediate results.

### GIMPLE example

C source:
```c
int y = (a + b) * (c - d);
```

GIMPLE:
```
_1 = a + b;
_2 = c - d;
y  = _1 * _2;
```

### GIMPLE statement types

| Code | Meaning | Example |
|------|---------|---------|
| `GIMPLE_ASSIGN` | Assignment | `x = y + z` |
| `GIMPLE_CALL` | Function call | `malloc(64)` |
| `GIMPLE_COND` | Conditional branch | `if (x == 0) goto L1` |
| `GIMPLE_RETURN` | Return statement | `return x` |
| `GIMPLE_PHI` | SSA phi node | `x_3 = PHI(x_1, x_2)` |

### Basic blocks and the CFG

GIMPLE is organized into **basic blocks** — straight-line sequences of code with no internal branches. Branches only happen at the END of a basic block.

The **Control Flow Graph (CFG)** connects basic blocks with edges representing possible execution paths.

```
           ┌─────────────┐
           │   ENTRY     │
           └──────┬──────┘
                  │
           ┌──────▼──────┐
           │  BB 2        │  x = malloc(64);
           │              │  if (x == 0) ...
           └──┬───────┬──┘
              │       │
    (null)    │       │ (not null)
              │       │
       ┌──────▼─┐  ┌──▼──────┐
       │  BB 3  │  │  BB 4   │
       │ return │  │ use x   │
       └────────┘  └─────────┘
```

We iterate over basic blocks using `FOR_EACH_BB_FN(bb, fun)` and over statements inside a block using a `gimple_stmt_iterator`.

---

## 9. What is SSA?

**Static Single Assignment (SSA)** form is a property of the IR where **every variable is assigned exactly once**.

### Why it matters for analysis

In ordinary code:
```c
int x = 5;    // first write
x = x + 1;   // second write — overwrites
```

In SSA form:
```
x_1 = 5;
x_2 = x_1 + 1;
```

Every "version" of a variable has a unique name (`x_1`, `x_2`). This makes **data flow** trivial: to find where `x_2` comes from, you just look at its single definition site.

### SSA_NAME nodes

In GCC's internal representation:
- Every use/def of a variable in SSA form is an `SSA_NAME` tree node.
- `SSA_NAME_VAR(ssa_name)` gives you the original `VAR_DECL`.
- `SSA_NAME_DEF_STMT(ssa_name)` gives you the statement that defines this version.
- `SSA_NAME_IS_DEFAULT_DEF(ssa_name)` is true if this version has NO definition inside the function — it represents the value of the variable at function entry (which is garbage for local variables).

### PHI nodes

At join points (where two execution paths merge), SSA needs a way to say "x_3 is either x_1 or x_2 depending on which path was taken":

```
x_3 = PHI(x_1/BB3, x_2/BB4)
```

PHI nodes are pseudo-statements that GCC inserts at the beginning of basic blocks. They are key to understanding data flow through branches.

### Why SSA is perfect for safecheck

- **Use before init**: if a variable has no real definition, GCC creates a "default definition" SSA_NAME. Detecting `SSA_NAME_IS_DEFAULT_DEF` == true directly maps to "use before initialization".
- **Def-use chains**: to find all uses of a defined value, follow the SSA chain — no alias analysis needed for simple cases.
- **Pointer state tracking**: each assignment to a pointer variable creates a new SSA version, making state transitions easy to track.

---

## 10. How Each Phase Uses GIMPLE & SSA

### Phase 1 (Unused Variables) — uses GIMPLE operands

Does NOT need SSA directly. We just:
- Walk every statement in every basic block.
- Record the LHS (left-hand side) of assignments in a `defined` set.
- Record all RHS operands and call arguments in a `used` set.
- Variables in `defined` but not `used` → unused.

Uses `hash_set<tree>` (GCC's internal hash set) for O(1) lookup.

### Phase 2 (Memory Management) — uses GIMPLE call recognition

- Recognizes `GIMPLE_CALL` statements by checking the function name via `gimple_call_fndecl`.
- Tracks per-pointer state with a linked list (small functions → linear scan is fast enough).
- Uses `strip_ssa()` to convert SSA_NAME operands back to their underlying `VAR_DECL` so we can correlate malloc with the corresponding free.

### Phase 2b (Null Dereference) — uses GIMPLE tree structure

- Recognizes `INDIRECT_REF`, `COMPONENT_REF` (for `->`) tree nodes in statement operands.
- Recognizes `GIMPLE_COND` with a zero-comparison to detect null checks.
- State machine per pointer: UNCHECKED → CHECKED transitions on `GIMPLE_COND`.

### Phase 3 (Use Before Init) — uses SSA default definitions

This is the most SSA-native phase:
- Pass 1: collect all VAR_DECLs that appear as an LHS in any statement (they have a real definition).
- Pass 2: scan all RHS SSA_NAME operands. If `SSA_NAME_IS_DEFAULT_DEF` is true AND the VAR_DECL is not in our "defined" set → use before initialization.

---

## 11. Architecture: How the Files Connect

```
gcc-12 loads safecheck.so
         │
         ▼
   plugin_init()         ← safecheck.c
         │
         ├─ version check
         ├─ parse args
         └─ register safecheck_pass
                  │
                  │ (GCC runs the pass on each function)
                  ▼
         safecheck_pass::execute(fun)
                  │
                  ├─ check_unused_variables(fun)  ← unused.c
                  ├─ check_memory_management(fun) ← memory.c
                  ├─ check_null_deref(fun)         ← memory.c
                  └─ check_uninit_variables(fun)  ← uninit.c
```

Each analysis function:
1. Calls `FOR_EACH_BB_FN` to iterate basic blocks.
2. Uses `gsi_start_bb` / `gsi_next` to iterate statements.
3. Calls `gsi_stmt` to get the current `gimple*` statement.
4. Inspects the statement using GCC API calls.
5. Emits diagnostics via `fprintf(stderr, ...)`.

We use `fprintf(stderr)` directly rather than GCC's `warning_at()` / `error_at()` because:
- `warning_at()` with `OPT_Wunused_variable` only fires with `-Wall`.
- `warning_at()` with option `0` is silently dropped on GCC 12.
- `fprintf(stderr)` is never filtered, always captured by `2>&1`.

---

## 12. safecheck.c Deep Dive — The Plugin Core

### plugin_init signature

```c
int plugin_init(struct plugin_name_args   *plugin_info,
                struct plugin_gcc_version *version)
```

- `plugin_info` contains: plugin file path, base name, argument array.
- `version` contains: what GCC version is currently running.
- Returns 0 on success, 1 on failure.

### Argument parsing

```c
for (int i = 0; i < plugin_info->argc; i++) {
    const char *key = plugin_info->argv[i].key;
    const char *val = plugin_info->argv[i].value;
    // -fplugin-arg-safecheck-level=warn
    // key = "level", val = "warn"
}
```

### Pass registration

```c
struct register_pass_info pass_info;
pass_info.pass                     = new safecheck_pass(g);
pass_info.reference_pass_name      = "ssa";
pass_info.ref_pass_instance_number = 1;
pass_info.pos_op                   = PASS_POS_INSERT_AFTER;

register_callback(plugin_info->base_name,
                  PLUGIN_PASS_MANAGER_SETUP,
                  NULL,
                  &pass_info);
```

- `g` is the global GCC context object.
- `PASS_POS_INSERT_AFTER` means: run our pass after the `ssa` pass completes.
- `PLUGIN_PASS_MANAGER_SETUP` is the callback event for pass registration.

### The pass class

```cpp
class safecheck_pass : public gimple_opt_pass {
public:
    safecheck_pass(gcc::context *ctx)
        : gimple_opt_pass(safecheck_pass_data, ctx) {}

    bool gate(function *) override { return true; }   // always run

    unsigned int execute(function *fun) override {
        // called once per function in the translation unit
        check_unused_variables(fun);
        check_memory_management(fun);
        check_null_deref(fun);
        check_uninit_variables(fun);
        return 0;  // no PROP_* flags changed
    }
};
```

`pass_data` specifies `PROP_cfg | PROP_ssa` as `properties_required`, telling GCC our pass needs these IR properties to already be built before we execute.

---

## 13. Phase 1 — Unused Variables (unused.c)

### Core data structures

```cpp
hash_set<tree> *defined;   // VAR_DECLs that appear on any LHS
hash_set<tree> *used;      // VAR_DECLs that appear on any RHS
```

`tree` is GCC's universal node type. Every AST/GIMPLE node is a `tree`. `hash_set<tree>` uses pointer identity for hashing, which is correct because each `VAR_DECL` is a unique heap object that represents one variable.

### collect_uses logic

```c
// Unwrap &arr[0] → arr (address-of array argument pattern)
if (TREE_CODE(op) == ADDR_EXPR) {
    tree inner = TREE_OPERAND(op, 0);
    if (inner && TREE_CODE(inner) == ARRAY_REF)
        inner = TREE_OPERAND(inner, 0);  // get the array decl
    inner = strip_ssa(inner);
    if (inner && TREE_CODE(inner) == VAR_DECL)
        used->add(inner);
}
```

This handles `printf("%s", arr)` which in GIMPLE becomes `printf("%s", &arr[0])` — an ADDR_EXPR wrapping an ARRAY_REF. Without this, arrays passed to functions would appear as "unused".

### is_real_var filter

```c
static bool is_real_var(tree decl) {
    if (DECL_ARTIFICIAL(decl)) return false;  // skip compiler temps
    if (DECL_EXTERNAL(decl))   return false;  // skip extern decls
    if (TREE_STATIC(decl))     return false;  // skip static/global
    return true;
}
```

GCC creates many artificial variables internally (e.g., for exception handling, stack frame management). We skip these to avoid false warnings.

---

## 14. Phase 2 — Memory Leak & Double-Free (memory.c)

### State machine

```
      malloc()
UNKNOWN ────────→ ALLOCATED
   ↑                  │
   │ ptr=NULL         │ free()
   │                  ↓
   └────────────── FREED ─── free() → DOUBLE FREE ERROR
```

### Key implementation detail: strip_ssa

```c
static tree strip_ssa(tree t) {
    if (TREE_CODE(t) == SSA_NAME) {
        tree var = SSA_NAME_VAR(t);
        return var ? var : t;
    }
    return t;
}
```

In SSA form, `buf` appears as `buf_3` (an SSA_NAME). `SSA_NAME_VAR` extracts the original `VAR_DECL`. This allows us to correlate `buf_1 = malloc(...)` with `free(buf_2)` — different SSA versions, same underlying variable.

### Allocation recognition

```c
static bool is_alloc_call(gimple *stmt) {
    if (!is_gimple_call(stmt)) return false;
    const char *n = get_fn_name(stmt);
    return (!strcmp(n, "malloc")  ||
            !strcmp(n, "calloc")  ||
            !strcmp(n, "realloc") ||
            !strcmp(n, "strdup"));
}
```

`gimple_call_fndecl(stmt)` returns the function's `DECL` node. `DECL_NAME` gives the identifier. `IDENTIFIER_POINTER` gives the C string.

### ptr = NULL reset

```c
if (gimple_code(stmt) == GIMPLE_ASSIGN) {
    tree lhs = strip_ssa(gimple_get_lhs(stmt));
    tree rhs = strip_ssa(gimple_assign_rhs1(stmt));
    if (lhs && is_real_var(lhs) &&
        POINTER_TYPE_P(TREE_TYPE(lhs)) &&
        rhs && integer_zerop(rhs))         // NULL is integer 0
        ptr_get(lhs)->state = PTR_UNKNOWN;
}
```

`ptr = NULL` is the safe idiom after `free()`. We reset the state to UNKNOWN so a subsequent `free(ptr)` on the NULL pointer won't trigger a double-free (which is safe in C — `free(NULL)` is a no-op).

---

## 15. Phase 2b — Null Pointer Dereference (memory.c)

### State machine

```
      malloc()
UNKNOWN ────────→ UNCHECKED ──── deref (*ptr, ptr->field) ──→ ERROR
                      │
                  if(!ptr)
                      │
                      ▼
                   CHECKED (safe)
```

### Dereference detection

```c
static bool is_deref_of(tree expr, tree target_decl) {
    switch (TREE_CODE(expr)) {
    case INDIRECT_REF:       // *ptr
        return strip_ssa(TREE_OPERAND(expr, 0)) == target_decl;
    case COMPONENT_REF:      // ptr->field (base is INDIRECT_REF)
    case ARRAY_REF:          // ptr[i]
        return is_deref_of(TREE_OPERAND(expr, 0), target_decl);
    }
}
```

In GIMPLE's tree representation, `ptr->field` is represented as:
```
COMPONENT_REF
  └── INDIRECT_REF
        └── ptr (SSA_NAME or VAR_DECL)
  └── field_decl
```

So recursing on `COMPONENT_REF`'s first operand reaches the `INDIRECT_REF`.

### NULL check detection

```c
if (gimple_code(stmt) == GIMPLE_COND) {
    tree cond_lhs = gimple_cond_lhs(stmt);
    tree cond_rhs = gimple_cond_rhs(stmt);
    // if cond_rhs is 0 (NULL), then cond_lhs is the pointer being tested
    if (integer_zerop(cond_rhs))
        ptr_side = strip_ssa(cond_lhs);
}
```

`if (!ptr)` compiles to `GIMPLE_COND: ptr_1 == 0`. `if (ptr == NULL)` also becomes `ptr_1 == 0` since `NULL` is defined as `(void*)0`.

---

## 16. Phase 3 — Variable Used Before Initialization (uninit.c)

### The SSA default definition trick

This is the most elegant phase, leaning entirely on SSA's properties.

When GCC builds SSA form and encounters a USE of a variable that has no preceding definition, it creates a **default definition**:

```
x_0 = undefined  ← default def (represents function entry value)
     ...
     USE(x_0)     ← this is the use before init
```

`SSA_NAME_IS_DEFAULT_DEF(x_0)` returns `true` for these nodes.

For **local variables** (not parameters), the value at function entry is garbage. So:

```
is_real_local_var(decl) && SSA_NAME_IS_DEFAULT_DEF(ssa_name)
→ use before initialization
```

### Why we also collect "defined" vars (Pass 1)

Consider:
```c
int x;
if (flag) x = 5;
printf("%d", x);  // maybe uninitialized
```

Here `x` has BOTH a real definition (on the `if` path) and a default-def (on the `else` path). In SSA form:
```
x_1 = 5         (inside if)
x_2 = PHI(x_1, x_0)   (at merge point, x_0 is default-def)
printf(x_2)
```

This is the "maybe uninitialized" case. It requires path-sensitive analysis to be certain. We conservatively skip it (because `x` appears in our `defined` set) to avoid false positives. The zero-false-positive guarantee is more valuable than catching every possible case.

### PHI node scanning

```c
if (gimple_code(stmt) == GIMPLE_PHI) {
    unsigned n_args = gimple_phi_num_args(stmt);
    for (unsigned i = 0; i < n_args; i++) {
        tree arg = gimple_phi_arg_def(stmt, i);
        check_ssa_name_for_uninit(arg, loc, reported, defined);
    }
}
```

PHI node inputs are not in `gimple_op()`, so we scan them separately. A PHI input that is a default-def means "on this predecessor path, the variable was never assigned" — another form of use-before-init.

---

## 17. Key GCC Internal APIs

| API | Purpose |
|-----|---------|
| `FOR_EACH_BB_FN(bb, fun)` | Iterate all basic blocks in a function |
| `gsi_start_bb(bb)` | Get iterator at start of basic block |
| `gsi_end_p(gsi)` | Check if iterator is past end |
| `gsi_next(&gsi)` | Advance iterator |
| `gsi_stmt(gsi)` | Get current statement |
| `gimple_code(stmt)` | Get statement type (GIMPLE_ASSIGN, etc.) |
| `gimple_location(stmt)` | Get source location |
| `gimple_get_lhs(stmt)` | Get left-hand side of assignment |
| `gimple_assign_rhs1(stmt)` | Get first RHS operand |
| `gimple_call_fndecl(stmt)` | Get function decl from call |
| `gimple_call_num_args(stmt)` | Number of call arguments |
| `gimple_call_arg(stmt, i)` | Get i-th argument |
| `gimple_cond_lhs/rhs(stmt)` | Operands of conditional |
| `gimple_phi_num_args(stmt)` | Number of PHI inputs |
| `gimple_phi_arg_def(stmt, i)` | i-th PHI input |
| `is_gimple_call(stmt)` | True if stmt is a call |
| `TREE_CODE(t)` | Get node type |
| `SSA_NAME_VAR(t)` | Get VAR_DECL from SSA_NAME |
| `SSA_NAME_IS_DEFAULT_DEF(t)` | True if no definition in function |
| `DECL_NAME(decl)` | Get name identifier |
| `IDENTIFIER_POINTER(id)` | Get C string from identifier |
| `DECL_SOURCE_LOCATION(decl)` | Declaration source location |
| `expand_location(loc)` | Expand to file/line/col |
| `POINTER_TYPE_P(type)` | True if type is a pointer |
| `integer_zerop(t)` | True if tree is integer 0 (NULL) |

---

## 18. Test Suite

| Test file | Phase | Tests |
|-----------|-------|-------|
| `test_unused_basic.c` | 1 | Basic unused variables |
| `test_unused_multiple.c` | 1 | Multiple unused per function |
| `test_loops.c` | 1 | Loop vars must NOT be flagged (false-positive guard) |
| `test_memory_leak.c` | 2 | malloc without free |
| `test_double_free.c` | 2 | free() called twice |
| `test_null_deref.c` | 2b | malloc return not checked |
| `test_uninit.c` | 3 | Variable used before assignment |
| `test_clean.c` | all | No errors expected (regression) |
| `test_demo.c` | all | Combined demo with multiple issues |
| `test_cpp.cpp` | all | C++ compatibility |

---

## 19. Expected Output Examples

### test_null_deref.c
```
test/test_null_deref.c:25:5: error: null pointer dereference: 'buf' may be NULL
test/test_null_deref.c:24:18: note: 'buf' was allocated here; allocation functions can return NULL
test/test_null_deref.c:25:5: note: suggestion: add 'if (!buf) return;' after the allocation

test/test_null_deref.c:50:5: error: null pointer dereference: 'node' may be NULL
test/test_null_deref.c:49:19: note: 'node' was allocated here; allocation functions can return NULL
```

### test_uninit.c
```
test/test_uninit.c:22:5: error: variable 'x' is used before initialization
test/test_uninit.c:20:9: note: 'x' was declared here but never assigned before use
test/test_uninit.c:20:9: note: suggestion: initialize 'x' at declaration, e.g., 'x = 0;'

test/test_uninit.c:44:9: error: variable 'val' is used before initialization
test/test_uninit.c:40:9: note: 'val' was declared here but never assigned before use
```

---

## 20. Common Pitfalls & FAQ

**Q: Why does safecheck use `fprintf(stderr)` instead of `warning_at()`?**

`warning_at()` with `OPT_Wunused_variable` only fires when the user passes `-Wall`. With option `0` it is silently dropped by GCC 12. `fprintf(stderr)` always works and is captured by `2>&1`.

**Q: Why does the plugin miss some cases?**

safecheck is intentionally conservative (no false positives):
- Phase 3 only reports the default-def case (guaranteed uninitialized), not the "maybe uninitialized" case (would need path-sensitive analysis).
- Phase 2b only reports unchecked malloc results, not arbitrary pointer arithmetic.
- Phase 2 does not track pointers through function calls or struct fields.

**Q: Can I use safecheck with CMake / other build systems?**

Yes. Add `-fplugin=/path/to/safecheck.so` to `CMAKE_C_FLAGS` or the equivalent in your build system.

**Q: Why must the plugin use C++ even though the analysis code looks like C?**

GCC's internal API is written in C++. The plugin must subclass `gimple_opt_pass` (a C++ class), use `hash_set<tree>` (a C++ template), and link against GCC's C++ runtime. The analysis logic uses C idioms where possible, but the plugin infrastructure requires C++.

**Q: What is `xmalloc` in the plugin code?**

`xmalloc` is GCC's own malloc wrapper that calls `fatal_error` on allocation failure instead of returning NULL. Since the plugin runs inside the compiler process, an OOM should abort compilation rather than silently continue with a NULL pointer.

**Q: How do I suppress a warning for a specific variable?**

Phase 1 warns about unused variables. Rename them with a leading underscore (e.g., `_debug`) — the suggestion in the output tells you this. For other phases, the correct fix is to fix the code: add null checks, free memory, or initialize variables.
