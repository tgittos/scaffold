# ralph Makefile - Modular build system
# Built with Cosmopolitan for universal binary compatibility

# =============================================================================
# INCLUDES
# =============================================================================

include mk/config.mk
include mk/sources.mk
include mk/deps.mk
include mk/tests.mk

# =============================================================================
# PRIMARY TARGETS
# =============================================================================

# Set default goal explicitly (included makefiles define targets before this)
.DEFAULT_GOAL := all

# Default target builds the complete ralph binary with embedded Python
all: $(BUILDDIR)/.ralph-linked embed-python

# Linking step - produces the base binary and saves it for embedding
$(BUILDDIR)/.ralph-linked: $(OBJECTS) $(ALL_LIBS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	@echo "Linking with PDFio support"
	$(CXX) $(LDFLAGS) -o $(TARGET) $(OBJECTS) $(LIBS) -lpthread
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

# Dependencies required for include paths to exist (order-only prerequisites)
# These ensure deps are downloaded/built before compilation, without triggering
# rebuilds when libs change
COMPILE_DEPS := $(LIBS_MBEDTLS) $(CJSON_LIB) $(SQLITE_LIB) $(PDFIO_LIB) $(ZLIB_LIB) \
    $(OSSP_UUID_LIB) $(READLINE_LIB) $(HNSWLIB_DIR)/hnswlib/hnswlib.h

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) | $(COMPILE_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS) | $(COMPILE_DEPS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TESTDIR)/%.o: $(TESTDIR)/%.c $(HEADERS) | $(COMPILE_DEPS)
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

$(TESTDIR)/unity/%.o: $(TESTDIR)/unity/%.c
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

# =============================================================================
# CLEAN TARGETS
# =============================================================================

clean:
	rm -f $(OBJECTS) $(TARGET) $(ALL_TEST_TARGETS)
	rm -f src/*.o src/*/*.o test/*.o test/*/*.o test/unity/*.o
	rm -f *.aarch64.elf *.com.dbg *.dbg
	rm -f src/*.aarch64.elf src/*/*.aarch64.elf src/*.com.dbg src/*/*.com.dbg src/*.dbg src/*/*.dbg
	rm -f test/*.aarch64.elf test/*/*.aarch64.elf test/*.com.dbg test/*/*.com.dbg test/*.dbg test/*/*.dbg
	rm -f test/*.log test/*.trs test/test-suite.log
	rm -f $(EMBEDDED_LINKS_HEADER)
	rm -f $(BUILDDIR)/.ralph-linked
	find build -type f ! -name 'bin2c.c' ! -name 'links' ! -name 'libpython*.a' ! -path 'build/python-include/*' -delete 2>/dev/null || true

clean-python:
	rm -f $(PYTHON_LIB)
	rm -rf $(PYTHON_INCLUDE)
	$(MAKE) -C python clean

distclean: clean
	rm -rf $(DEPDIR)
	rm -f *.tar.gz
	rm -f $(LINKS_BUNDLED)
	$(MAKE) -C python distclean

# =============================================================================
# PHONY TARGETS
# =============================================================================

.PHONY: all test check check-valgrind clean clean-python distclean python embed-python update-cacert
