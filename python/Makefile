# Cosmopolitan Python Build
# Shares dependencies with main ralph project (../deps/)
# Outputs static library to main build directory (../build/)

.PHONY: all clean distclean

# Dependency versions - must match main Makefile
PYTHON_VERSION := 3.12.3
ZLIB_VERSION := 1.3.1

# Paths
RALPH_ROOT := $(shell pwd)/..
DEPS_DIR := $(RALPH_ROOT)/deps
BUILD_OUTPUT := $(RALPH_ROOT)/build

# Shared dependencies
ZLIB_LIB := $(DEPS_DIR)/zlib-$(ZLIB_VERSION)/libz.a

all: python.com

# Build Python - depends on zlib from main project
python.com: build.py Setup.local $(ZLIB_LIB)
	uv run python build.py

# Build zlib if not already built by main Makefile
$(ZLIB_LIB):
	@echo "Building zlib dependency from main project..."
	$(MAKE) -C $(RALPH_ROOT) $(ZLIB_LIB)

clean:
	-$(MAKE) -C build/cpython-$(PYTHON_VERSION) clean 2>/dev/null || true
	rm -rf build/build build/install build/results
	rm -f python.com python.com.x86_64 python.com.aarch64
	rm -f $(BUILD_OUTPUT)/libpython*.a
	rm -rf $(BUILD_OUTPUT)/python-include

distclean: clean
	rm -rf __pycache__/
	rm -rf build/
	find . -name "*.pyc" -delete
