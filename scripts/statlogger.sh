#!/bin/bash - 
#===============================================================================
#
#          FILE: statlogger.sh
# 
#         USAGE: ./statlogger.sh 
# 
#   DESCRIPTION:greps and outputs cpu and mem usage to gnuplottable file
# 
#       OPTIONS: -f <file>
#  REQUIREMENTS: ---
#          BUGS: ---
#         NOTES: ---
#        AUTHOR: Tomi Salminen (), 
#  ORGANIZATION: 
#       CREATED: 15.08.2012 14.18.48 EEST
#      REVISION:  ---
#===============================================================================

usage()
{
cat << EOF
$0 : Monitors vlbistreamers stat-output and outputs data to file

Usage: $0 time

EOF
}

UTIMESPOT=14
STIMESPOT=15
VSIZESPOT=23
n=1
while [ $# -gt 0 ]
do
  #Arg1 can come at any time so doing this the boring way
  case $1 in
    *) eval "arg_$n=$1";n=$(($n+1));;
  esac
  shift
done

TIME=$arg_1
TIMESPENT=0
if [ -z "$TIME" ]
then
  usage
  exit -1
fi

echo "#time utime stime vsize"

while [ "$TIMESPENT" -le "$TIME" ]
do
  DAPID=$(pidof vlbistreamer)
  if [ -z $DAPID ]
  then
    echo "No more vlbistreamer proc detected"
    exit 1
  fi
  DASTAT=($(cat /proc/$DAPID/stat))
  processors=0
  I=0
  CPUTIME_TOTAL=0
  for i in $(cat /proc/stat|grep cpu)
  do
    case $i in
      cpu*)
	processors=$(($processors+1))
	NAME=CPUSTATS_${processors}
	I=0
	#CPUSTATS_${processors}=( )
	;;
      *)
	printf -v $NAME[$I] $i
	I=$(($I+1))
	CPUTIME_TOTAL=$(($CPUTIME_TOTAL + $i))
	;;
    esac
  done

  if [ ! -z "$CPUSTAT_OLD_1" ]
  then
    TOTALCHANGE=$(($CPUTIME_TOTAL - $LAST_CPUTIME_TOTAL))
    #echo $CPUTIME_TOTAL $LAST_CPUTIME_TOTAL
    #echo  $TOTALCHANGE
    UTIME=$((${DASTAT[UTIMESPOT]}+${DASTAT[UTIMESPOT+2]}))
    UTIMEOLD=$((${LAST_DASTAT[UTIMESPOT]}+${LAST_DASTAT[UTIMESPOT+2]}))
    STIME=$((${DASTAT[STIMESPOT]}+${DASTAT[STIMESPOT+2]}))
    STIMEOLD=$((${LAST_DASTAT[STIMESPOT]}+${LAST_DASTAT[STIMESPOT+2]}))
    #Making the value match the top-values as percentage
    CALC_UTIME=$(echo "1000*(($UTIME-$UTIMEOLD)/$TOTALCHANGE)" | bc -l)
    CALC_STIME=$(echo "1000*(($STIME-$STIMEOLD)/$TOTALCHANGE)" | bc -l)
    #echo $CALC_UTIME $CALC_STIME
    DALINE="$TIMESPENT $CALC_UTIME $CALC_STIME ${DASTAT[$VSIZESPOT]}"
    echo $DALINE
    #cho $DALINE
    #echo "one time!"
    #echo ${DASTAT[@]}
  fi

  for i in $(seq 1 $processors)
  do
    #NAME=CPUSTATS_${i}
    NAME="CPUSTATS_${i}[@]";
    NUNAME=CPUSTAT_OLD_${i}
    I=0
    for j in ${!NAME}
    do
      #At last!
      printf -v $NUNAME[$I] $j
      I=$(($I+1))
    done
    #echo ${!NAME}
    #eval echo \${$NAME[@]}
    #CPUSTAT_OLD_${i}=( eval echo \${$NAME[@]} )
    #echo \( `eval echo \${$NAME[@]}`\)
    #CPUSTAT_OLD_${i}=`echo (eval echo \${$NAME[@]})`
    #echo $NAME
    #declare -a CPUSTAT_OLD_${i}=(${!NAME});
    #declare -a `$NUNAME`=(${!NAME});
  done
  #echo ${CPUSTAT_OLD_1[@]}
  #echo ${y[@]}
  LAST_CPUTIME_TOTAL=$CPUTIME_TOTAL
  LAST_DASTAT=(${DASTAT[@]})
  TIMESPENT=$(($TIMESPENT +1 ))
  sleep 1
done
