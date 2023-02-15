## Process this file with automake to produce Makefile.in

DIST_SUBDIRS = \
	zbxalgo \
	zbxaudit \
	zbxavailability \
	zbxcommon \
	zbxcomms \
	zbxcommshigh \
	zbxcompress \
	zbxconf \
	zbxcrypto \
	zbxdb \
	zbxcacheconfig \
	zbxcachehistory \
	glb_state \
	glb_conf \
	zbxdbhigh \
	zbxdbwrap \
	zbxdbschema \
	zbxdbupgrade \
	zbxdiag \
	zbxdiscovery \
	zbxembed \
	zbxeval \
	zbxexec \
	zbxexport \
	zbxexpr \
	zbxgetopt \
	zbxha \
	zbxhash \
	glb_history \
	zbxhttp \
	zbxicmpping \
	zbxip \
	zbxipcservice \
	zbxjson \
	zbxkvs \
	zbxlog \
	zbxmedia \
	zbxmodules \
	zbxmutexs \
	zbxnix \
	zbxnum \
	zbxparam \
	zbxprometheus \
	zbxregexp \
	zbxrtc \
	zbxself \
	zbxserialize \
	zbxserver \
	zbxservice \
	zbxshmem \
	zbxsysinfo \
	zbxstats \
	zbxstr \
	zbxtasks \
	zbxthreads \
	zbxtime \
	zbxtrends \
	zbxvariant \
	zbxvault \
	zbxversion \
	zbxprof \
	zbxxml

if SERVER
SERVER_SUBDIRS = \
	zbxaudit \
	zbxavailability \
	zbxcompress \
	zbxdb \
	zbxcacheconfig \
	zbxcachehistory \
	glb_state \
	glb_conf \
	zbxdbhigh \
	zbxdbwrap \
	zbxdbschema \
	zbxdbupgrade \
	zbxdiag \
	zbxembed \
	zbxeval \
	zbxexport \
	zbxexpr \
	zbxha \
	glb_history \
	zbxhttp \
	zbxicmpping \
	zbxip \
	zbxkvs \
	zbxmedia \
	zbxnum \
	zbxparam \
	zbxprometheus \
	zbxrtc \
	zbxself \
	zbxserialize \
	zbxserver \
	zbxservice \
	zbxshmem \
	zbxstats \
	zbxstr \
	zbxtasks \
	zbxtime \
	zbxtrends \
	zbxvariant \
	zbxvault \
	zbxversion \
	zbxxml \
	apm \
	zbxeval \
	zbxalgo/tests \
	glb_events_log
else
if PROXY
PROXY_SUBDIRS = \
	zbxaudit \
	zbxavailability \
	zbxcompress \
	zbxdb \
	zbxcacheconfig \
	zbxcachehistory \
	zbxdbhigh \
	zbxdbwrap \
	zbxdbschema \
	zbxdbupgrade \
	zbxdiag \
	zbxembed \
	zbxeval \
	zbxexport \
	glb_history \
	zbxhttp \
	zbxicmpping \
	zbxip \
	zbxkvs \
	zbxnum \
	zbxparam \
	zbxprometheus \
	zbxrtc \
	zbxself \
	zbxserialize \
	zbxserver \
	zbxshmem \
	zbxstats \
	zbxstr \
	zbxtasks \
	zbxtime \
	zbxtrends \
	zbxvariant \
	zbxvault \
	zbxversion \
	glb_state \
	glb_conf \
	apm \
	zbxxml
endif
endif

SUBDIRS = \
	zbxalgo \
	zbxcommon \
	zbxcomms \
	zbxcommshigh \
	zbxcompress \
	zbxconf \
	zbxcrypto \
	zbxdbschema \
	zbxdiscovery \
	zbxexec \
	zbxexpr \
	zbxgetopt \
	zbxhash \
	zbxhttp \
	zbxip \
	zbxipcservice \
	zbxjson \
	zbxlog \
	zbxmodules \
	zbxmutexs \
	zbxprof \
	zbxnix \
	zbxnum \
	zbxparam \
	zbxregexp \
	zbxsysinfo \
	zbxstr \
	zbxthreads \
	zbxtime \
	zbxvariant \
	zbxversion \
	zbxxml \
	$(PROXY_SUBDIRS) \
	$(SERVER_SUBDIRS) \
	$(AGENT_SUBDIRS)

EXTRA_DIST = \
	zbxsymbols \
	zbxwin32 \
	zbxwinservice

## hashicorp and cyberark used to be a separate libraries, remove them at some point in the future
clean: clean-recursive
	rm -rf hashicorp && \
	rm -rf cyberark
.PHONY: clean
