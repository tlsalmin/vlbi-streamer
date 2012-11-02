bin_PROGRAMS = check_64_seq insert_64_seq scramble_64_seq scramble2_64_seq vdif_exam check_vdif_seq check_mark5b
check_64_seq_SOURCE = check_64_seq.c
insert_64_seq_SOURCE = insert_64_seq.c
scramble_64_seq_SOURCE = scramble_64_seq.c
scramble2_64_seq_SOURCE = scramble2_64_seq.c
vidf_exam_SOURCE= vdif_exam.c
AM_CPPFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DLOCALSTATEDIR='"$(localstatedir)"'
check_vdif_seq_SOURCE = common.h common.c check_vdif_seq.c 
check_vdif_seq_LDADD = $(ADD_LIBS) common.o
check_mark5b_SOURCE = common.h common.c check_mark5b.h check_mark5b.c
check_mark5b_LDADD = $(ADD_LIBS) common.o
