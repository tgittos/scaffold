check_PROGRAMS = test_main
test_main_SOURCES = test_main.c unity/unity.c
test_main_CPPFLAGS = -I$(srcdir)/unity

# Override DEFS to replace spaces with underscores for cosmopolitan compatibility  
override DEFS := $(subst hello-world\ 1.0,hello-world_1.0,$(DEFS))

debug-defs:
	@echo "DEFS = $(DEFS)"

TESTS = $(check_PROGRAMS)

# Clean all cosmopolitan build artifacts
CLEANFILES = test_main.aarch64.elf test_main.com.dbg test_main.dbg

clean-local:
	rm -rf .aarch64 .deps

# Valgrind targets - use platform-specific cosmopolitan ELF binary
VALGRIND = valgrind
VALGRIND_FLAGS = --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1

check-valgrind: $(check_PROGRAMS)
	$(VALGRIND) $(VALGRIND_FLAGS) ./test_main@COSMO_ELF_SUFFIX@

.PHONY: check-valgrind