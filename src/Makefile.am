bin_PROGRAMS = hello-world
hello_world_SOURCES = main.c http_client.c http_client.h

# Override DEFS to replace spaces with underscores for cosmopolitan compatibility  
override DEFS := $(subst hello-world\ 1.0,hello-world_1.0,$(DEFS))

# libcurl and MbedTLS paths
CURL_BUILD_DIR = $(abs_builddir)/../deps/curl-8.4.0
MBEDTLS_BUILD_DIR = $(abs_builddir)/../deps/mbedtls-3.5.1

hello_world_CPPFLAGS = -I$(CURL_BUILD_DIR)/include -I$(MBEDTLS_BUILD_DIR)/include
hello_world_LDFLAGS = -L$(CURL_BUILD_DIR)/lib/.libs -L$(MBEDTLS_BUILD_DIR)/library
hello_world_LDADD = -lcurl -lmbedtls -lmbedx509 -lmbedcrypto