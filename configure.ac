#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([vlbi-streamer], [0.1], [tlsalmin@gmail.com])
AC_CONFIG_SRCDIR([src/fanout.h])
AC_CONFIG_HEADER([src/config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
ADD_LIBS=""
OK=1
AC_CHECK_LIB([pthread],[pthread_exit], ADD_LIBS="$ADD_LIBS -lpthread", [OK=0])
if test "$OK" = "0"; then
  AC_MSG_WARN([Could not find library pthread])
  AC_MSG_ERROR([Cannot continue])
fi
OK=1
AC_DEFINE([HAVE_LIBAIO], 1, [Include libaio use in streamer])
AC_CHECK_LIB([aio],[io_submit], ADD_LIBS="$ADD_LIBS -laio", [OK=0])
if test "$OK" = "0"; then
  AC_MSG_WARN([Could not find library libaio])
  AC_MSG_WARN([libaio writing backend support])
  AC_MSG_WARN([wont be included])
  AC_DEFINE([HAVE_LIBAIO], 0, [Include libaio use in streamer])
fi
AC_SUBST(ADD_LIBS)
# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h malloc.h netdb.h netinet/in.h stddef.h stdlib.h string.h sys/ioctl.h sys/socket.h unistd.h])

#Options
AC_MSG_CHECKING(--enable-libaio argument)
AC_ARG_ENABLE(libaio,
    [  --enable-libaio         Include libaio backend.],
    [enable_libaio=$enableval],
    [enable_libaio="no"])
AC_MSG_RESULT($enable_libaio)
if test "$enable_libaio" = "yes"; then
  AC_DEFINE(HAVE_LIBAIO)
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([ftruncate gethostbyname memset munmap socket strdup strerror])

AM_INIT_AUTOMAKE

#AC_CONFIG_FILES([Makefile])
AC_OUTPUT(Makefile src/Makefile)
