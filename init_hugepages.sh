#!/bin/bash
#Number of pages in KB
HUGESIZE=2048
#Buffer size in GB
BUFFERSIZE=4
G=G
NR_HUGE=$(((BUFFERSIZE*1024*1024)/HUGESIZE))
if [ -z "$1" ]
then
  echo "Please give streamer uid as parameters"
  exit 0
fi
UID=$(id -u $1)
echo "Setting and creating $NR_HUGE of $HUGESIZE KB pages"
echo $NR_HUGE > /proc/sys/vm/nr_hugepages
mkdir /mnt/huge
$(mount -t hugetlbfs -o uid=$UID,mode=0755,size=$BUFFERSIZE$G none /mnt/huge)
