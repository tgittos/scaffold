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

all: $(TARGET) embed-python

$(TARGET): $(OBJECTS) $(ALL_LIBS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	@echo "Linking with PDFio support"
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS) -lpthread
	@echo "Saving base binary for smart embedding..."
	@uv run scripts/embed_python.py --save-base

embed-python: $(TARGET)
	@uv run scripts/embed_python.py

python: $(PYTHON_LIB)

# =============================================================================
# COMPILATION RULES
# =============================================================================

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TESTDIR)/%.o: $(TESTDIR)/%.c $(HEADERS) $(HNSWLIB_DIR)/hnswlib/hnswlib.h
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
