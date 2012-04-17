#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([vlbi-streamer], [0.1], [tlsalmin@gmail.com])
AC_CONFIG_SRCDIR([src/fanout.h])

# Checks for programs.
AC_PROG_CC

#Options

AC_MSG_CHECKING(--enable-hugegpages argument)
AC_ARG_ENABLE(hugepages,
    [  --enable-hugepages         Enable support for hugepages.],
    [enable_hugepages=$enableval],
    [enable_hugepages="yes"])
AC_MSG_RESULT($enable_hugepages)
AS_IF([test "x$enable_hugepages" != "xno"],[
  AC_DEFINE(HAVE_HUGEPAGES, 1, [Compile with hugepage support])
  ])

# Checks for libraries.
ADD_LIBS=""
OK=1
AC_CHECK_LIB([pthread],[pthread_exit], ADD_LIBS="$ADD_LIBS -lpthread", [OK=0])
if test "$OK" = "0"; then
  AC_MSG_WARN([Could not find library pthread])
  AC_MSG_ERROR([Cannot continue])
fi

dnl Check for libaio and enable if found
AC_MSG_CHECKING(--enable-libaio argument)
AC_ARG_ENABLE(libaio,
    [  --enable-libaio         Include libaio backend.],
    [enable_libaio=$enableval],
    [enable_libaio="yes"])
AC_MSG_RESULT($enable_libaio)
AS_IF([test "x$enable_libaio" != "xno"], [
       AC_CHECK_LIB([aio],[io_submit], [
		     ADD_LIBS="$ADD_LIBS -laio"
		     AC_DEFINE(HAVE_LIBAIO, 1, [Compile with added libaio backend])
		     ], [
			 AC_MSG_WARN([Could not find library libaio])
			 AC_MSG_WARN([libaio writing backend disabled])
			 enable_libaio="no"
			 ])
])
#AC_DEFINE([HAVE_LIBAIO], 0,[hur])
#AC_MSG_WARN([hur dis $HAVE_LIBAIO ur])
dnl Its a bit silly to define em twice..
AM_CONDITIONAL([HAVE_LIBAIO], [test "${enable_libaio}" = "yes"])

AC_MSG_CHECKING([--enable-debug argument])
AC_ARG_ENABLE(debug,
    [  --enable-debug         Enable debug output.],
    [enable_debug=$enableval],
    [enable_debug="no"])
AC_MSG_RESULT($enable_debug)
AS_IF([test "x$enable_debug" != "xno"], [
		     AC_DEFINE(DEBUG_OUTPUT, 1, [Print debug_output to stdout])
		     ])
AM_CONDITIONAL([DEBUG_OUTPUT], [test "${enable_debug}" = "yes"])

AC_MSG_CHECKING([--enable-multisenddebug argument])
AC_ARG_ENABLE(multisenddebug,
    [  --enable-multisenddebug         Enable multisend debug output.],
    [enable_multisenddebug=$enableval],
    [enable_multisenddebug="no"])
AC_MSG_RESULT($enable_multisenddebug)
AS_IF([test "x$enable_multisenddebug" != "xno"], [
		     AC_DEFINE(MULTITHREAD_SEND_DEBUG, 1, [Print multisenddebug_output to stdout])
		     ])
AM_CONDITIONAL([MULTITHREAD_SEND_DEBUG], [test "${enable_multisenddebug}" = "yes"])

AC_MSG_CHECKING([--enable-ratelimiter argument])
AC_ARG_ENABLE(ratelimiter,
    [  --enable-ratelimiter         Enable rate limiting in sending.],
    [enable_ratelimiter=$enableval],
    [enable_ratelimiter="yes"])
AC_MSG_RESULT($enable_ratelimiter)
AS_IF([test "x$enable_ratelimiter" != "xno"], [
       AC_CHECK_LIB([rt],[clock_gettime],[
		     ADD_LIBS="$ADD_LIBS -lrt"
		     AC_DEFINE(HAVE_RATELIMITER, 1, [Print ratelimiter_output to stdout])
		     ],[
			AC_MSG_WARN([Couldnt find library rt])
			AC_MSG_WARN([rate limiter disabled])
			])
       ])
AM_CONDITIONAL([HAVE_RATELIMITER], [test "${enable_ratelimiter}" = "yes"])
dnl Push the libs
AC_SUBST(ADD_LIBS)
# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h malloc.h netdb.h netinet/in.h stddef.h stdlib.h string.h sys/ioctl.h sys/socket.h unistd.h])


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
AC_CONFIG_HEADER([src/config.h])
AC_OUTPUT(Makefile src/Makefile)
