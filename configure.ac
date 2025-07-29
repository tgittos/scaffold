AC_INIT([hello-world], [1.0], [tim.gittos@example.com])
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])

# Cosmopolitan compiler doesn't support spaces in -D arguments
# This is handled in Makefile.am with subst
AC_PROG_CC
AC_CANONICAL_HOST

# Check for required tools to build libcurl
AC_CHECK_PROGS([WGET], [wget curl], [])
if test -z "$WGET"; then
  AC_MSG_ERROR([wget or curl is required to download libcurl])
fi

AC_CHECK_PROG([TAR], [tar], [tar], [])
if test -z "$TAR"; then
  AC_MSG_ERROR([tar is required to extract libcurl])
fi

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