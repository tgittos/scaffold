# ralph Makefile - Modular build system
# Built with Cosmopolitan for universal binary compatibility

# =============================================================================
# INCLUDES
# =============================================================================

include mk/config.mk
include mk/lib.mk
include mk/sources.mk
include mk/deps.mk
include mk/tests.mk

# =============================================================================
# VERSION HEADER
# =============================================================================

VERSION_HEADER := $(BUILDDIR)/version.h

# Always run gen_version.sh, but the script only touches the file when content
# changes (compare-and-swap), so unchanged builds don't trigger recompilation.
# After the first build, compiler-generated .o.d files track version.h as a
# dependency of any .o that includes it — so only those files recompile.
$(VERSION_HEADER): FORCE
	@scripts/gen_version.sh "$(RALPH_VERSION_MAJOR)" "$(RALPH_VERSION_MINOR)" "$(RALPH_VERSION_PATCH)" "$@"

FORCE:

# Ensure version header exists before any source file compiles
COMPILE_DEPS += $(VERSION_HEADER)

# =============================================================================
# PRIMARY TARGETS
# =============================================================================

# Set default goal explicitly (included makefiles define targets before this)
.DEFAULT_GOAL := all

# Default target builds the complete ralph binary with embedded Python and all tests
all: $(BUILDDIR)/.ralph-linked embed-python libralph $(ALL_TEST_TARGETS)

# Alias for test dependencies that reference 'ralph' directly
ralph: $(BUILDDIR)/.ralph-linked

# Linking step - produces the base binary and saves it for embedding
$(BUILDDIR)/.ralph-linked: $(OBJECTS) $(ALL_LIBS) $(LIBRALPH) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	@echo "Linking with PDFio support"
	$(CXX) $(LDFLAGS) -o $(TARGET) $(OBJECTS) $(LIB_OBJECTS) $(LIBS) -lpthread
	@echo "Saving base binary for smart embedding..."
	@uv run scripts/embed_python.py --save-base
	@touch $@

# Embed Python stdlib into the binary (can be run separately to re-embed)
embed-python: $(BUILDDIR)/.ralph-linked
	@uv run scripts/embed_python.py

python: $(PYTHON_LIB)

# =============================================================================
# COMPILATION RULES
# =============================================================================

# COMPILE_DEPS is defined in mk/config.mk (must be before lib.mk is included)

$(SRCDIR)/%.o: $(SRCDIR)/%.c | $(COMPILE_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp | $(COMPILE_DEPS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TESTDIR)/%.o: $(TESTDIR)/%.c | $(COMPILE_DEPS)
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

$(TESTDIR)/unity/%.o: $(TESTDIR)/unity/%.c
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

# =============================================================================
# CLEAN TARGETS
# =============================================================================

clean:
	rm -f $(OBJECTS) $(TARGET) $(ALL_TEST_TARGETS)
	rm -f src/*.o src/*/*.o test/*.o test/*/*.o test/unity/*.o
	rm -f src/*.o.d src/*/*.o.d test/*.o.d test/*/*.o.d test/unity/*.o.d lib/*.o.d lib/*/*.o.d lib/*/*/*.o.d
	rm -f *.aarch64.elf *.com.dbg *.dbg
	rm -f src/*.aarch64.elf src/*/*.aarch64.elf src/*.com.dbg src/*/*.com.dbg src/*.dbg src/*/*.dbg
	rm -f test/*.aarch64.elf test/*/*.aarch64.elf test/*.com.dbg test/*/*.com.dbg test/*.dbg test/*/*.dbg
	rm -f test/*.log test/*.trs test/test-suite.log
	rm -f $(BUILDDIR)/.ralph-linked
	find build -type f ! -name 'libpython*.a' ! -path 'build/python-include/*' -delete 2>/dev/null || true

clean-python:
	rm -f $(PYTHON_LIB)
	rm -rf $(PYTHON_INCLUDE)
	$(MAKE) -C python clean

distclean: clean
	rm -rf $(DEPDIR)
	rm -f *.tar.gz
	$(MAKE) -C python distclean

# =============================================================================
# PHONY TARGETS
# =============================================================================

# =============================================================================
# AUTOMATIC DEPENDENCY TRACKING
# =============================================================================

# .mk file changes trigger rebuilds (no more "rm stale binary" workaround)
$(OBJECTS): Makefile mk/config.mk mk/sources.mk
$(LIB_OBJECTS): Makefile mk/config.mk mk/lib.mk
$(ALL_TEST_TARGETS): mk/tests.mk mk/config.mk

# Include compiler-generated header dependencies (.o.d files from -MMD -MP)
-include $(wildcard src/*.o.d src/*/*.o.d lib/*.o.d lib/*/*.o.d lib/*/*/*.o.d test/*.o.d test/*/*.o.d test/unity/*.o.d \
    src/*/.aarch64/*.o.d lib/*/.aarch64/*.o.d lib/*/*/.aarch64/*.o.d test/*/.aarch64/*.o.d test/unity/.aarch64/*.o.d)

.PHONY: all test check check-valgrind clean clean-python distclean python embed-python update-cacert
