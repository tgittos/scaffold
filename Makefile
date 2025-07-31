# Simple, sane Makefile for ralph HTTP client
# No autotools bullshit, just straightforward make

CC = cosmocc
CFLAGS = -Wall -Wextra -Werror -O2 -std=c11
TARGET = ralph
SRCDIR = src
TESTDIR = test
DEPDIR = deps

# Source files
SOURCES = $(SRCDIR)/main.c $(SRCDIR)/ralph.c $(SRCDIR)/http_client.c $(SRCDIR)/env_loader.c $(SRCDIR)/output_formatter.c $(SRCDIR)/prompt_loader.c $(SRCDIR)/conversation_tracker.c $(SRCDIR)/tools_system.c $(SRCDIR)/shell_tool.c $(SRCDIR)/file_tools.c $(SRCDIR)/links_tool.c $(SRCDIR)/debug_output.c $(SRCDIR)/api_common.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = $(SRCDIR)/ralph.h $(SRCDIR)/http_client.h $(SRCDIR)/env_loader.h $(SRCDIR)/output_formatter.h $(SRCDIR)/prompt_loader.h $(SRCDIR)/conversation_tracker.h $(SRCDIR)/tools_system.h $(SRCDIR)/shell_tool.h $(SRCDIR)/file_tools.h $(SRCDIR)/links_tool.h $(SRCDIR)/debug_output.h $(SRCDIR)/embedded_links.h $(SRCDIR)/api_common.h

# Tools
BIN2C = build/bin2c

# Test files
TEST_MAIN_SOURCES = $(TESTDIR)/test_main.c $(TESTDIR)/unity/unity.c
TEST_MAIN_OBJECTS = $(TEST_MAIN_SOURCES:.c=.o)
TEST_MAIN_TARGET = $(TESTDIR)/test_main

TEST_HTTP_SOURCES = $(TESTDIR)/test_http_client.c $(SRCDIR)/http_client.c $(SRCDIR)/env_loader.c $(TESTDIR)/unity/unity.c
TEST_HTTP_OBJECTS = $(TEST_HTTP_SOURCES:.c=.o)
TEST_HTTP_TARGET = $(TESTDIR)/test_http_client

TEST_ENV_SOURCES = $(TESTDIR)/test_env_loader.c $(SRCDIR)/env_loader.c $(TESTDIR)/unity/unity.c
TEST_ENV_OBJECTS = $(TEST_ENV_SOURCES:.c=.o)
TEST_ENV_TARGET = $(TESTDIR)/test_env_loader

TEST_OUTPUT_SOURCES = $(TESTDIR)/test_output_formatter.c $(SRCDIR)/output_formatter.c $(SRCDIR)/debug_output.c $(TESTDIR)/unity/unity.c
TEST_OUTPUT_OBJECTS = $(TEST_OUTPUT_SOURCES:.c=.o)
TEST_OUTPUT_TARGET = $(TESTDIR)/test_output_formatter

TEST_PROMPT_SOURCES = $(TESTDIR)/test_prompt_loader.c $(SRCDIR)/prompt_loader.c $(TESTDIR)/unity/unity.c
TEST_PROMPT_OBJECTS = $(TEST_PROMPT_SOURCES:.c=.o)
TEST_PROMPT_TARGET = $(TESTDIR)/test_prompt_loader

TEST_CONVERSATION_SOURCES = $(TESTDIR)/test_conversation_tracker.c $(SRCDIR)/conversation_tracker.c $(TESTDIR)/unity/unity.c
TEST_CONVERSATION_OBJECTS = $(TEST_CONVERSATION_SOURCES:.c=.o)
TEST_CONVERSATION_TARGET = $(TESTDIR)/test_conversation_tracker

TEST_TOOLS_SOURCES = $(TESTDIR)/test_tools_system.c $(SRCDIR)/tools_system.c $(SRCDIR)/shell_tool.c $(SRCDIR)/file_tools.c $(SRCDIR)/links_tool.c $(TESTDIR)/unity/unity.c
TEST_TOOLS_OBJECTS = $(TEST_TOOLS_SOURCES:.c=.o)
TEST_TOOLS_TARGET = $(TESTDIR)/test_tools_system

TEST_SHELL_SOURCES = $(TESTDIR)/test_shell_tool.c $(SRCDIR)/shell_tool.c $(SRCDIR)/tools_system.c $(SRCDIR)/file_tools.c $(SRCDIR)/links_tool.c $(TESTDIR)/unity/unity.c
TEST_SHELL_OBJECTS = $(TEST_SHELL_SOURCES:.c=.o)
TEST_SHELL_TARGET = $(TESTDIR)/test_shell_tool

TEST_FILE_SOURCES = $(TESTDIR)/test_file_tools.c $(SRCDIR)/file_tools.c $(SRCDIR)/tools_system.c $(SRCDIR)/shell_tool.c $(SRCDIR)/links_tool.c $(TESTDIR)/unity/unity.c
TEST_FILE_OBJECTS = $(TEST_FILE_SOURCES:.c=.o)
TEST_FILE_TARGET = $(TESTDIR)/test_file_tools

TEST_RALPH_SOURCES = $(TESTDIR)/test_ralph.c $(TESTDIR)/mock_api_server.c $(SRCDIR)/ralph.c $(SRCDIR)/http_client.c $(SRCDIR)/env_loader.c $(SRCDIR)/output_formatter.c $(SRCDIR)/prompt_loader.c $(SRCDIR)/conversation_tracker.c $(SRCDIR)/tools_system.c $(SRCDIR)/shell_tool.c $(SRCDIR)/file_tools.c $(SRCDIR)/links_tool.c $(SRCDIR)/debug_output.c $(SRCDIR)/api_common.c $(TESTDIR)/unity/unity.c
TEST_RALPH_OBJECTS = $(TEST_RALPH_SOURCES:.c=.o)
TEST_RALPH_TARGET = $(TESTDIR)/test_ralph

TEST_BUNDLED_LINKS = test_bundled_links

ALL_TEST_TARGETS = $(TEST_MAIN_TARGET) $(TEST_HTTP_TARGET) $(TEST_ENV_TARGET) $(TEST_OUTPUT_TARGET) $(TEST_PROMPT_TARGET) $(TEST_CONVERSATION_TARGET) $(TEST_TOOLS_TARGET) $(TEST_SHELL_TARGET) $(TEST_FILE_TARGET) $(TEST_RALPH_TARGET)

# Dependencies
CURL_VERSION = 8.4.0
MBEDTLS_VERSION = 3.5.1
CURL_DIR = $(DEPDIR)/curl-$(CURL_VERSION)
MBEDTLS_DIR = $(DEPDIR)/mbedtls-$(MBEDTLS_VERSION)

# Dependency paths
CURL_LIB = $(CURL_DIR)/lib/.libs/libcurl.a
MBEDTLS_LIB1 = $(MBEDTLS_DIR)/library/libmbedtls.a
MBEDTLS_LIB2 = $(MBEDTLS_DIR)/library/libmbedx509.a  
MBEDTLS_LIB3 = $(MBEDTLS_DIR)/library/libmbedcrypto.a

# Include and library flags
INCLUDES = -I$(CURL_DIR)/include -I$(MBEDTLS_DIR)/include
TEST_INCLUDES = $(INCLUDES) -I$(TESTDIR)/unity -I$(SRCDIR)
LDFLAGS = -L$(CURL_DIR)/lib/.libs -L$(MBEDTLS_DIR)/library
LIBS = -lcurl -lmbedtls -lmbedx509 -lmbedcrypto
RALPH_TEST_LIBS = $(LIBS) -lpthread

# Bundled Links binary
LINKS_BUNDLED = build/links
EMBEDDED_LINKS_HEADER = $(SRCDIR)/embedded_links.h

# Default target
all: $(TARGET)

# Build bundled links test
$(TEST_BUNDLED_LINKS): test_bundled_links.c $(SRCDIR)/tools_system.o $(SRCDIR)/links_tool.o $(SRCDIR)/shell_tool.o $(SRCDIR)/file_tools.o $(EMBEDDED_LINKS_HEADER)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(SRCDIR)/tools_system.o $(SRCDIR)/links_tool.o $(SRCDIR)/shell_tool.o $(SRCDIR)/file_tools.o

# Build main executable
$(TARGET): $(EMBEDDED_LINKS_HEADER) $(OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

# Build bin2c tool
$(BIN2C): build/bin2c.c
	$(CC) -O2 -o $@ $<

# Generate embedded links header
$(EMBEDDED_LINKS_HEADER): $(LINKS_BUNDLED) $(BIN2C)
	$(BIN2C) $(LINKS_BUNDLED) embedded_links > $(EMBEDDED_LINKS_HEADER)

# Download pre-built Cosmopolitan Links binary
$(LINKS_BUNDLED):
	@echo "Checking for pre-built Cosmopolitan Links binary..."
	@if [ ! -f $(LINKS_BUNDLED) ]; then \
		echo "Downloading pre-built Cosmopolitan Links binary..."; \
		curl -L -o $(LINKS_BUNDLED) https://cosmo.zip/pub/cosmos/bin/links || \
		wget -O $(LINKS_BUNDLED) https://cosmo.zip/pub/cosmos/bin/links; \
		chmod +x $(LINKS_BUNDLED); \
	else \
		echo "Using existing $(LINKS_BUNDLED)"; \
	fi

# Compile source files (links_tool.o depends on embedded_links.h)
$(SRCDIR)/links_tool.o: $(SRCDIR)/links_tool.c $(EMBEDDED_LINKS_HEADER) $(HEADERS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile other source files
$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test targets
test: $(ALL_TEST_TARGETS)
	./$(TEST_MAIN_TARGET)
	./$(TEST_HTTP_TARGET)
	./$(TEST_ENV_TARGET)
	./$(TEST_OUTPUT_TARGET)
	./$(TEST_PROMPT_TARGET)
	./$(TEST_CONVERSATION_TARGET)
	./$(TEST_TOOLS_TARGET)
	./$(TEST_SHELL_TARGET)
	./$(TEST_FILE_TARGET)
	./$(TEST_RALPH_TARGET)

check: test

# Build test executables
$(TEST_MAIN_TARGET): $(TEST_MAIN_OBJECTS)
	$(CC) -o $@ $(TEST_MAIN_OBJECTS)

$(TEST_HTTP_TARGET): $(TEST_HTTP_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(LDFLAGS) -o $@ $(TEST_HTTP_OBJECTS) $(LIBS)

$(TEST_ENV_TARGET): $(TEST_ENV_OBJECTS)
	$(CC) -o $@ $(TEST_ENV_OBJECTS)

$(TEST_OUTPUT_TARGET): $(TEST_OUTPUT_OBJECTS)
	$(CC) -o $@ $(TEST_OUTPUT_OBJECTS)

$(TEST_PROMPT_TARGET): $(TEST_PROMPT_OBJECTS)
	$(CC) -o $@ $(TEST_PROMPT_OBJECTS)

$(TEST_CONVERSATION_TARGET): $(TEST_CONVERSATION_OBJECTS)
	$(CC) -o $@ $(TEST_CONVERSATION_OBJECTS)

$(TEST_TOOLS_TARGET): $(TEST_TOOLS_OBJECTS)
	$(CC) -o $@ $(TEST_TOOLS_OBJECTS)

$(TEST_SHELL_TARGET): $(TEST_SHELL_OBJECTS)
	$(CC) -o $@ $(TEST_SHELL_OBJECTS)

$(TEST_FILE_TARGET): $(TEST_FILE_OBJECTS)
	$(CC) -o $@ $(TEST_FILE_OBJECTS)

$(TEST_RALPH_TARGET): $(TEST_RALPH_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(LDFLAGS) -o $@ $(TEST_RALPH_OBJECTS) $(RALPH_TEST_LIBS)

# Compile test files
$(TESTDIR)/%.o: $(TESTDIR)/%.c $(HEADERS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

$(TESTDIR)/unity/%.o: $(TESTDIR)/unity/%.c
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

# Valgrind testing (excluding HTTP tests due to external library noise)
check-valgrind: $(ALL_TEST_TARGETS)
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_MAIN_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_ENV_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_OUTPUT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_PROMPT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_CONVERSATION_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_TOOLS_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_SHELL_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_FILE_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_RALPH_TARGET).aarch64.elf

# Valgrind testing for all tests (including external libraries - may show false positives)
check-valgrind-all: $(ALL_TEST_TARGETS)
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_MAIN_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_HTTP_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_ENV_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_OUTPUT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_PROMPT_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_CONVERSATION_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_TOOLS_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_SHELL_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_FILE_TARGET).aarch64.elf
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_RALPH_TARGET).aarch64.elf

# Dependencies (redundant - libraries are built automatically when needed)

# Create deps directory
$(DEPDIR):
	mkdir -p $(DEPDIR)

# Build MbedTLS libraries
$(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3): | $(DEPDIR)
	@echo "Building MbedTLS..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f mbedtls-$(MBEDTLS_VERSION).tar.gz ]; then \
		curl -L -o mbedtls-$(MBEDTLS_VERSION).tar.gz https://github.com/Mbed-TLS/mbedtls/archive/v$(MBEDTLS_VERSION).tar.gz || \
		wget -O mbedtls-$(MBEDTLS_VERSION).tar.gz https://github.com/Mbed-TLS/mbedtls/archive/v$(MBEDTLS_VERSION).tar.gz; \
	fi && \
	if [ ! -d mbedtls-$(MBEDTLS_VERSION) ]; then \
		tar -xzf mbedtls-$(MBEDTLS_VERSION).tar.gz; \
	fi && \
	cd mbedtls-$(MBEDTLS_VERSION) && \
	CC="$(CC)" CFLAGS="-O2" $(MAKE) lib

# Build libcurl (depends on MbedTLS)
$(CURL_LIB): $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	@echo "Building libcurl..."
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f curl-$(CURL_VERSION).tar.gz ]; then \
		curl -L -o curl-$(CURL_VERSION).tar.gz https://curl.se/download/curl-$(CURL_VERSION).tar.gz || \
		wget -O curl-$(CURL_VERSION).tar.gz https://curl.se/download/curl-$(CURL_VERSION).tar.gz; \
	fi && \
	if [ ! -d curl-$(CURL_VERSION) ]; then \
		tar -xzf curl-$(CURL_VERSION).tar.gz; \
	fi && \
	cd curl-$(CURL_VERSION) && \
	CC="$(CC)" LD="apelink" \
		CPPFLAGS="-D_GNU_SOURCE -I$$(pwd)/../mbedtls-$(MBEDTLS_VERSION)/include" \
		LDFLAGS="-L$$(pwd)/../mbedtls-$(MBEDTLS_VERSION)/library" \
		./configure \
		--disable-shared --enable-static \
		--disable-ldap --disable-sspi --disable-tls-srp --disable-rtsp \
		--disable-proxy --disable-dict --disable-telnet --disable-tftp \
		--disable-pop3 --disable-imap --disable-smb --disable-smtp \
		--disable-gopher --disable-manual --disable-ipv6 --disable-ftp \
		--disable-file --disable-ntlm --disable-crypto-auth --disable-digest-auth --disable-negotiate-auth --with-mbedtls --without-zlib --without-brotli \
		--without-zstd --without-libpsl --without-nghttp2 && \
	$(MAKE) CC="$(CC)"

# Clean targets
clean:
	rm -f $(OBJECTS) $(TEST_MAIN_OBJECTS) $(TEST_HTTP_OBJECTS) $(TEST_RALPH_OBJECTS) $(TARGET) $(ALL_TEST_TARGETS) $(TEST_BUNDLED_LINKS)
	rm -f src/*.o test/*.o test/unity/*.o
	rm -f *.aarch64.elf *.com.dbg *.dbg src/*.aarch64.elf src/*.com.dbg src/*.dbg test/*.aarch64.elf test/*.com.dbg test/*.dbg
	rm -f test/*.log test/*.trs test/test-suite.log
	rm -f $(EMBEDDED_LINKS_HEADER)
	# Clean non-tracked files from build directory (keep bin2c.c and links)
	find build -type f ! -name 'bin2c.c' ! -name 'links' -delete 2>/dev/null || true

distclean: clean
	rm -rf $(DEPDIR)
	rm -f *.tar.gz
	rm -f $(LINKS_BUNDLED)

# This target works without any configuration whatsoever
realclean: distclean

.PHONY: all test check check-valgrind deps clean distclean realclean