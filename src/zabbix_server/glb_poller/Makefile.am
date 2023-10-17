## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libglbpoller.a

libglbpoller_a_SOURCES = \
	glb_poller.c \
	glb_pinger.c \
	glb_worker.c \
	glb_server.c \
	poller_tcp.c	\
	snmp.c \
	calculated.c \
	poller_ipc.c \
	poller_async_io.c \
	csnmp.c \
	asn1.c \
	snmp_get.c \
	snmp_walk.c \
	poller_sessions.c \
	poller_contention.c \
	snmp_util.c \
	tcp_agent_proto.c \
	tcp_simple_http_proto.c \
	internal.c \
	poller_snmp_worker.c \
	poller_snmp_worker_discovery.c \
	poller_snmp_worker_get.c \
	poller_snmp_worker_walk.c \
	poller_agent.c
	
		
libglbpoller_a_CFLAGS = \
	-I$(top_srcdir)/src/libs/zbxsysinfo/simple \
	-I$(top_srcdir)/src/libs/zbxdbcache \
	$(SNMP_CFLAGS) \
	$(SSH2_CFLAGS) \
	$(SSH_CFLAGS)

libglbpoller_server_a_CFLAGS = -I$(top_srcdir)/src/libs/zbxdbcache
