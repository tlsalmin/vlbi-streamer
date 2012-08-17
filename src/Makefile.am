bin_PROGRAMS = vlbistreamer
#vlbistreamer_SOURCES = fanout.h fanout.c common_wrt.h common_wrt.c udp_stream.c udp_stream.h streamer.c streamer.h ringbuf.c ringbuf.h defwriter.h defwriter.c sendfile_streamer.h sendfile_streamer.c splicewriter.h splicewriter.c
vlbistreamer_SOURCES = common_wrt.h common_wrt.c udp_stream.c udp_stream.h streamer.c streamer.h defwriter.h defwriter.c splicewriter.h splicewriter.c simplebuffer.h simplebuffer.c fanout.h fanout.c resourcetree.h resourcetree.c confighelper.h confighelper.c
if DAEMON
  vlbistreamer_SOURCES += server.c
endif
if HAVE_LIBAIO
  vlbistreamer_SOURCES += aiowriter.c aiowriter.h
endif
AM_CPPFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DLOCALSTATEDIR='"$(localstatedir)"'
vlbistreamer_LDADD = $(ADD_LIBS)
