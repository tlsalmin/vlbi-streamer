bin_PROGRAMS = insert_64_seq scramble_64_seq scramble2_64_seq binarysearchmatch check_bufalignment vbs_metadata
#check_64_seq_SOURCES = check_64_seq.c
insert_64_seq_SOURCES = insert_64_seq.c
scramble_64_seq_SOURCES = scramble_64_seq.c
scramble2_64_seq_SOURCES = scramble2_64_seq.c
vbs_metadata_SOURCES = vbs_metadata.c vbs_metadata.h ../src/datatypes_common.h ../src/datatypes_common.c
vbs_metadata_LDADD = $(ADD_LIBS) #../src/datatypes_common.o
check_bufalignment_SOURCES = check_bufalignment.c
#disk2file_SOURCES = disk2file.c common.h common.c
#vidf_exam_SOURCES= vdif_exam.c
AM_CPPFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DLOCALSTATEDIR='"$(localstatedir)"'
#check_vdif_seq_SOURCES = common.c common.h check_vdif_seq.c
#check_vdif_seq_LDADD = $(ADD_LIBS) 
#check_mark5b_SOURCES = common.h common.c  check_mark5b.c
binarysearchmatch_SOURCES = binarysearchmatch.c ../src/datatypes_common.h ../src/datatypes_common.c
binarysearchmatch_LDADD = $(ADD_LIBS)
#check_mark5b_LDADD = $(ADD_LIBS) ../src/datatypes_common.o
