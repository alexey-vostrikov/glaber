## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libglbpoller.a

libglbpoller_a_SOURCES = \
	glb_poller.c
	
libglbpoller_a_CFLAGS = \
	-I$(top_srcdir)/src/libs/zbxsysinfo/simple \
	-I$(top_srcdir)/src/libs/zbxdbcache \
	$(SNMP_CFLAGS) \
	$(SSH2_CFLAGS) \
	$(SSH_CFLAGS)

libglbpoller_server_a_CFLAGS = -I$(top_srcdir)/src/libs/zbxdbcache
