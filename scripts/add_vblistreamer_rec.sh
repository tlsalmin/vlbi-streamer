#!/bin/bash
if [ "$1" == "" ]; then 
  echo "Syntax: $0 <snapfile>"
fi

SNP=$1
I=1
#SNAMEVAR=scan_name

echo "Snap file: $SNP"
LASTLINE=""
SCANNAME=""
LASTTIME=""
TIMENOW=""

while read line; do
  LINESTART=`echo $line|awk '{split($0,a,"="); print a[1]}'`
  RECNAME=`echo $line|awk '{split($0,a,"="); print a[2]}'`
  if `echo $LINESTART|grep scan_name 1>/dev/null 2>&1`
  then
    #We found a new scan!
    SCANNAME=$RECNAME
    echo $RECNAME
  elif `echo $LINESTART |grep disk_pos 1>/dev/null 2>&1` 
  then
    LASTTIME=$LASTLINE
    echo $LASTLINE
  fi
  I=$(($I+1))
  LASTLINE=$line
done < $SNP
