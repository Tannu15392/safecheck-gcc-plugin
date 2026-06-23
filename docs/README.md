# SafeCheck

SafeCheck is a GCC Plugin for Static Code Analysis that detects common programming issues during compilation.

## Features

- Unused Variable Detection
- Memory Leak Detection
- Double Free Detection
- Null Pointer Dereference Detection
- Use Before Initialization Detection

## Technologies Used

- C/C++
- GCC Plugin API
- GIMPLE IR
- SSA
- Control Flow Graph (CFG)

## Project Structure

```text
plugin/
├── safecheck.c
├── unused.c
├── memory.c
└── uninit.c

test/
├── test_memory_leak.c
├── test_null_deref.c
├── test_uninit.c
└── ...
```

## Build

```bash
make
```

## Run

```bash
gcc-12 -fplugin=./safecheck.so test.c
```

## Sample Issues Detected

- Unused Variables
- Memory Leaks
- Double Free Errors
- Null Pointer Dereferences
- Use Before Initialization

## Future Improvements

- Interprocedural Analysis
- Alias Analysis
- Path-Sensitive Analysis
