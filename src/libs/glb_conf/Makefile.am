## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libglbconfig.a

libglbconfig_a_SOURCES = \
	conf.c \
    conf_items.c \
    conf_items_preproc.c \
    conf_hosts.c \
    conf_items_notify.c \
    ./valuemaps/conf_valuemaps.c \
    ./valuemaps/conf_valuemap.c

libglbconfig_a_CFLAGS = \
	-I$(top_srcdir)/src/libs/zbxalgo
     
