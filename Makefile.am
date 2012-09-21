## Additional flags to pass to aclocal when it is invoked automatically at
## make time. The ${ACLOCAL_FLAGS} variable is picked up from the environment
## to provide a way for the user to supply additional arguments.
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}
cfgdir = $(sysconfdir)/
cfg_DATA = vlbistreamer.conf
rundir = $(localstatedir)/opt/vlbistreamer
run_DATA = schedule
logdir = $(localstatedir)/log
log_DATA = vlbistreamer.log
bin_SCRIPTS = scripts/*

install-data-hook:
	chown -R @USER@:@GROUP@ $(rundir)
	chmod -R u+rw $(rundir)
	chown @USER@:@GROUP@ $(cfgdir)/vlbistreamer.conf
	chmod u+rw $(cfgdir)/vlbistreamer.conf
	chown @USER@:@GROUP@ $(logdir)/vlbistreamer.log
	chmod u+rw $(logdir)/vlbistreamer.log
	sed -i 's:ROOTDIRS=.*:ROOTDIRS=$(ROOTDIRS):' $(bindir)/vbs_ls
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_queue
	sed -i 's:LOGFILE=.*:LOGFILE=$(logdir)/vlbistreamer.log:' $(bindir)/vbs_queue
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_record
	sed -i 's:LOGFILE=.*:LOGFILE=$(logdir)/vlbistreamer.log:' $(bindir)/vbs_record
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_clearschedule
	sed -i 's:LOGFILE=.*:LOGFILE=$(logdir)/vlbistreamer.log:' $(bindir)/vbs_clearschedule
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_send
	sed -i 's:LOGFILE=.*:LOGFILE=$(logdir)/vlbistreamer.log:' $(bindir)/vbs_send
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_shutdown
	sed -i 's:LOGFILE=.*:LOGFILE=$(logdir)/vlbistreamer.log:' $(bindir)/vbs_daemon
	sed -i 's:PIDFILE=.*:PIDFILE=$(rundir)/vlbistreamer.pid:' $(bindir)/vbs_daemon
	sed -i 's:DAEMON=.*:DAEMON=$(bindir)/vlbistreamer:' $(bindir)/vbs_daemon
	sed -i 's:BINDIR=.*:BINDIR=$(bindir):' $(bindir)/vbs_daemon
	sed -i 's:LOGFILE=.*:LOGFILE=$(logdir)/vlbistreamer.log:' $(bindir)/vbs_logscript
	sed -i 's:BINDIR=.*:BINDIR=$(bindir):' $(bindir)/vbs_logscript
	sed -i 's:CFGFILE=.*:CFGFILE=$(cfgdir)/vlbistreamer.conf:' $(bindir)/init_hugepages
	sed -i 's:USER=.*:USER=@USER@:' $(bindir)/mount_volumes
	sed -i 's:GROUP=.*:GROUP=@GROUP@:' $(bindir)/mount_volumes
	sed -i 's:USER=.*:USER=@USER@:' $(bindir)/vbs_daemon
	sed -i 's:GROUP=.*:GROUP=@GROUP@:' $(bindir)/vbs_daemon
	sed -i 's:USER=.*:USER=@USER@:' $(bindir)/init_hugepages
	sed -i 's:GROUP=.*:GROUP=@GROUP@:' $(bindir)/init_hugepages

## Set the default command-line flags for the C preprocessor to the value
## obtained from pkg-config via PKG_CHECK_MODULES in configure.ac.  These
## flags are passed to the compiler for both C and C++, in addition to the
## language-specific options.
#AM_CPPFLAGS = $(PTHREAD_LIBS) $(LIBAIO_LIBS)

SUBDIRS = src seqsrc
