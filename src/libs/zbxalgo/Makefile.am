## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libzbxalgo.a

if SERVER
EVALUATE_C = evaluate.c
else
if PROXY
EVALUATE_C = evaluate.c
endif
endif

libzbxalgo_a_SOURCES = \
	algodefs.c \
	binaryheap.c \
	$(EVALUATE_C) \
	hashmap.c \
	hashset.c \
	int128.c \
	linked_list.c \
	prediction.c \
	queue.c \
	vector.c \
	strpool.c \
	vectorimpl.h \
	serialize.c \
	ring_buffer.c \
	binarypool.c \
	obj_index.c \
	elems_hash.c \
	event_queue.c
