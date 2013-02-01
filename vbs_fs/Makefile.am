if HAVE_FUSE
  bin_PROGRAMS = vbs_fs
  vbs_fs_SOURCES = vbs_fs.c
  vbs_fs_LDADD = $(FUSE_LIBS) $(FUSE_CFLAGS)
  AM_CPPFLAGS = $(FUSE_LIBS) $(FUSE_CFLAGS)
endif
