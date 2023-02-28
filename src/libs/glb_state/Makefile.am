## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libglbstate.a

libglbstate_a_SOURCES = \
	load_dump.c \
	load_dump.h \
	glb_state.c \
	glb_state.h	\
	glb_state_items.h \
	glb_state_items.c \
	discovery.h \
	discovery.c \
	glb_state_triggers.c \
	glb_state_interfaces.c \
	glb_state.c \
	glb_state_ids.c

libglbstate_a_CFLAGS = \
	-I$(top_srcdir)/src/libs/zbxalgo
     
SUBDIRS = \
	tests