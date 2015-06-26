## Introduction ##

The main method for invocing vlbi-streamer is thorugh an ssh-connection using ssh-keys and proxycommands, so the streamers can be securely inside each stations internal network structure. If the invocation need to be timed and automatic, then a good way to implement this is through cron.

The invocators need to distribute their public keys to the target machines and route them with ssh proxycommands for easy access.


## Deprecated notice ##

Most info on this page was done before daemon mode.

## SSH remote invocation ##

```
  ssh <user>@<machine> "<command>"
```

This will connect as user to machine and run the command. Using vlbistreamer, this would become for example:
```
  ssh <user>@<machine> "vlbistreamer -n 12 -p 5624 -v testwrite 20"
```
This would start a recording with 12 hard-drives in verbose mode for 20 seconds naming it to testwrite. Now of course writing this manually wouldn't be very efficient, so they should either be wrapped to scripts or aliases like:
```
  alias writeinstation='ssh <user>@recorder.station.fi "vlbistreamer -p 5643 -n 12"'
  writeinstation testwrite 20
```
after which you can just run "writeinstation name time" to write stuff in the station machine.

Almost the same with a bash script
```
#!/bin/bash
  DEF_PACKETSIZE=5678
  HOST=recorder.station.fi
  USER=stationuser
  DEF_HDS=12
  PROG=vlbistreamer
  TIME=0

  while getopts "p:h:u:d:t:" opt; do
    case $opt in
      p)
	DEF_PACKETSIZE=$OPTARG
	;;
      h)
	HOST=$OPTARG
	;;
      u)
	USER=$OPTARG
	;;
      d)
	DEF_HDS=$OPTARG
	;;
      t)
	TIME=$OPTARG
	;;
    esac
  done

  NAME=$1

  if [ ! $TIME -eq 0 ] ; then
    ssh $USER@$HOST "$PROG -p $DEF_PACKETSIZE -n $DEF_HDS $NAME $TIME" &
    echo "Recording started on $HOST"
  else
    echo "-t is a required parameter"
  fi
```
This would let parameters be specified on an if needed basis and use the default parameters else.

## SSH proxying ##

Since the recorders would preferably be protected inside the stations own intranet with only an open port for ssh and vlbi-streamer, it would normally be cumbersome to go through the links one by one to get to the recordermachine. Thankfully ssh has a config file for proxycommanding, which basically lets you route straight through to intranet machines with one ssh-call. These settings can be set in the ~/.ssh/config file.

```
Host jiverecorder
  HostName jiverecorder.vlbilan
  User vlbirecorder
  ProxyCommand ssh jivegateway.com nc %h %p

Host jivegateway.com jivegateway
  HostName jivegateway.nl
  User jiver
  ProxyCommand ssh localgateway nc %h %p

Host localgateway localgateway.lan
  Hostname localgateway.lan
  User localuser
```

In this setup simply invocing "ssh jiverecorder" would tunnel the connection through the localgateway and jivegateway into the jiverecorder machines. You can also set users per each part, so you don't have to add them to the command line. You can add any number of proxycommands to go through more gateways.

## Multistation record ##

After we've set up the ssh-config for easy access to all the machines in different stations, we can make a script for invoking a simultaneous recording on multiple stations that takes the basic name and time of recording and a comma-separated list of stations on which to record on
```
#!/bin/bash
TIME=0

function getstationparams()
{
  case $1 in
    metsahovi)
      DEF_PACKETSIZE=5678
      HOST=recorder.metsahovi.fi
      DEF_HDS=24
      PROG=vlbistreamer
      ;;
    jive)
      DEF_PACKETSIZE=6326
      HOST=recorder.jive.nl
      DEF_HDS=32
      PROG=vlbistreamer
      ;;
    ara)
      DEF_PACKETSIZE=6326
      HOST=ara.vlbi
      DEF_HDS=12
      PORT=2227
      PROG=/home/oper/src/vlbi-streamer/src/vlbistreamer
      ;;
    watt)
      DEF_PACKETSIZE=6326
      HOST=watt.vlbi
      DEF_HDS=12
      PORT=2227
      PROG=/home/oper/src/vlbi-streamer/src/vlbistreamer
      ;;
  esac
}
function remoteinv()
{
  if [ ! $TIME -eq 0 ] ; then
    ssh -q $HOST "$PROG -p $DEF_PACKETSIZE -s  $PORT -v -n $DEF_HDS $NAME $TIME " > $HOST.$NAME.log &
    echo "Recording started on $HOST"
  else
    ssh -q $HOST "$PROG -n $DEF_HDS  -s $PORT -v $NAME $TARGET" > $HOST.$NAME.log &
    echo "Sending started on $HOST"
  fi
}
while getopts "t:s:d:n:" opt; do
  case $opt in
    t)
      TIME=$OPTARG
      ;;
    s)
      STATIONS=$OPTARG
      ;;
    d)
      TARGET=$OPTARG
      ;;
    n)
      NAME=$OPTARG
      ;;
  esac
done
RECNAME=$1
LOOPVAR=${STATIONS},
IFS=,

for i in $LOOPVAR; do
  getstationparams $i
  remoteinv
done
```

Now by running ./multistationstreamer -t 20 -s ara,watt -n tesrecname all the stations listed will start to record for time t and write logfiles of the recordings to stationname.recordname.log.

```
  ./multistationstreamer -t 20 -s ara,watt -n testrec
    Recording started on ara.vlbi
    Recording started on watt.vlbi
```

## Additional functionality ##

The parameters shown here are set in the main invokers script. This is troublesome, since stations should keep their own set of operational parameters for easier updating. Such a config file should be set somewhere in the /etc-folder.

Invokers should also be able to stop vlbi-streamers remotely. This requires saving info on running vlbi-streamers, so the correct one can be stopped. A run-file could be set to /var/run and a separate local log could be run for the recordings. This also enables giving status on running vlbi-streamers. These all can be added as bash-scripts, that parse and append the config and run-file.

The listing of recorded sessions should also be added. It would simply be a script that parses the output of a regular ls-script on the remote vlbi-streamers and displays all the recordings and on which stations they are present.