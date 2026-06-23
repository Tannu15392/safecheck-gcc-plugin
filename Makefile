# ==============================================================
#  Makefile — safecheck GCC plugin
#
#  Requirements (Ubuntu / WSL / Debian):
#    sudo apt install gcc-12 g++-12 gcc-12-plugin-dev
#
#  Targets:
#    make              build safecheck.so
#    make test         run all test cases (show expected output)
#    make test-uninit  run only the uninit test
#    make test-null    run only the null-deref test
#    make clean        remove build artifacts
#
#  Plugin source files (in plugin/):
#    safecheck.c  — plugin core, GCC glue, pass registration
#    unused.c     — Phase 1:  unused variable detection
#    memory.c     — Phase 2:  memory leak + double-free
#                   Phase 2b: null pointer dereference
#    uninit.c     — Phase 3:  variable used before initialization
# ==============================================================

GCC        := gcc-12
GPP        := g++-12
SO         := safecheck.so

# All plugin source files
SRCS := plugin/safecheck.c plugin/unused.c plugin/memory.c plugin/uninit.c
OBJS := $(SRCS:.c=.o)

# Plugin headers live inside the GCC installation
PLUGIN_INC := $(shell $(GCC) -print-file-name=plugin)/include

# Flags required for GCC plugins:
#   -fPIC      position-independent code (shared library)
#   -fno-rtti  GCC plugin headers require this (they use C-style casts)
#   -std=c++14 plugin API uses C++
#   -Iplugin   so unused.h / memory.h / uninit.h are found
CXXFLAGS := \
	-I$(PLUGIN_INC) \
	-Iplugin        \
	-fPIC           \
	-fno-rtti       \
	-std=c++14      \
	-O2             \
	-Wall           \
	-Wno-unused-parameter

.PHONY: all test test-uninit test-null test-memory test-unused clean check-deps

# ── Default target ─────────────────────────────────────────────
all: check-deps $(SO)

# Link all object files into the shared library
$(SO): $(OBJS)
	@echo "[BUILD] Linking safecheck plugin..."
	$(GPP) -shared -fno-rtti -o $@ $(OBJS)
	@echo "[BUILD] Done → $(SO)"

# Compile each .c file to a .o
plugin/%.o: plugin/%.c
	@echo "[BUILD] Compiling $<..."
	$(GPP) $(CXXFLAGS) -c $< -o $@

# ── Verify dependencies before building ───────────────────────
check-deps:
	@which $(GCC) > /dev/null 2>&1 || \
		(echo ""; \
		 echo "ERROR: $(GCC) not found."; \
		 echo "       Run: sudo apt install gcc-12 g++-12 gcc-12-plugin-dev"; \
		 echo ""; exit 1)
	@test -f "$(PLUGIN_INC)/gcc-plugin.h" || \
		(echo ""; \
		 echo "ERROR: GCC plugin headers not found."; \
		 echo "       Expected: $(PLUGIN_INC)/gcc-plugin.h"; \
		 echo "       Run: sudo apt install gcc-12-plugin-dev"; \
		 echo ""; exit 1)
	@echo "[CHECK] GCC 12 and plugin headers found — OK"

# ── Individual test targets ────────────────────────────────────

test-unused: $(SO)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo "  Phase 1: Unused Variable Tests"
	@echo "══════════════════════════════════════════"
	@echo "[TEST] test_unused_basic.c"
	@$(GCC) -fplugin=./$(SO) test/test_unused_basic.c -o /dev/null 2>&1 || true
	@echo ""
	@echo "[TEST] test_unused_multiple.c"
	@$(GCC) -fplugin=./$(SO) test/test_unused_multiple.c -o /dev/null 2>&1 || true
	@echo ""
	@echo "[TEST] test_loops.c (expect: zero warnings)"
	@$(GCC) -fplugin=./$(SO) test/test_loops.c -o /dev/null 2>&1 || true

test-memory: $(SO)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo "  Phase 2: Memory Leak & Double-Free Tests"
	@echo "══════════════════════════════════════════"
	@echo "[TEST] test_memory_leak.c"
	@$(GCC) -fplugin=./$(SO) test/test_memory_leak.c -o /dev/null 2>&1 || true
	@echo ""
	@echo "[TEST] test_double_free.c"
	@$(GCC) -fplugin=./$(SO) test/test_double_free.c -o /dev/null 2>&1 || true
	@echo ""
	@echo "[TEST] test_clean.c (expect: zero errors)"
	@$(GCC) -fplugin=./$(SO) test/test_clean.c -o /dev/null 2>&1 || true

test-null: $(SO)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo "  Phase 2b: Null Pointer Dereference Tests"
	@echo "══════════════════════════════════════════"
	@echo "[TEST] test_null_deref.c"
	@$(GCC) -fplugin=./$(SO) test/test_null_deref.c -o /dev/null 2>&1 || true

test-uninit: $(SO)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo "  Phase 3: Use Before Initialization Tests"
	@echo "══════════════════════════════════════════"
	@echo "[TEST] test_uninit.c"
	@$(GCC) -fplugin=./$(SO) test/test_uninit.c -o /dev/null 2>&1 || true

test: $(SO)
	@echo ""
	@echo "╔══════════════════════════════════════════╗"
	@echo "║       safecheck — full test suite        ║"
	@echo "╚══════════════════════════════════════════╝"
	@$(MAKE) test-unused  --no-print-directory
	@$(MAKE) test-memory  --no-print-directory
	@$(MAKE) test-null    --no-print-directory
	@$(MAKE) test-uninit  --no-print-directory
	@echo ""
	@echo "[DONE] All test cases completed."

# ── Clean build artifacts ──────────────────────────────────────
clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -f $(OBJS) $(SO)
	@echo "[CLEAN] Done"
