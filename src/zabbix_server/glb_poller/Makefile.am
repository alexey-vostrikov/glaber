## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libglbpoller.a

libglbpoller_a_SOURCES = \
	glb_poller.c \
	glb_pinger.c \
	glb_worker.c \
	glb_server.c \
	glb_poller.h \
	glb_pinger.h \
	glb_worker.h \
	glb_server.h \
	glb_agent.c	\
	glb_agent.h \
	snmp.c \
	calculated.c
	
libglbpoller_a_CFLAGS = \
	-I$(top_srcdir)/src/libs/zbxsysinfo/simple \
	-I$(top_srcdir)/src/libs/zbxdbcache \
	$(SNMP_CFLAGS) \
	$(SSH2_CFLAGS) \
	$(SSH_CFLAGS)

libglbpoller_server_a_CFLAGS = -I$(top_srcdir)/src/libs/zbxdbcache
