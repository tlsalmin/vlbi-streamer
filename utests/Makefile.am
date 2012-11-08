#include_HEADERS = src/timekeeper.h
#INCLUDES                    = @CHECK_CFLAGS@
#if HAVE_CHECK
AUTOMAKE_OPTIONS = parallel-tests
TESTS = resourcetree
#else
#TESTS                       =
#endif

#noinst_PROGRAMS = $(TESTS)
check_PROGRAMS = $(TESTS)
#timekeeper_SOURCES = timekeeper_utest.c
#timekeeper_LDADD = $(ADD_LIBS)
#resourcetree_SOURCES = resourcetree_utest.c ../src/resourcetree.h ../src/resourcetree.c ../src/streamer.h
#resourcetree_SOURCES = resourcetree_utest.c $(top_builddir)/src/resourcetree.h $(top_builddir)/src/resourcetree.h
resourcetree_SOURCES = resourcetree_utest.c $(top_builddir)/src/resourcetree.h
resourcetree_LDADD = ../src/splicewriter.o ../src/aiowriter.o ../src/common_wrt.o ../src/defwriter.o ../src/udp_stream.o ../src/timer.o ../src/confighelper.o ../src/fanout.o ../src/simplebuffer.o ../src/resourcetree.o ../src/streamer.o ../src/common_filehandling.o $(ADD_LIBS) -lcheck
#resourcetree_CFLAGS = $(INCLUDES)
#resourcetree_LDADD = $(top_builddir)/src/libtests.la @CHECK_LIBS@
#resourcetree_LDADD = $(ADD_LIBS)
