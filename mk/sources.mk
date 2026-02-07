# Source file definitions for ralph

# Core application sources
CORE_SOURCES := \
    $(SRCDIR)/core/main.c

# Tool system (Python tools only; core tools in lib/tools/)
TOOL_SOURCES := \
    $(SRCDIR)/tools/python_tool.c \
    $(SRCDIR)/tools/python_tool_files.c \
    $(SRCDIR)/tools/python_extension.c

# LLM (in lib/llm/)
PROVIDER_SOURCES :=
MODEL_SOURCES :=

# Combined sources
C_SOURCES := $(CORE_SOURCES) $(TOOL_SOURCES) \
    $(PROVIDER_SOURCES) $(MODEL_SOURCES)
CPP_SOURCES :=
SOURCES := $(C_SOURCES) $(CPP_SOURCES)
OBJECTS := $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o)
