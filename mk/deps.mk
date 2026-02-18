# Dependency building rules for ralph

# Common download function - usage: $(call download,name,version,url)
# Downloads and extracts if not already present
define download_extract
	@mkdir -p $(DEPDIR)
	cd $(DEPDIR) && \
	if [ ! -f $(1)-$(2).tar.gz ]; then \
		curl -L -o $(1)-$(2).tar.gz $(3) || wget -O $(1)-$(2).tar.gz $(3); \
	fi && \
	if [ ! -d $(1)-$(2) ]; then \
		tar -xzf $(1)-$(2).tar.gz; \
	fi
endef

$(DEPDIR):
	mkdir -p $(DEPDIR)

# hnswlib (header-only)
$(HNSWLIB_DIR)/hnswlib/hnswlib.h: | $(DEPDIR)
	@echo "Downloading hnswlib..."
	$(call download_extract,hnswlib,$(HNSWLIB_VERSION),https://github.com/nmslib/hnswlib/archive/v$(HNSWLIB_VERSION).tar.gz)

# MbedTLS (stamp file prevents triple-build race with -j)
$(MBEDTLS_DIR)/.built: | $(DEPDIR)
	@echo "Building MbedTLS..."
	$(call download_extract,mbedtls,$(MBEDTLS_VERSION),https://github.com/Mbed-TLS/mbedtls/archive/v$(MBEDTLS_VERSION).tar.gz)
	cd $(MBEDTLS_DIR) && CC="$(CC)" CFLAGS="-O2" $(MAKE) lib
	@touch $@

$(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3): $(MBEDTLS_DIR)/.built

# libcurl (depends on MbedTLS)
$(CURL_LIB): $(MBEDTLS_LIB1) $(MBEDTLS_LIB2) $(MBEDTLS_LIB3)
	@echo "Building libcurl..."
	$(call download_extract,curl,$(CURL_VERSION),https://curl.se/download/curl-$(CURL_VERSION).tar.gz)
	cd $(CURL_DIR) && \
	CC="$(CC)" LD="apelink" \
		CPPFLAGS="-D_GNU_SOURCE -I$$(pwd)/../mbedtls-$(MBEDTLS_VERSION)/include" \
		LDFLAGS="-L$$(pwd)/../mbedtls-$(MBEDTLS_VERSION)/library" \
		./configure \
		--disable-shared --enable-static \
		--disable-ldap --disable-sspi --disable-tls-srp --disable-rtsp \
		--disable-proxy --disable-dict --disable-telnet --disable-tftp \
		--disable-pop3 --disable-smb --disable-smtp \
		--disable-gopher --disable-manual --disable-ipv6 --disable-ftp \
		--disable-file --disable-ntlm --disable-crypto-auth --disable-digest-auth \
		--disable-negotiate-auth --with-mbedtls --without-zlib --without-brotli \
		--without-zstd --without-libpsl --without-nghttp2 && \
	$(MAKE) CC="$(CC)"

# zlib
$(ZLIB_LIB): | $(DEPDIR)
	@echo "Building zlib..."
	@if [ -f $(DEPDIR)/zlib-$(ZLIB_VERSION).tar.gz ] && [ $$(wc -c < $(DEPDIR)/zlib-$(ZLIB_VERSION).tar.gz) -lt 10000 ]; then \
		echo "  Removing corrupt zlib tarball from cache"; \
		rm -f $(DEPDIR)/zlib-$(ZLIB_VERSION).tar.gz; \
	fi
	$(call download_extract,zlib,$(ZLIB_VERSION),https://github.com/madler/zlib/releases/download/v$(ZLIB_VERSION)/zlib-$(ZLIB_VERSION).tar.gz)
	cd $(ZLIB_DIR) && \
	CC="$(CC)" CFLAGS="-O2" ./configure --static && \
	$(MAKE) CC="$(CC)"

# PDFio (depends on zlib)
$(PDFIO_LIB): $(ZLIB_LIB) | $(DEPDIR)
	@echo "Building PDFio..."
	$(call download_extract,pdfio,$(PDFIO_VERSION),https://github.com/michaelrsweet/pdfio/archive/v$(PDFIO_VERSION).tar.gz)
	cd $(PDFIO_DIR) && \
	if [ ! -f Makefile ]; then \
		CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" \
			CFLAGS="-O2 -I$$(pwd)/../zlib-$(ZLIB_VERSION)" \
			LDFLAGS="-L$$(pwd)/../zlib-$(ZLIB_VERSION)" \
			./configure --disable-shared --enable-static; \
	fi && \
	sed -i 's|^AR[[:space:]]*=.*|AR\t\t=\t$(AR)|' Makefile && \
	CC="$(CC)" AR="$(AR)" RANLIB="cosmoranlib" \
		CFLAGS="-O2 -I$$(pwd)/../zlib-$(ZLIB_VERSION)" \
		LDFLAGS="-L$$(pwd)/../zlib-$(ZLIB_VERSION)" \
		$(MAKE) libpdfio.a

# cJSON
$(CJSON_LIB): | $(DEPDIR)
	@echo "Building cJSON..."
	$(call download_extract,cJSON,$(CJSON_VERSION),https://github.com/DaveGamble/cJSON/archive/v$(CJSON_VERSION).tar.gz)
	cd $(CJSON_DIR) && \
	$(MAKE) CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" CFLAGS="-O2 -fno-stack-protector" libcjson.a

# ncurses
$(NCURSES_LIB): | $(DEPDIR)
	@echo "Building ncurses..."
	$(call download_extract,ncurses,$(NCURSES_VERSION),https://ftp.gnu.org/gnu/ncurses/ncurses-$(NCURSES_VERSION).tar.gz)
	cd $(NCURSES_DIR) && \
	CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" CFLAGS="-O2 -fno-stack-protector" \
		./configure --disable-shared --enable-static --without-progs --without-tests \
		--without-cxx-binding --without-ada && \
	$(MAKE) CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)"

# readline (depends on ncurses; stamp file prevents double-build race with -j)
$(READLINE_DIR)/.built: $(NCURSES_LIB)
	@echo "Building Readline..."
	$(call download_extract,readline,$(READLINE_VERSION),https://ftp.gnu.org/gnu/readline/readline-$(READLINE_VERSION).tar.gz)
	cd $(READLINE_DIR) && \
	if [ ! -f doc/Makefile.in ]; then \
		echo "# Stub Makefile for readline doc directory" > doc/Makefile.in; \
		echo "all:" >> doc/Makefile.in; \
		echo "install:" >> doc/Makefile.in; \
		echo "clean:" >> doc/Makefile.in; \
	fi && \
	CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" \
		CFLAGS="-O2 -fno-stack-protector -I$$(pwd)/../ncurses-$(NCURSES_VERSION)/include" \
		LDFLAGS="-L$$(pwd)/../ncurses-$(NCURSES_VERSION)/lib" \
		./configure --disable-shared --enable-static && \
	$(MAKE) CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" \
		CFLAGS="-O2 -fno-stack-protector -I$$(pwd)/../ncurses-$(NCURSES_VERSION)/include" && \
	mkdir -p readline && \
	cd readline && \
	for f in ../*.h; do ln -sf "$$f" $$(basename "$$f"); done
	@touch $@

$(READLINE_LIB) $(HISTORY_LIB): $(READLINE_DIR)/.built

# SQLite
$(SQLITE_LIB): | $(DEPDIR)
	@echo "Building SQLite..."
	$(call download_extract,sqlite-autoconf,$(SQLITE_VERSION),https://www.sqlite.org/2024/sqlite-autoconf-$(SQLITE_VERSION).tar.gz)
	cd $(SQLITE_DIR) && \
	$(CC) -O2 -fno-stack-protector \
		-DSQLITE_THREADSAFE=1 \
		-DSQLITE_ENABLE_FTS5 \
		-DSQLITE_ENABLE_JSON1 \
		-DSQLITE_ENABLE_RTREE \
		-DSQLITE_ENABLE_MATH_FUNCTIONS \
		-DSQLITE_DQS=0 \
		-c sqlite3.c -o sqlite3.o && \
	mkdir -p .libs && \
	$(AR) rcs .libs/libsqlite3.a sqlite3.o && \
	$(RANLIB) .libs/libsqlite3.a

# OSSP UUID
$(OSSP_UUID_LIB): | $(DEPDIR)
	@echo "Building OSSP UUID..."
	$(call download_extract,uuid,$(OSSP_UUID_VERSION),https://deb.debian.org/debian/pool/main/o/ossp-uuid/ossp-uuid_$(OSSP_UUID_VERSION).orig.tar.gz)
	cd $(OSSP_UUID_DIR) && \
	cp /usr/share/misc/config.guess . && \
	cp /usr/share/misc/config.sub . && \
	CC="$(CC)" LD="apelink" AR="$(AR)" RANLIB="$(RANLIB)" CFLAGS="-O2 -fno-stack-protector" \
		./configure --disable-shared --enable-static --without-perl --without-php --without-pgsql && \
	$(MAKE) CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)"

# Python (delegated to python/Makefile)
$(PYTHON_LIB): $(ZLIB_LIB)
	@echo "Building Python..."
	$(MAKE) -C python
	@test -f $(PYTHON_LIB) || (echo "Error: Python build did not produce $(PYTHON_LIB)" && exit 1)

# CA Certificate bundle
$(CACERT_PEM):
	@echo "Downloading Mozilla CA certificate bundle..."
	@mkdir -p $(BUILDDIR)
	curl -sL https://curl.se/ca/cacert.pem -o $(CACERT_PEM)
	@echo "Downloaded CA bundle ($$(wc -c < $(CACERT_PEM) | tr -d ' ') bytes)"

$(CACERT_SOURCE): $(CACERT_PEM)
	@echo "Generating embedded CA certificate source..."
	./scripts/gen_cacert.sh $(CACERT_PEM) $(CACERT_SOURCE)

update-cacert:
	@echo "Updating Mozilla CA certificate bundle..."
	@mkdir -p $(BUILDDIR)
	curl -sL https://curl.se/ca/cacert.pem -o $(CACERT_PEM)
	./scripts/gen_cacert.sh $(CACERT_PEM) $(CACERT_SOURCE)
	@echo "CA certificate bundle updated. Rebuild with 'make clean && make'"

.PHONY: update-cacert
