bin_PROGRAMS = check_64_seq insert_64_seq scramble_64_seq scramble2_64_seq vdif_exam check_vdif_seq check_mark5b binarysearchmatch
check_64_seq_SOURCES = check_64_seq.c
insert_64_seq_SOURCES = insert_64_seq.c
scramble_64_seq_SOURCES = scramble_64_seq.c
scramble2_64_seq_SOURCES = scramble2_64_seq.c
#disk2file_SOURCES = disk2file.c common.h common.c
#vidf_exam_SOURCES= vdif_exam.c
AM_CPPFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DLOCALSTATEDIR='"$(localstatedir)"'
check_vdif_seq_SOURCES = common.c common.h check_vdif_seq.c
check_vdif_seq_LDADD = $(ADD_LIBS)
check_mark5b_SOURCES = common.h common.c  check_mark5b.c
binarysearchmatch_SOURCES = common.h common.c binarysearchmatch.c
binarysearchmatch_LDADD = $(ADD_LIBS)
