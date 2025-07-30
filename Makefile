# Simple, sane Makefile for hello-world HTTP client
# No autotools bullshit, just straightforward make

CC = cosmocc
CFLAGS = -Wall -Wextra -Werror -O2 -std=c11
TARGET = hello-world
SRCDIR = src
TESTDIR = test
DEPDIR = deps

# Source files
SOURCES = $(SRCDIR)/main.c $(SRCDIR)/http_client.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = $(SRCDIR)/http_client.h

# Test files
TEST_SOURCES = $(TESTDIR)/test_main.c $(SRCDIR)/http_client.c $(TESTDIR)/unity/unity.c
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)
TEST_TARGET = $(TESTDIR)/test_main

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

# Default target
all: $(TARGET)

# Build main executable
$(TARGET): $(OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

# Compile source files
$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test target
test: $(TEST_TARGET)
	./$(TEST_TARGET)

check: test

# Build test executable
$(TEST_TARGET): $(TEST_OBJECTS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(LDFLAGS) -o $@ $(TEST_OBJECTS) $(LIBS)

# Compile test files
$(TESTDIR)/%.o: $(TESTDIR)/%.c $(HEADERS) $(CURL_LIB) $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

$(TESTDIR)/unity/%.o: $(TESTDIR)/unity/%.c
	$(CC) $(CFLAGS) $(TEST_INCLUDES) -c $< -o $@

# Valgrind testing
check-valgrind: $(TEST_TARGET)
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_TARGET).aarch64.elf

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
		CPPFLAGS="-D_GNU_SOURCE -I../mbedtls-$(MBEDTLS_VERSION)/include" \
		LDFLAGS="-L../mbedtls-$(MBEDTLS_VERSION)/library" \
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
	rm -f $(OBJECTS) $(TEST_OBJECTS) $(TARGET) $(TEST_TARGET)
	rm -f src/*.o test/*.o test/unity/*.o
	rm -f *.aarch64.elf *.com.dbg *.dbg src/*.aarch64.elf src/*.com.dbg src/*.dbg test/*.aarch64.elf test/*.com.dbg test/*.dbg
	rm -f test/*.log test/*.trs test/test-suite.log

distclean: clean
	rm -rf $(DEPDIR)
	rm -f *.tar.gz

# This target works without any configuration whatsoever
realclean: distclean

.PHONY: all test check check-valgrind deps clean distclean realclean