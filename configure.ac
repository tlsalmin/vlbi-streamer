#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

: ${CXXFLAGS= -Werror }  
AC_PREREQ([2.63])
AC_INIT([vlbi-streamer], [0.6], [tlsalmin@gmail.com])
AC_CONFIG_SRCDIR([src/fanout.h])

# Checks for programs.
#AM_PATH_CHECK([0.9.8],[],[AC_MSG_ERROR([check is missing])])
AC_PROG_CC
#AC_PROG_LIBTOOL
#AM_PROG_CC_C_O

#Options

AC_DEFINE(PROTECT_STATUS_W_RWLOCK, 1, [rwlock on status])

AC_MSG_CHECKING(--enable-hugepages argument)
AC_ARG_ENABLE(hugepages,
    [  --enable-hugepages         Enable support for hugepages.],
    [enable_hugepages=$enableval],
    [enable_hugepages="yes"])
AC_MSG_RESULT($enable_hugepages)
AS_IF([test "x$enable_hugepages" != "xno"],[
  AC_DEFINE(HAVE_HUGEPAGES, 1, [Compile with hugepage support])
  ])

AC_MSG_CHECKING(--enable-localsocket argument)
AC_ARG_ENABLE(localsocket,
    [  --enable-localsocket         Enable support for localsocket.],
    [enable_localsocket=$enableval],
    [enable_localsocket="yes"])
AC_MSG_RESULT($enable_localsocket)
AS_IF([test "x$enable_localsocket" != "xno"],[
  AC_DEFINE(HAVE_LOCALSOCKET, 1, [Compile with localsocket support])
  ])

# Checks for libraries.
ADD_LIBS=""
OK=1
AC_CHECK_LIB([pthread],[pthread_exit], ADD_LIBS="$ADD_LIBS -lpthread", [OK=0])
if test "$OK" = "0"; then
  AC_MSG_WARN([Could not find library pthread])
  AC_MSG_ERROR([Cannot continue])
fi

OK=1
AC_CHECK_LIB([config],[config_init], ADD_LIBS="$ADD_LIBS -lconfig", [OK=0])
if test "$OK" = "0"; then
  AC_MSG_WARN([Could not find library libconfig])
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
AM_CONDITIONAL([HAVE_LIBAIO], [test "${enable_libaio}" = "yes"])

AC_MSG_CHECKING(--enable-libfuse argument)
AC_ARG_ENABLE(libfuse,
    [  --enable-libfuse         Include fuse fs.],
    [enable_libfuse=$enableval],
    [enable_libfuse="yes"])
AC_MSG_RESULT($enable_libfuse)
AS_IF([test "x$enable_libfuse" != "xno"], [
       AC_CHECK_LIB([fuse],[fuse_main], [
	      FUSE_LIBS=`pkg-config fuse --libs`
	      FUSE_CFLAGS=`pkg-config fuse --cflags`
	      AC_SUBST([FUSE_LIBS])
	      AC_SUBST([FUSE_CFLAGS])
	      AC_DEFINE(HAVE_LIBFUSE, 1, [Compile with added libaio backend])
		     ], [
			 AC_MSG_WARN([Could not find library fuse])
			 AC_MSG_WARN([fuse functionality disabled])
			 enable_libfuse="no"
			 ])
])
dnl Its a bit silly to define em twice..
AM_CONDITIONAL([HAVE_FUSE], [test "${enable_libfuse}" = "yes"])

AC_MSG_CHECKING(--enable-gprofile argument)
AC_ARG_ENABLE(gprofile,
    [  --enable-gprofile         Include gprofile linking.],
    [enable_gprofile=$enableval],
    [enable_gprofile="no"])
AC_MSG_RESULT($enable_gprofile)
AS_IF([test "x$enable_gprofile" != "xno"], [
       ADD_LIBS="$ADD_LIBS -lprofiler"
       AC_DEFINE(HAVE_GPROFILE, 1, [Compile with added gprofile linking])
])
dnl Its a bit silly to define em twice..
AM_CONDITIONAL([HAVE_GPROFILE], [test "${enable_gprofile}" = "yes"])

AC_MSG_CHECKING([--enable-debug argument])
AC_ARG_ENABLE(debug,
    [  --enable-debug         Enable debug output.],
    [enable_debug=$enableval],
    [enable_debug="no"])
AC_MSG_RESULT($enable_debug)
AS_IF([test "x$enable_debug" != "xno"], [
		     AC_DEFINE(DEBUG_OUTPUT, 1, [Print debug_output to stdout])
		     ], AC_DEFINE(DEBUG_OUTPUT, 0, [Print debug_output to stdout]))
AM_CONDITIONAL([DEBUG_OUTPUT], [test "${enable_debug}" = "yes"])

AC_MSG_CHECKING([--enable-preemptkernel argument])
AC_ARG_ENABLE(preemptkernel,
    [  --enable-preemptkernel         Enable pre-empt kernel sparing a busyloop on rated sending.],
    [enable_preemptkernel=$enableval],
    [enable_preemptkernel="no"])
AC_MSG_RESULT($enable_preemptkernel)
AS_IF([test "x$enable_preemptkernel" != "xno"], [
		     AC_DEFINE(PREEMPTKERNEL, 1, [Use nanosleep])
		     ], AC_DEFINE(PREEMPTKERNEL, 0, [use busyloop]))
AM_CONDITIONAL([PREEMPTKERNEL], [test "${enable_preemptkernel}" = "yes"])

AC_MSG_CHECKING([--enable-logtofile argument])
AC_ARG_ENABLE(logtofile,
	      [  --enable-logtofile         Enable logging to file.],
    [enable_logtofile=$enableval],
    [enable_logtofile="yes"])
AC_MSG_RESULT($enable_logtofile)
AS_IF([test "x$enable_logtofile" != "xno"], [
       AC_DEFINE(LOG_TO_FILE, 1, [log to file ])
       ], AC_DEFINE(LOG_TO_FILE, 0, [log to file]))
AM_CONDITIONAL([LOG_TO_FILE], [test "${enable_logtofile}" = "yes"])

AC_MSG_CHECKING([--enable-daemon argument])
AC_ARG_ENABLE(daemon,
    [  --enable-daemon         Compile software as daemon(Default: yes).],
    [enable_daemon=$enableval],
    [enable_daemon="yes"])
AC_MSG_RESULT($enable_daemon)
AS_IF([test "x$enable_daemon" != "xno"], [
		     AC_DEFINE(DAEMON, 1, [Compile as daemon])
		     ], AC_DEFINE(DAEMON, 0, [Compile as daemon]))
AM_CONDITIONAL([DAEMON], [test "${enable_daemon}" = "yes"])

AC_MSG_CHECKING([--enable-spinlock argument])
AC_ARG_ENABLE(spinlock,
    [  --enable-spinlock         Use spinlock in resourcetree(default: No).],
    [enable_spinlock=$enableval],
    [enable_spinlock="no"])
AC_MSG_RESULT($enable_spinlock)
AS_IF([test "x$enable_spinlock" != "xno"], [
		     AC_DEFINE(SPINLOCK, 1, [Compile with spinlock])
		     ], AC_DEFINE(SPINLOCK, 0, [Compile without spinlock]))

AC_MSG_CHECKING([--enable-ppriority argument])
AC_ARG_ENABLE(ppriority,
    [  --enable-ppriority         Enable pthread priority setting (default: Yes).],
    [enable_ppriority=$enableval],
    [enable_ppriority="yes"])
AC_MSG_RESULT($enable_ppriority)
AS_IF([test "x$enable_ppriority" != "xno"], [
		     AC_DEFINE(PPRIORITY, 1, [Compile with pthread priority])
		     ], AC_DEFINE(PPRIORITY, 0, [Compile without pthread priority]))
PPRIORITY=$enable_ppriority
AC_SUBST(PPRIORITY)


AC_MSG_CHECKING([--enable-senddebug argument])
AC_ARG_ENABLE(senddebug,
    [  --enable-senddebug         Enable senddebug output.],
    [enable_senddebug=$enableval],
    [enable_senddebug="no"])
AC_MSG_RESULT($enable_senddebug)
AS_IF([test "x$enable_senddebug" != "xno"], [
		     AC_DEFINE(SEND_DEBUG, 1, [Print senddebug_output to stdout])
		     ], AC_DEFINE(SEND_DEBUG, 0, [Print senddebug_output to stdout]))
AM_CONDITIONAL([SEND_DEBUG], [test "${enable_senddebug}" = "yes"])

AC_MSG_CHECKING([--enable-writegranuality argument])
AC_ARG_ENABLE(writegranuality,
    [  --enable-writegranuality         Enable write granuality (default: yes) Recommended no on large raids.],
    [enable_writegranuality=$enableval],
    [enable_writegranuality="yes"])
AC_MSG_RESULT($enable_writegranuality)
AS_IF([test "x$enable_writegranuality" != "xno"], [
		     AC_DEFINE(WRITE_GRANUALITY, 1, [Use granuality in write])
		     ], AC_DEFINE(WRITE_GRANUALITY, 0, [Use granuality in write]))
AM_CONDITIONAL([WRITE_GRANUALITY], [test "${enable_writegranuality}" = "yes"])

AC_MSG_CHECKING([--user argument])
AC_ARG_ENABLE(user,
    [  --enable-user         Set the user.(default oper)],
    [user=$enableval],
    [user="oper"])
AC_MSG_RESULT($user)
USER=$user
AC_SUBST(USER)

AC_MSG_CHECKING([--group argument])
AC_ARG_ENABLE(group,
    [  --enable-group         Set the group.(default flexbuf)],
    [group=$enableval],
    [group=$user])
AC_MSG_RESULT($group)
GROUP=$group
AC_SUBST(GROUP)

#AM_PATH_CHECK(,[have_check="yes"],
 #AC_MSG_WARN([Check not found; cannot run unit tests])
 #[have_check="no"])
#AM_CONDITIONAL(HAVE_CHECK, test x"$have_check" = "xyes")
#PKG_CHECK_MODULES(CHECK, [check >= 0.9.8],,[AC_MSG_RESULT([no, testing is disabled])]) 
#AM_CONDITIONAL(HAVE_CHECK, [test "x$CHECK_LIBS" != "x"])

AC_SUBST(CHECK_CFLAGS)
AC_SUBST(CHECK_LIBS)

AC_ARG_VAR([ROOTDIRS], [Prefix for disks organized as prefix<n>.
                         Default = "/mnt/disk" >= disks are /mnt/disk0..n])
AS_IF([test "x$ROOTDIRS" = x], [ROOTDIRS="/mnt/disk"])
AC_DEFINE_UNQUOTED(ROOTDIRS, "${ROOTDIRS}", [Location for disks])
AC_SUBST(ROOTDIRS)

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

dnl TODO: Double check below
AC_MSG_CHECKING([--enable-ratelimiter argument])
AC_ARG_ENABLE(ratelimiter,
    [  --enable-ratelimiter         Enable rate limiting in sending.],
    [enable_ratelimiter=$enableval],
    [enable_ratelimiter="yes"])
AC_MSG_RESULT($enable_ratelimiter)
AS_IF([test "x$enable_ratelimiter" != "xno"], [
       AC_CHECK_LIB([rt],[clock_gettime],[
       AC_DEFINE(HAVE_RATELIMITER, 1, [Print ratelimiter_output to stdout])
		     ],[
			AC_MSG_WARN([Couldnt find library rt])
			AC_MSG_WARN([Ratelimiter disabled])
			])
       ])
AC_CHECK_LIB([rt],[clock_gettime],[
	      ADD_LIBS="$ADD_LIBS -lrt"
	      AC_DEFINE(HAVE_LRT, 1, [Measure time properly])
	      ],[
	      AC_MSG_WARN([Couldnt find library rt])
	      AC_MSG_WARN([Time measurement disabled])
	      ])
dnl Push the libs
AC_SUBST(ADD_LIBS)
# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h malloc.h netdb.h netinet/in.h stddef.h stdlib.h string.h sys/ioctl.h sys/socket.h unistd.h libconfig.h])

# Chec#ks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([ftruncate gethostbyname memset munmap socket strdup strerror])

#AM_INIT_AUTOMAKE
AM_INIT_AUTOMAKE([foreign parallel-tests color-tests])

#AC_CONFIG_FILES([Makefile])
AC_CONFIG_HEADER([src/config.h])
#AM_CONFIG_HEADER([utests/config.h])
AC_OUTPUT(Makefile src/Makefile seqsrc/Makefile utests/Makefile vbs_fs/Makefile man/Makefile)
