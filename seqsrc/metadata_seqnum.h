#ifndef METADATA_SEQNUM_H
#define METADATA_SEQNUM_H

struct metadata_seqnum
{
  uint64_t count;
  uint64_t init_count;
  uint64_t clean_count;
  int	wordsize;
};

int seqnum_preinit_wordsize(struct common_control_element *cce, int wordsize)
{
  struct metadata_seqnum* ms;
  cce->datatype_metadata = malloc(sizeof(struct metadata_seqnum));
  memset(cce->datatype_metadata, 0, sizeof(struct metadata_seqnum));
  ms = cce->datatype_metadata;
  ms->wordsize = wordsize;
  if((ms->wordsize <= 0)|| (ms->wordsize > 16)){
    E("Illegal framesize %d",, ms->wordsize);
    return -1;
  }
  return 0;
}
int seqnum_check(struct common_control_element *cce)
{
  struct metadata_seqnum * ms = cce->datatype_metadata;
  if(ms->count == 0){
    ms->init_count = ms->count = get_a_count(cce->buffer, ms->wordsize, cce->offset, BITUP(cce->optbits, CHANGE_ENDIANESS));
    LOG("First count as %ld\n", ms->count);
  }
  else
  {
    uint64_t newseq = get_a_count(cce->buffer, ms->wordsize, cce->offset, BITUP(cce->optbits, CHANGE_ENDIANESS));
    if(newseq != ms->count){
      LOG("Discrepancy as read %ld and count is %ld. Clean packets inbetween %ld\n", newseq, ms->count, ms->clean_count);
      ms->clean_count = 0;
    }
    else
      ms->clean_count++;
  }
  return 0;
}
void seqnum_metadata_increment(struct common_control_element * cce, long count)
{
  if(count == 0)
    ((struct metadata_seqnum*)cce->datatype_metadata)->count=((struct metadata_seqnum*)cce->datatype_metadata)->init_count;
  else
    ((struct metadata_seqnum*)cce->datatype_metadata)->count+=count;
}
void seqnum_clean(struct common_control_element *cce)
{
  free(cce->datatype_metadata);
}
int init_seqnum_data(struct common_control_element *cce)
{
  struct metadata_seqnum* ms;
  if(cce->framesize <= 0|| cce->framesize > MAX_FRAMESIZE){
    E("Illegal framesize %d",, cce->framesize);
    return -1;
  }
  if(cce->datatype_metadata == NULL){
    cce->datatype_metadata = malloc(sizeof(struct metadata_seqnum));
    memset(cce->datatype_metadata, 0, sizeof(struct metadata_seqnum));
    ms = cce->datatype_metadata;
  }
  if(ms->wordsize == 0)
    ms->wordsize = 8;
  cce->optbits |= CHANGE_ENDIANESS;
  /* No real info in udpmon data so lest make it the same as check_seqnum	*/
  cce->print_info = cce->check_for_discrepancy = seqnum_check;
  cce->cleanup_inspector = seqnum_clean;
  cce->metadata_increment = seqnum_metadata_increment;

  return 0;
}
/*
void usage(){
    LOG("Usage: check_64_seq <filename/socket> <byte spacing> [OPTIONS]\n");
    LOG("OPTIONS:\n \
	-t 		for tcp-socket\n \
	-u 		for tcp-socket\n \
	-w <wordsize>	for definining wordsize(default 8 bytes). Allowed 4 or 8\n\
	-o <length>	offset each read by length bytes\n\
	-b 		change endianess after read\n");

  exit(-1);
}


int main(int argc, char** argv){
  int fd,sockfd=0,err;
  int opts =0;
  int64_t count = 0;
  int succesfull_socket=0;
  long spacing=0;
  int (*read_operator)(int, void*, int);
  int64_t read_count=0;
  int readoffset=0;
  int wordsize=8;
  int wordsizediff = 0;
  struct stat st;
  char* filename = NULL;
  void * buf;
  int bflag=0;
  char c;
  read_operator = do_read_file;
  while ((c = getopt (argc, argv, "w:bo:tu")) != -1)
    switch (c)
    {
      case 'w':
	wordsize = atoi(optarg);
	if(wordsize != 4 && wordsize != 8)
	{
	  usage();
	}
	break;
      case 'o':
	readoffset= atoi(optarg);
	break;
      case 't':
	opts |= ITS_A_TCP_SOCKET;
	read_operator = do_read_socket;
	break;
      case 'u':
	opts |= ITS_A_UDP_SOCKET;
	read_operator = do_read_udpsocket;
	break;
      case 'b':
	bflag = 1;
	break;
      case '?':
	if (optopt == 'w' || optopt == 'o')
	  fprintf (stderr, "Option -%c requires an argument.\n", optopt);
	else if (isprint (optopt)){
	  fprintf (stderr, "Unknown option `-%c'.\n", optopt);
	  usage(argv);
	}
	else{
	  fprintf (stderr,
	      "Unknown option character `\\x%x'.\n",
	      optopt);
	  usage(argv);
	}
      default:{
		abort ();
	      }
    }
  wordsizediff = 8 - wordsize;

  argc-=optind;
  argv+=optind;
  if(argc != 2)
    usage(argv);

  filename = argv[0];
  if(opts & ITS_A_TCP_SOCKET)
    LOG("Socket");
  else
    LOG("Filename");
  LOG(" is %s\n", filename);

  spacing = atol(argv[1]);
  LOG("Spacing is %ld\n", spacing);

  if(filename == NULL || spacing == 0){
    fprintf(stderr,"No socket/filename or no spacing\n");
    return -1;
  }

  if(opts & (ITS_A_TCP_SOCKET|ITS_A_UDP_SOCKET))
  {
  }
  else
  {
    if(stat(filename, &st) != 0){
      E("error in stat");
      exit(-1);
    }
    fd = open(filename, O_RDONLY);
    if(fd == -1){
      E("Error opening file %s",, filename);
      exit(-1);
    }
  }

  if(opts & ITS_A_TCP_SOCKET && succesfull_socket == 0){
    E("Didnt get a socket");
    exit(-1);
  }

  buf = malloc(spacing);
  if(buf == 0)
  {
    E("Error in buffer malloc");
    exit(-1);
  }

  err = read_operator(fd, buf, spacing);
  if(err< 0){
    E("Error in read");
    exit(-1);
  }

  memcpy((((void*)&count)+wordsizediff), buf+readoffset, wordsize);

  if(bflag == 0)
  {
    if(wordsize == 4)
      count = be32toh(*(((int32_t*)(&count))+1));
    else
      count = be64toh(count);
  }
  LOG("first count is %ld\n", count);
  count++;

  do
  {
    //for(i=1;i*spacing < fsize;i++){

    err = read_operator(fd,buf,spacing);
    if(err < 0){
      if(err == -INT_MAX)
	LOG("Orderly shutdown\n");
      else
	E("Error in read operation");
      break;
    }
    //lseek(fd, i*spacing+readoffset, SEEK_SET);

    memcpy((((void*)&read_count)+wordsizediff), buf+readoffset, wordsize);
    //memcpy(&read_count, buf+readoffset, wordsize);

    if(bflag==0){
      if(wordsize == 4)
	read_count = be32toh(*(((int32_t*)(&read_count))+1));
      else
	read_count = be64toh(read_count);
    }
    if(count != read_count){
      fprintf(stdout, "Discrepancy as count is %ld and read_count is %ld\n",count, read_count);
      //count = read_count;
    }
    count++;
  }
  while(err == 0);
    fprintf(stdout, "Done! Final count was %ld\n", count);

    if(opts & ITS_A_TCP_SOCKET){
      shutdown(fd, SHUT_RD);
      shutdown(sockfd, SHUT_RDWR);
      close(sockfd);
    }

    free(buf);

    if(close(fd) != 0){
      E("Error on close");
      exit(-1);
    }

    exit(0);
  }
*/
#endif
