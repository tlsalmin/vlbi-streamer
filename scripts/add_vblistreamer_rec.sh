#!/bin/bash
if [ "$1" == "" ]; then 
  echo "Syntax: $0 <snapfile>"
  exit -1
fi

VLBISTREAMER=ara.metsahovi.fi
VLBISTART=vlbistreamer
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
#cat $SNP | grep data_valid=off -1 | grep ! | sed -n -e 's/[!.]/ /gp'|sed -n -e 's/[:.]/ /gp' > ./endtimes

#echo $SNP|sed 's/\(.*\)\.snp/\1vbs\.snp/'
NEWFILE=`echo $SNP|sed 's/\(.*\)\.snp/\1vbs\.snp/'`
rm $NEWFILE
cp $SNP $NEWFILE

I=1
LC=`wc -l scans | awk '{print $1}'`
while [ $I -le $LC ]
do
  RARRAY=(`tail -n +$I ./scans | head -1`)
  SARRAY=(`tail -n +$I ./starttimes | head -1`)

  #Use time from EPOCH as start time, since its nice to use in C
  STARTEPOCH=`date -d "01/01/${SARRAY[0]} + ${SARRAY[1]} days ${SARRAY[2]}" +%s `
  #Duration is straight in the scan name.
  dur=${RARRAY[4]}
  #Trying to comform with http://vlbi.org/filename/
  EXPNAME=${RARRAY[1]}_${RARRAY[2]}_${RARRAY[0]}

  LINEOFSCAN=`grep "scan_name=${RARRAY[0]}" $NEWFILE -n| sed  's/\([0-9]*\).*/\1/'`
  #Adding $I to $LINEOFSCAN here because augmenting the file changes the
  #result each time. Don't know why thought..
  sed -i "$LINEOFSCAN a sy=ssh $VLBISTREAMER \"$VLBISTART $EXPNAME $dur -S $STARTEPOCH\"" $NEWFILE
  
  #echo $EXPNAME $STARTEPOCH $dur
  I=$(($I+1))
done < starttimes
