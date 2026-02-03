# Library build configuration for libralph
# This file defines the library sources and build rules
# Note: LIBDIR is defined in config.mk

# Library output
LIBRALPH := $(BUILDDIR)/libralph.a

# =============================================================================
# LIBRARY SOURCES
# =============================================================================

# Phase 1: Initial structure with stubs
# These will be populated as modules are migrated from src/

# IPC module
LIB_IPC_SOURCES := \
    $(LIBDIR)/ipc/pipe_notifier.c \
    $(LIBDIR)/ipc/agent_identity.c \
    $(LIBDIR)/ipc/message_store.c \
    $(LIBDIR)/ipc/message_poller.c

# UI module (Phase 3)
LIB_UI_SOURCES := \
    $(LIBDIR)/ui/terminal.c \
    $(LIBDIR)/ui/spinner.c \
    $(LIBDIR)/ui/repl.c \
    $(LIBDIR)/ui/output_formatter.c \
    $(LIBDIR)/ui/json_output.c

# Tools module (Phase 4)
LIB_TOOLS_SOURCES :=

# Agent module (Phase 5)
LIB_AGENT_SOURCES := $(LIBDIR)/agent/agent.c

# LLM module (future)
LIB_LLM_SOURCES :=

# Services module (Phase 6 - Dependency Injection)
LIB_SERVICES_SOURCES := $(LIBDIR)/services/services.c

# Session module (future)
LIB_SESSION_SOURCES :=

# Policy module (future)
LIB_POLICY_SOURCES :=

# Util module (generic utilities)
LIB_UTIL_SOURCES := $(LIBDIR)/util/uuid_utils.c

# Database module
LIB_DB_SOURCES := $(LIBDIR)/db/sqlite_dal.c

# Workflow module (Phase 7)
LIB_WORKFLOW_SOURCES := $(LIBDIR)/workflow/workflow.c

# Combined library sources
LIB_C_SOURCES := $(LIB_IPC_SOURCES) $(LIB_UI_SOURCES) $(LIB_TOOLS_SOURCES) \
    $(LIB_AGENT_SOURCES) $(LIB_SERVICES_SOURCES) $(LIB_LLM_SOURCES) \
    $(LIB_SESSION_SOURCES) $(LIB_POLICY_SOURCES) $(LIB_DB_SOURCES) \
    $(LIB_WORKFLOW_SOURCES) $(LIB_UTIL_SOURCES)

LIB_CPP_SOURCES :=

LIB_SOURCES := $(LIB_C_SOURCES) $(LIB_CPP_SOURCES)
LIB_OBJECTS := $(LIB_C_SOURCES:.c=.o) $(LIB_CPP_SOURCES:.cpp=.o)
LIB_HEADERS := $(wildcard $(LIBDIR)/*.h) $(wildcard $(LIBDIR)/*/*.h)

# =============================================================================
# LIBRARY BUILD RULES
# =============================================================================

# Library include path (for src/ to use lib/)
LIB_INCLUDES := -I$(LIBDIR)

# Compile library C files
$(LIBDIR)/%.o: $(LIBDIR)/%.c $(LIB_HEADERS) | $(COMPILE_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) $(LIB_INCLUDES) -c $< -o $@

# Compile library C++ files
$(LIBDIR)/%.o: $(LIBDIR)/%.cpp $(LIB_HEADERS) | $(COMPILE_DEPS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LIB_INCLUDES) -c $< -o $@

# Build the library archive
# Note: Currently empty until Phase 2 adds sources
$(LIBRALPH): $(LIB_OBJECTS)
	@mkdir -p $(BUILDDIR)
	@if [ -n "$(LIB_OBJECTS)" ]; then \
		$(AR) rcs $@ $(LIB_OBJECTS); \
	else \
		echo "Note: libralph is empty (Phase 1 - structure only)"; \
		touch $@; \
	fi

# =============================================================================
# LIBRARY TARGETS
# =============================================================================

.PHONY: libralph libralph-clean

libralph: $(LIBRALPH)

libralph-clean:
	rm -f $(LIB_OBJECTS) $(LIBRALPH)
	find $(LIBDIR) -name "*.o" -delete 2>/dev/null || true

# =============================================================================
# INTEGRATION WITH MAIN BUILD
# =============================================================================

# Update INCLUDES to include lib/ for all compilation
INCLUDES += $(LIB_INCLUDES)
TEST_INCLUDES += $(LIB_INCLUDES)

# Clean target should also clean library
clean: libralph-clean
