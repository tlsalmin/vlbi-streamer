AUTOMAKE_OPTIONS = parallel-tests
TESTS = resourcetree active_file_index datatypes integration

check_PROGRAMS = $(TESTS)
resourcetree_SOURCES = resourcetree_utest.c $(top_builddir)/src/resourcetree.h
resourcetree_LDADD = ../src/splicewriter.o ../src/common_wrt.o ../src/defwriter.o ../src/udp_stream.o ../src/timer.o ../src/confighelper.o ../src/fanout.o ../src/simplebuffer.o ../src/resourcetree.o ../src/streamer.o ../src/common_filehandling.o ../src/active_file_index.o ../src/disk2file.o ../src/writev_writer.o ../src/datatypes.o ../src/dummywriter.o ../src/dummy_stream.o $(ADD_LIBS) -lcheck
if HAVE_LIBAIO
  resourcetree_LDADD += ../src/aiowriter.o
endif
active_file_index_SOURCES = active_file_index_utest.c $(top_builddir)/src/active_file_index.h
active_file_index_LDADD = $(resourcetree_LDADD)
datatypes_SOURCES = datatypes_utest.c $(top_builddir)/src/datatypes.h
datatypes_LDADD = $(resourcetree_LDADD)
integration_SOURCES = integration_utest.c $(top_builddir)/src/streamer.h
integration_LDADD = $(resourcetree_LDADD)
