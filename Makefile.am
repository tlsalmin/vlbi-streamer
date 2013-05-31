## Additional flags to pass to aclocal when it is invoked automatically at
## make time. The ${ACLOCAL_FLAGS} variable is picked up from the environment
## to provide a way for the user to supply additional arguments.
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}
EXTRA_DIST=vlbistreamer.conf
cfgdir = $(sysconfdir)/
cfg_DATA = vlbistreamer.conf
rundir = $(localstatedir)/opt/vlbistreamer
logdir = $(localstatedir)/log
dist_bin_SCRIPTS = scripts/*

#TODO  Set all stuff to load vbs_common_functions first like in nfilepreview
#so we can delete all the extra stuff!
install-data-hook:
	mkdir -p $(rundir)
	mkdir -p $(cfgdir)
	mkdir -p $(logdir)
	touch $(rundir)/schedule
	chown -R @USER@:@GROUP@ $(rundir)
	chmod -R u+rw $(rundir)
	chown @USER@:@GROUP@ $(cfgdir)/vlbistreamer.conf
	chmod u+rw $(cfgdir)/vlbistreamer.conf
	touch $(logdir)/vlbistreamer.log
	chown @USER@:@GROUP@ $(logdir)/vlbistreamer.log
	chmod u+rw $(logdir)/vlbistreamer.log
	sed -i 's:ROOTDIRS=.*:ROOTDIRS=$(ROOTDIRS):' $(bindir)/vbs_common_functions
	sed -i 's:SCHEDULE=.*:SCHEDULE=$(rundir)/schedule:' $(bindir)/vbs_common_functions
	sed -i 's:LOGFILE=.*:LOGFILE=$(logdir)/vlbistreamer.log:' $(bindir)/vbs_common_functions
	sed -i 's:PIDFILE=.*:PIDFILE=$(rundir)/vlbistreamer.pid:' $(bindir)/vbs_common_functions
	sed -i 's:SCHEDLOCKFILE=.*:SCHEDLOCKFILE=$(rundir)/vlbistreamer_schedlockfile:' $(bindir)/vbs_common_functions
	sed -i 's:BINDIR=.*:BINDIR=$(bindir):' $(bindir)/vbs_common_functions
	sed -i 's:CFGFILE=.*:CFGFILE=$(cfgdir)/vlbistreamer.conf:' $(bindir)/vbs_common_functions
	sed -i 's:PPRIORITY=.*:PPRIORITY=@PPRIORITY@:' $(bindir)/vbs_common_functions
	sed -i 's:DAEMON=.*:DAEMON=$(bindir)/vlbistreamer:' $(bindir)/vbs_common_functions
	sed -i 's:USER=.*:USER=@USER@:' $(bindir)/vbs_common_functions
	sed -i 's:GROUP=.*:GROUP=@GROUP@:' $(bindir)/vbs_common_functions
	rm -f /etc/init.d/vbs_daemon
	ln -s ${bindir}/vbs_daemon /etc/init.d/vbs_daemon

uninstall-hook:
	rm /etc/init.d/vbs_daemon

## Set the default command-line flags for the C preprocessor to the value
## obtained from pkg-config via PKG_CHECK_MODULES in configure.ac.  These
## flags are passed to the compiler for both C and C++, in addition to the
## language-specific options.
#AM_CPPFLAGS = $(PTHREAD_LIBS) $(LIBAIO_LIBS)

SUBDIRS = src seqsrc . utests vbs_fs
