AC_INIT([hello-world], [1.0], [tim.gittos@example.com])
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])

# Cosmopolitan compiler doesn't support spaces in -D arguments
# Override the default PACKAGE_STRING to use underscores
AC_DEFINE([PACKAGE_STRING], ["hello-world_1.0"], [Define to the full name and version of this package.])
AC_PROG_CC
AC_CANONICAL_HOST

# Determine cosmopolitan ELF suffix for valgrind testing
case $host_cpu in
  x86_64)
    COSMO_ELF_SUFFIX=".dbg"
    ;;
  aarch64)
    COSMO_ELF_SUFFIX=".aarch64.elf"
    ;;
  *)
    COSMO_ELF_SUFFIX=".dbg"
    ;;
esac
AC_SUBST([COSMO_ELF_SUFFIX])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
 test/Makefile
])
AC_OUTPUT