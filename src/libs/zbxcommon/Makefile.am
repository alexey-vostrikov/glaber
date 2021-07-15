## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libzbxcommon.a

libzbxcommon_a_SOURCES = \
	alias.c \
	comms.c \
	file.c \
	iprange.c \
	misc.c \
	setproctitle.c \
	str.c \
	variant.c \
	variant_misc.c \
	xml.c \
	zbxgetopt.c \
	time.c
	
libzbxcommon_a_CFLAGS = $(ICONV_CFLAGS) $(LIBXML2_CFLAGS)

#this was in 5.2, retrun in case of compile fail
#-I$(top_srcdir)/src/libs/zbxalgo 
