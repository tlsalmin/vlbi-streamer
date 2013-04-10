#ifndef SOCKETHANDLING_H
#define SOCKETHANDLING_H
int bind_port(struct addrinfo* si, int fd, int readmode, int do_connect){
  int err=0;
  struct addrinfo *p;

  if(readmode == 0)
  {
    /* TODO: this needs to be done earlier */
    for(p = si; p != NULL; p = p->ai_next)
    {
      err = bind(fd, p->ai_addr, p->ai_addrlen);
      if(err != 0)
      {
	E("bind socket");
	//close(sockfd);
	continue;
      }
      break;
    }
    if(err != 0){
      E("Cant bind");
      return -1;
    }
  }
  else if(do_connect == 1)
  {
    for(p = si; p != NULL; p = p->ai_next)
    {
      err = connect(fd, p->ai_addr, p->ai_addrlen);
      if(err != 0)
      {
	E("connect socket");
	//close(sockfd);
	continue;
      }
      break;
    }
    if(err != 0){
      E("Cant connect");
      return -1;
    }
  }

  return 0;
}
int create_socket(int *fd, char * port, struct addrinfo ** servinfo, char * hostname, int socktype, struct addrinfo ** used)
{
  int err;
  struct addrinfo hints, *p;
  memset(&hints, 0, sizeof(struct addrinfo));
  /* Great ipv6 guide http://beej.us/guide/bgnet/					*/
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;
  hints.ai_flags = AI_PASSIVE;
  /* Port as integer is legacy from before I saw the light from Beej network guide	*/
  err = getaddrinfo(hostname, port, &hints, servinfo);
  if(err != 0){
    E("Error in getting address info %s",, gai_strerror(err));
    return -1;
  }
  for(p = *servinfo; p != NULL; p = p->ai_next)
  {
    *fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    D("Socket initialized");
    if(*fd < 0 ){
      E("Cant bind to %s. Trying next",, p->ai_canonname);
      continue;
    }
    if(used != NULL)
      *used = p;
    break;
  }
  if(*fd < 0){
    E("Couldn't get socket at all. Exiting as failed");
    return -1;
  }
  return 0;
}

#endif
