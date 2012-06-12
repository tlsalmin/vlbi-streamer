#!/bin/bash
if [ "$1" == "" ]; then 
  echo "Syntax: $0 <snapfile>"
  exit -1
fi

SNP=$1
I=1
#SNAMEVAR=scan_name

echo "Snap file: $SNP"

# get scan names, convert "," to space
sed -n -e '/scan_name=/s/scan_name=//p' $SNP | sed -n -e 's/,/ /gp' > ./scans

# get start times
# sed -n -e '/preob/,/!/p' $SNP | grep ! | sed -n -e 's/[!.]/ /gp' > ./starttimes
cat $SNP | grep disk_pos -1 | grep ! | sed -n -e 's/[!.]/ /gp' > ./starttimes

# get end times
cat $SNP | grep data_valid=off -1 | grep ! | sed -n -e 's/[!.]/ /gp' > ./endtimes

