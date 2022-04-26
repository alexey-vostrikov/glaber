## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libglbprocess.a

libglbprocess_a_SOURCES = \
	process.c \
	process.h \
	proc_trends.h \
	proc_trends.c \
	proc_triggers.h \
	proc_triggers.c \
	proc_trigger_timers.c \
	proc_trigger_timers.h


libglbprocess_a_CFLAGS = \
	-I$(top_srcdir)/src/libs/zbxalgo \
	$(TEST_FLAGS)
