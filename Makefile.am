# Build dependencies first, then subdirectories
SUBDIRS = . src test

# libcurl dependency variables
CURL_VERSION = 8.4.0
CURL_DIR = deps/curl-$(CURL_VERSION)
CURL_ARCHIVE = curl-$(CURL_VERSION).tar.gz
CURL_URL = https://curl.se/download/$(CURL_ARCHIVE)
CURL_PREFIX = $(abs_builddir)/deps/curl-install

# MbedTLS dependency variables
MBEDTLS_VERSION = 3.5.1
MBEDTLS_DIR = deps/mbedtls-$(MBEDTLS_VERSION)
MBEDTLS_ARCHIVE = mbedtls-$(MBEDTLS_VERSION).tar.gz
MBEDTLS_URL = https://github.com/Mbed-TLS/mbedtls/archive/v$(MBEDTLS_VERSION).tar.gz

# Build MbedTLS as a dependency
$(MBEDTLS_DIR)/library/libmbedtls.a: | deps
	cd deps && \
	if [ ! -f $(MBEDTLS_ARCHIVE) ]; then \
		curl -L -o $(MBEDTLS_ARCHIVE) $(MBEDTLS_URL) || wget -O $(MBEDTLS_ARCHIVE) $(MBEDTLS_URL); \
	fi && \
	if [ ! -d mbedtls-$(MBEDTLS_VERSION) ]; then \
		tar -xzf $(MBEDTLS_ARCHIVE); \
	fi && \
	cd mbedtls-$(MBEDTLS_VERSION) && \
	CC="cosmocc" CFLAGS="-O2" make lib

# Build libcurl as a dependency
$(CURL_DIR)/lib/.libs/libcurl.a: $(MBEDTLS_DIR)/library/libmbedtls.a | deps
	cd deps && \
	if [ ! -f $(CURL_ARCHIVE) ]; then \
		curl -L -o $(CURL_ARCHIVE) $(CURL_URL) || wget -O $(CURL_ARCHIVE) $(CURL_URL); \
	fi && \
	if [ ! -d curl-$(CURL_VERSION) ]; then \
		tar -xzf $(CURL_ARCHIVE); \
	fi && \
	cd curl-$(CURL_VERSION) && \
	CC="cosmocc" LD="apelink" \
		CPPFLAGS="-D_GNU_SOURCE -I$(abs_builddir)/deps/mbedtls-$(MBEDTLS_VERSION)/include" \
		LDFLAGS="-L$(abs_builddir)/deps/mbedtls-$(MBEDTLS_VERSION)/library" \
		./configure \
		--prefix=$(CURL_PREFIX) --disable-shared --enable-static \
		--disable-ldap --disable-sspi --disable-tls-srp --disable-rtsp \
		--disable-proxy --disable-dict --disable-telnet --disable-tftp \
		--disable-pop3 --disable-imap --disable-smb --disable-smtp \
		--disable-gopher --disable-manual --disable-ipv6 --disable-ftp \
		--disable-file --with-mbedtls --without-zlib --without-brotli \
		--without-zstd --without-libpsl --without-nghttp2 && \
	$(MAKE) CC="cosmocc" && \
	$(MAKE) install

deps:
	mkdir -p deps

# Build dependencies in this directory first
all-local: $(CURL_DIR)/lib/.libs/libcurl.a

# Make subdirectories depend on local targets
src test: all-local

CLEANFILES = hello-world hello-world.aarch64.elf hello-world.com.dbg a.out.aarch64.elf a.out.com.dbg

DISTCLEANFILES = aclocal.m4 compile config.log config.status configure configure~ depcomp install-sh missing Makefile.in src/Makefile.in test/Makefile.in config.guess config.sub test-driver

clean-local:
	rm -rf autom4te.cache .aarch64

distclean-local:
	rm -rf autom4te.cache .aarch64 deps

# Export library paths for subdirectories
export CURL_BUILD_DIR = $(abs_builddir)/deps/curl-$(CURL_VERSION)
export MBEDTLS_BUILD_DIR = $(abs_builddir)/deps/mbedtls-$(MBEDTLS_VERSION)

.PHONY: deps all-local