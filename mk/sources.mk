# Source file definitions for ralph and scaffold

# Core application sources (ralph binary)
CORE_SOURCES := \
    $(SRCDIR)/ralph/main.c

# Tool system (Python tools only; core tools in lib/tools/)
TOOL_SOURCES := \
    $(SRCDIR)/ralph/tools/python_tool.c \
    $(SRCDIR)/ralph/tools/python_tool_files.c \
    $(SRCDIR)/ralph/tools/python_extension.c \
    $(SRCDIR)/ralph/tools/http_python.c \
    $(SRCDIR)/ralph/tools/verified_file_python.c \
    $(SRCDIR)/ralph/tools/sys_python.c

# LLM (in lib/llm/)
PROVIDER_SOURCES :=
MODEL_SOURCES :=

# Combined sources (ralph binary)
C_SOURCES := $(CORE_SOURCES) $(TOOL_SOURCES) \
    $(PROVIDER_SOURCES) $(MODEL_SOURCES)
CPP_SOURCES :=
SOURCES := $(C_SOURCES) $(CPP_SOURCES)
OBJECTS := $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)

# Scaffold binary sources
SCAFFOLD_SOURCES := $(SRCDIR)/scaffold/main.c
SCAFFOLD_OBJECTS := $(SCAFFOLD_SOURCES:.c=.o) $(TOOL_SOURCES:.c=.o)
