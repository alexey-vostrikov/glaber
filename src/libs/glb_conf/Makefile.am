## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libglbconfig.a

libglbconfig_a_SOURCES = \
	conf.c \
    conf_hosts.c \
    ./items/conf_items.c \
    ./items/conf_items_preproc.c \
    ./items/conf_items_notify.c \
    ./valuemaps/conf_valuemaps.c \
    ./valuemaps/conf_valuemap.c \
    ./triggers/conf_calc_trigger.c \
    ./tags/tags.c \
    ./api_io/glb_api_conf.c \
    ./api_io/openssl_hostname_validation.c \
    ./api_io/hostcheck.c \
    ./scripts/scripts.c \
    ./scripts/script.c \
    api_sync.c \
    ./operations/operations.c \
    ./operations/operation.c \
    conf_parse.c

    
libglbconfig_a_CFLAGS = \
	-I$(top_srcdir)/src/libs/zbxalgo
     
