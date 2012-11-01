#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

: ${CXXFLAGS= -Werror }  
AC_PREREQ([2.63])
AC_INIT([seqsrc], [0.0.1], [tlsalmin@gmail.com])
AC_CONFIG_SRCDIR([mark5b.h])

# Checks for programs.
#AM_PATH_CHECK([0.9.8],[],[AC_MSG_ERROR([check is missing])])
AC_PROG_CC
#AC_PROG_LIBTOOL
#AM_PROG_CC_C_O

#Options

AC_SUBST(ADD_LIBS)
# Checks for header files.
AC_CHECK_HEADERS([malloc.h netdb.h stddef.h stdlib.h string.h unistd.h])

# Chec#ks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([ftruncate memset munmap strdup strerror])

#AM_INIT_AUTOMAKE
AM_INIT_AUTOMAKE([foreign parallel-tests color-tests])

#AC_CONFIG_FILES([Makefile])
AC_CONFIG_HEADER([config.h])
#AM_CONFIG_HEADER([utests/config.h])
AC_OUTPUT(Makefile)
