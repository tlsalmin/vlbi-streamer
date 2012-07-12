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
	chown -R @USER@ $(rundir)
	chmod -R u+rw $(rundir)
	chown @USER@ $(cfgdir)/vlbistreamer.conf
	chmod u+rw $(cfgdir)/vlbistreamer.conf
	chown @USER@ $(logdir)/vlbistreamer.log
	chmod u+rw $(logdir)/vlbistreamer.log
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_queue
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_record
	sed -i 's:LOGFILE=.*:LOGFILE=$(logdir)/vlbistreamer.log:' $(bindir)/vbs_record
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_send
	sed -i 's:LOGFILE=.*:LOGFILE=$(logdir)/vlbistreamer.log:' $(bindir)/vbs_send
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_shutdown

## Set the default command-line flags for the C preprocessor to the value
## obtained from pkg-config via PKG_CHECK_MODULES in configure.ac.  These
## flags are passed to the compiler for both C and C++, in addition to the
## language-specific options.
#AM_CPPFLAGS = $(PTHREAD_LIBS) $(LIBAIO_LIBS)

SUBDIRS = src
