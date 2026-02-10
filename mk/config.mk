# Configuration variables for ralph build system

CC := cosmocc
CXX := cosmoc++
AR := cosmoar
RANLIB := aarch64-linux-cosmo-ranlib

CFLAGS := -Wall -Wextra -Werror -O2 -std=c11 -DHAVE_PDFIO -MMD -MP
CXXFLAGS := -Wall -Wextra -Werror -O2 -std=c++14 -Wno-unused-parameter -Wno-unused-function -Wno-type-limits -MMD -MP

TARGET := ralph

# Project version (single source of truth — build/version.h is generated from these)
RALPH_VERSION_MAJOR := 0
RALPH_VERSION_MINOR := 1
RALPH_VERSION_PATCH := 0

# Directory structure
SRCDIR := src
LIBDIR := lib
TESTDIR := test
DEPDIR := deps
BUILDDIR := build

# Dependency versions
CURL_VERSION := 8.4.0
MBEDTLS_VERSION := 3.5.1
HNSWLIB_VERSION := 0.8.0
PDFIO_VERSION := 1.3.1
ZLIB_VERSION := 1.3.1
CJSON_VERSION := 1.7.18
READLINE_VERSION := 8.2
NCURSES_VERSION := 6.4
SQLITE_VERSION := 3450000
OSSP_UUID_VERSION := 1.6.2
PYTHON_VERSION := 3.12

# Dependency directories
CURL_DIR := $(DEPDIR)/curl-$(CURL_VERSION)
MBEDTLS_DIR := $(DEPDIR)/mbedtls-$(MBEDTLS_VERSION)
HNSWLIB_DIR := $(DEPDIR)/hnswlib-$(HNSWLIB_VERSION)
PDFIO_DIR := $(DEPDIR)/pdfio-$(PDFIO_VERSION)
ZLIB_DIR := $(DEPDIR)/zlib-$(ZLIB_VERSION)
CJSON_DIR := $(DEPDIR)/cJSON-$(CJSON_VERSION)
READLINE_DIR := $(DEPDIR)/readline-$(READLINE_VERSION)
NCURSES_DIR := $(DEPDIR)/ncurses-$(NCURSES_VERSION)
SQLITE_DIR := $(DEPDIR)/sqlite-autoconf-$(SQLITE_VERSION)
OSSP_UUID_DIR := $(DEPDIR)/uuid-$(OSSP_UUID_VERSION)

# Library paths
CURL_LIB := $(CURL_DIR)/lib/.libs/libcurl.a
MBEDTLS_LIB1 := $(MBEDTLS_DIR)/library/libmbedtls.a
MBEDTLS_LIB2 := $(MBEDTLS_DIR)/library/libmbedx509.a
MBEDTLS_LIB3 := $(MBEDTLS_DIR)/library/libmbedcrypto.a
PDFIO_LIB := $(PDFIO_DIR)/libpdfio.a
ZLIB_LIB := $(ZLIB_DIR)/libz.a
CJSON_LIB := $(CJSON_DIR)/libcjson.a
READLINE_LIB := $(READLINE_DIR)/libreadline.a
HISTORY_LIB := $(READLINE_DIR)/libhistory.a
NCURSES_LIB := $(NCURSES_DIR)/lib/libncurses.a
SQLITE_LIB := $(SQLITE_DIR)/.libs/libsqlite3.a
OSSP_UUID_LIB := $(OSSP_UUID_DIR)/.libs/libuuid.a
PYTHON_LIB := $(BUILDDIR)/libpython$(PYTHON_VERSION).a
PYTHON_INCLUDE := $(BUILDDIR)/python-include

# Grouped library sets
LIBS_MBEDTLS := $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
ALL_LIBS := $(LIBS_MBEDTLS) $(PDFIO_LIB) $(ZLIB_LIB) $(CJSON_LIB) $(READLINE_LIB) $(HISTORY_LIB) $(NCURSES_LIB) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(PYTHON_LIB)
# Omits READLINE_LIB/HISTORY_LIB/NCURSES_LIB — no test reaches the REPL path in libralph.a.
# If a future test calls agent_run() or repl functions, add those libs or use $(LIBS) instead.
LIBS_STANDARD := $(LIBS_MBEDTLS) $(PDFIO_LIB) $(CJSON_LIB) $(SQLITE_LIB) $(OSSP_UUID_LIB) $(PYTHON_LIB) $(ZLIB_LIB) -lm -lpthread

# Dependencies required for include paths to exist (order-only prerequisites)
# These ensure deps are downloaded/built before compilation, without triggering
# rebuilds when libs change. Must be defined here (before lib.mk is included).
COMPILE_DEPS := $(LIBS_MBEDTLS) $(CJSON_LIB) $(SQLITE_LIB) $(PDFIO_LIB) $(ZLIB_LIB) \
    $(OSSP_UUID_LIB) $(READLINE_LIB) $(PYTHON_LIB) $(HNSWLIB_DIR)/hnswlib/hnswlib.h

# Include paths
# -I. allows includes like "lib/tools/tools_system.h"
INCLUDES := -I. -I$(CURL_DIR)/include -I$(MBEDTLS_DIR)/include -I$(HNSWLIB_DIR) \
    -I$(PDFIO_DIR) -I$(ZLIB_DIR) -I$(CJSON_DIR) -I$(READLINE_DIR) \
    -I$(READLINE_DIR)/readline -I$(NCURSES_DIR)/include -I$(SQLITE_DIR) \
    -I$(OSSP_UUID_DIR) -I$(PYTHON_INCLUDE) \
    -I$(BUILDDIR)/generated \
    -I$(SRCDIR) -I$(SRCDIR)/ralph -I$(SRCDIR)/llm \
    -I$(SRCDIR)/session -I$(SRCDIR)/tools -I$(SRCDIR)/utils -I$(SRCDIR)/db \
    -I$(SRCDIR)/cli

TEST_INCLUDES := $(INCLUDES) -I$(TESTDIR)/unity -I$(TESTDIR) \
    -I$(TESTDIR)/ralph -I$(TESTDIR)/network -I$(TESTDIR)/llm \
    -I$(TESTDIR)/session -I$(TESTDIR)/tools -I$(TESTDIR)/utils -I$(TESTDIR)/policy

LDFLAGS := -L$(CURL_DIR)/lib/.libs -L$(MBEDTLS_DIR)/library -L$(PDFIO_DIR) \
    -L$(ZLIB_DIR) -L$(CJSON_DIR) -L$(READLINE_DIR) -L$(NCURSES_DIR)/lib

LIBS := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto $(PDFIO_LIB) $(ZLIB_LIB) \
    $(CJSON_LIB) $(READLINE_LIB) $(HISTORY_LIB) $(NCURSES_LIB) $(SQLITE_LIB) \
    $(OSSP_UUID_LIB) $(PYTHON_LIB) -lm

# CA Certificate
CACERT_PEM := $(BUILDDIR)/cacert.pem
CACERT_SOURCE := $(LIBDIR)/network/embedded_cacert.c

# Python paths
PYTHON_STDLIB_DIR := python/build/results/py-tmp
PYTHON_DEFAULTS_DIR := src/tools/python_defaults

# Valgrind settings
VALGRIND_FLAGS := --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1
