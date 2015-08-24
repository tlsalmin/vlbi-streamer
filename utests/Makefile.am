AUTOMAKE_OPTIONS = parallel-tests
TESTS = test_afi test_resourcetree test_datatypes
#Integration tests disabled for now.

check_PROGRAMS = $(TESTS)
test_afi_LDADD = -lcheck -lpthread
test_resourcetree_LDADD = -lcheck -lpthread ../src/splicewriter.o ../src/common_wrt.o ../src/defwriter.o ../src/udp_stream.o ../src/timer.o ../src/confighelper.o ../src/fanout.o ../src/simplebuffer.o ../src/resourcetree.o ../src/streamer.o ../src/common_filehandling.o ../src/active_file_index.o ../src/disk2file.o ../src/writev_writer.o ../src/datatypes.o ../src/dummywriter.o ../src/dummy_stream.o ../src/datatypes_common.o ../src/sockethandling.o ../src/tcp_stream.o ../src/sendfile_writer.o  $(ADD_LIBS) -lcheck
if HAVE_LIBAIO
  test_resourcetree_LDADD += ../src/aiowriter.o
endif
test_datatypes_LDADD = -lcheck -lm ../src/datatypes.o ../src/datatypes_common.o ../src/active_file_index.o -lpthread
