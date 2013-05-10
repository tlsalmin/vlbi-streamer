bin_PROGRAMS = vlbistreamer groupsend sleeptest stream_multiplier splice_to_net morph_to_net
#vlbistreamer_SOURCES = fanout.h fanout.c common_wrt.h common_wrt.c udp_stream.c udp_stream.h streamer.c streamer.h ringbuf.c ringbuf.h defwriter.h defwriter.c sendfile_streamer.h sendfile_streamer.c splicewriter.h splicewriter.c
vlbistreamer_SOURCES = common_wrt.h common_wrt.c udp_stream.c udp_stream.h streamer.c streamer.h defwriter.h defwriter.c splicewriter.h splicewriter.c simplebuffer.h simplebuffer.c fanout.h fanout.c resourcetree.h resourcetree.c confighelper.h confighelper.c timer.h timer.c timekeeper.h timekeeper.c disk2file.c disk2file.h common_filehandling.h common_filehandling.c  writev_writer.c writev_writer.h active_file_index.c active_file_index.h datatypes.h datatypes.c dummywriter.h dummywriter.c dummy_stream.c dummy_stream.h datatypes_common.h datatypes_common.c logging.h sockethandling.h sockethandling.c
if DAEMON
  vlbistreamer_SOURCES += server.c server.h
endif
if HAVE_LIBAIO
  vlbistreamer_SOURCES += aiowriter.c aiowriter.h
endif
AM_CPPFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DLOCALSTATEDIR='"$(localstatedir)"'
vlbistreamer_LDADD = $(ADD_LIBS)
groupsend_SOURCES = groupsend.c
#groupsend_SOURCES += $(vlbistreamer_SOURCES)
groupsend_LDADD = $(ADD_LIBS)
sleeptest_SOURCES = timer.c timer.h sleeptest.c
sleeptest_LDADD = -lrt
stream_multiplier_SOURCES = stream_multiplier.c
stream_multiplier_LDADD = $(ADD_LIBS)
splice_to_net_SOURCES = splice_to_net.c
morph_to_net_SOURCES = morph_to_net.c
morph_to_net_LDADD = $(ADD_LIBS) timer.o
#sleeptest_LDFLAGS = -c
#disk2file_SOURCES = disk2file.c common_wrt.h common_wrt.c udp_stream.c udp_stream.h streamer.c streamer.h defwriter.h defwriter.c splicewriter.h splicewriter.c simplebuffer.h simplebuffer.c fanout.h fanout.c resourcetree.h resourcetree.c confighelper.h confighelper.c timer.h timer.c timekeeper.h timekeeper.c aiowriter.c aiowriter.h
#disk2file_CFLAGS = -DUSE_FOR_DISK2FILE
#disk2file_SOURCES = streamer.h streamer.c disk2file.c confighelper.c confighelper.h simplebuffer.h 
#disk2file_SOURCES = disk2file.c
#disk2file_SOURCES += vlbistreamer_SOURCES
#disk2file_LDADD = $(ADD_LIBS)

#vlbistreamer_CFLAGS = $(AM_CFLAGS)
#lib_LTLIBRARIES = libtests.la
#libtests_la_SOURCES = resourcetree.c resourcetree.h
