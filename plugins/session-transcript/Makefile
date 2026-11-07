CC := cosmocc
CFLAGS := -Wall -Wextra -Werror -O2 -std=c11

CJSON_DIR := ../../deps/cJSON-1.7.18
OUTDIR := ../../out/plugins
BUILDDIR := ../../build

SOURCES := main.c $(CJSON_DIR)/cJSON.c
TARGET := $(OUTDIR)/session-transcript
VERSION_HEADER := $(BUILDDIR)/plugin_session_transcript_version.h

all: $(TARGET)

$(VERSION_HEADER): FORCE
	@../../scripts/gen_plugin_version.sh session-transcript "$@"

FORCE:

$(TARGET): $(SOURCES) $(CJSON_DIR)/cJSON.h $(VERSION_HEADER)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -I$(CJSON_DIR) -include $(VERSION_HEADER) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)

.PHONY: all clean FORCE
