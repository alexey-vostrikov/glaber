/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"

#ifdef HAVE_SQLITE3
#	error SQLite is not supported as a main Zabbix database backend.
#endif

#include "cfg.h"
#include "pid.h"
#include "db.h"
#include "dbcache.h"
#include "zbxdbupgrade.h"
#include "log.h"
#include "zbxgetopt.h"
#include "mutexs.h"

#include "sysinfo.h"
#include "zbxmodules.h"
#include "zbxserver.h"

#include "zbxnix.h"
#include "daemon.h"
#include "zbxself.h"
#include "../libs/zbxnix/control.h"

#include "alerter/alerter.h"
#include "alerter/alert_manager.h"
#include "alerter/alert_syncer.h"
#include "dbsyncer/dbsyncer.h"
#include "dbconfig/dbconfig.h"
#include "discoverer/discoverer.h"
#include "httppoller/httppoller.h"
#include "housekeeper/housekeeper.h"
#include "pinger/pinger.h"
#include "glb_poller/glb_pinger.h"
#include "poller/poller.h"
#include "glb_poller/glb_poller.h"
#include "glb_poller/poller_ipc.h"
#include "timer/timer.h"
#include "trapper/trapper.h"
#include "snmptrapper/snmptrapper.h"
#include "escalator/escalator.h"
#include "proxypoller/proxypoller.h"
#include "selfmon/selfmon.h"
#include "vmware/vmware.h"
#include "taskmanager/taskmanager.h"
#include "preprocessor/preproc_manager.h"
#include "preprocessor/preproc_worker.h"
#include "preprocessor/glb_preproc_worker.h"
#include "glb_preproc.h"
#include "availability/avail_manager.h"
#include "lld/lld_manager.h"
#include "lld/lld_worker.h"
#include "reporter/report_manager.h"
#include "reporter/report_writer.h"
#include "tests/server_tests.h"
#include "events.h"
#include "setproctitle.h"
#include "zbxcrypto.h"
#include "zbxipcservice.h"
#include "zbxhistory.h"
#include "postinit.h"
#include "export.h"
#include "zbxvault.h"
#include "zbxdiag.h"
#include "zbxtrends.h"
#include "../libs/zbxexec/worker.h"
#include "../libs/zbxipcservice/glb_ipc.h"
#include "../libs/glb_state/glb_state.h"
#include "../libs/glb_state/glb_state_items.h"
#include "../libs/apm/apm.h"

#ifdef HAVE_OPENIPMI
#include "ipmi/ipmi_manager.h"
#include "ipmi/ipmi_poller.h"
#endif

const char	*progname = NULL;
const char	title_message[] = "zabbix_server";
const char	syslog_app_name[] = "zabbix_server";
const char	*usage_message[] = {
	"[-c config-file]", NULL,
	"[-c config-file]", "-R runtime-option", NULL,
	"-h", NULL,
	"-V", NULL,
	NULL	/* end of text */
};

const char	*help_message[] = {
	"The core daemon of Zabbix software.",
	"",
	"Options:",
	"  -c --config config-file        Path to the configuration file",
	"                                 (default: \"" DEFAULT_CONFIG_FILE "\")",
	"  -f --foreground                Run Zabbix server in foreground",
	"  -R --runtime-control runtime-option   Perform administrative functions",
	"",
	"    Runtime control options:",
	"      " ZBX_CONFIG_CACHE_RELOAD "        Reload configuration cache",
	"      " ZBX_HOUSEKEEPER_EXECUTE "        Execute the housekeeper",
	"      " ZBX_LOG_LEVEL_INCREASE "=target  Increase log level, affects all processes if",
	"                                 target is not specified",
	"      " ZBX_LOG_LEVEL_DECREASE "=target  Decrease log level, affects all processes if",
	"                                 target is not specified",
	"      " ZBX_SNMP_CACHE_RELOAD "          Reload SNMP cache",
	"      " ZBX_SECRETS_RELOAD "             Reload secrets from Vault",
	"      " ZBX_DIAGINFO "=section           Log internal diagnostic information of the",
	"                                 section (historycache, preprocessing, alerting,",
	"                                 lld, valuecache, locks) or everything if section is",
	"                                 not specified",
	"",
	"      Log level control targets:",
	"        process-type             All processes of specified type",
	"                                 (alerter, alert manager, configuration syncer,",
	"                                 discoverer, escalator, history syncer,",
	"                                 housekeeper, http poller, icmp pinger,",
	"                                 ipmi manager, ipmi poller, java poller,",
	"                                 poller, preprocessing manager,",
	"                                 preprocessing worker, proxy poller,",
	"                                 self-monitoring, snmp trapper, task manager,",
	"                                 timer, trapper, unreachable poller,",
	"                                 vmware collector, history poller, availability manager)",
	"        process-type,N           Process type and number (e.g., poller,3)",
	"        pid                      Process identifier, up to 65535. For larger",
	"                                 values specify target as \"process-type,N\"",
	"",
	"  -h --help                      Display this help message",
	"  -V --version                   Display version number",
	"",
	"Some configuration parameter default locations:",
	"  AlertScriptsPath               \"" DEFAULT_ALERT_SCRIPTS_PATH "\"",
	"  ExternalScripts                \"" DEFAULT_EXTERNAL_SCRIPTS_PATH "\"",
	"  WorkerScripts                  \"" DEFAULT_WORKER_SCRIPTS_PATH "\"",
#ifdef HAVE_LIBCURL
	"  SSLCertLocation                \"" DEFAULT_SSL_CERT_LOCATION "\"",
	"  SSLKeyLocation                 \"" DEFAULT_SSL_KEY_LOCATION "\"",
#endif
	"  LoadModulePath                 \"" DEFAULT_LOAD_MODULE_PATH "\"",
	NULL	/* end of text */
};

/* COMMAND LINE OPTIONS */

/* long options */
static struct zbx_option	longopts[] =
{
	{"config",		1,	NULL,	'c'},
	{"foreground",		0,	NULL,	'f'},
	{"runtime-control",	1,	NULL,	'R'},
	{"help",		0,	NULL,	'h'},
	{"version",		0,	NULL,	'V'},
	{NULL}
};

void DC_set_debug_item(u_int64_t);
void DC_set_debug_trigger(u_int64_t);

/* short options */
static char	shortopts[] = "c:hVR:f";

/* end of COMMAND LINE OPTIONS */

int		threads_num = 0;
pid_t		*threads = NULL;
static int	*threads_flags;

unsigned char	program_type		= ZBX_PROGRAM_TYPE_SERVER;
unsigned char	process_type		= ZBX_PROCESS_TYPE_UNKNOWN;
int		process_num		= 0;
int		server_num		= 0;

u_int64_t CONFIG_DEBUG_ITEM = 0;
u_int64_t CONFIG_DEBUG_TRIGGER = 0;

int CONFIG_ENABLE_HOST_DEACTIVATION = 1;
int	CONFIG_ALERTER_FORKS		= 3;
int	CONFIG_DISCOVERER_FORKS		= 1;
int	CONFIG_HOUSEKEEPER_FORKS	= 1;
int CONFIG_GLB_SNMP_FORKS		= 1;
int CONFIG_GLB_SNMP_CONTENTION	= 1;
int CONFIG_GLB_PINGER_FORKS		= 1;
int CONFIG_GLB_WORKER_FORKS		= 0;
int CONFIG_GLB_AGENT_FORKS		= 0;

int CONFIG_ICMP_METHOD  = GLB_ICMP;
char *CONFIG_VCDUMP_LOCATION	= NULL;
int CONFIG_VCDUMP_FREQUENCY	= 60;

char	*CONFIG_SERVERS = NULL;

int	CONFIG_POLLER_FORKS		= 5;
int	CONFIG_PINGER_FORKS		= 5;
int	CONFIG_UNREACHABLE_POLLER_FORKS	= 1;

int	CONFIG_HTTPPOLLER_FORKS		= 1;
int	CONFIG_IPMIPOLLER_FORKS		= 0;
int	CONFIG_TIMER_FORKS		= 1;
int	CONFIG_TRAPPER_FORKS		= 5;
int	CONFIG_API_TRAPPER_FORKS	= 0;
int	CONFIG_SNMPTRAPPER_FORKS	= 0;
int	CONFIG_JAVAPOLLER_FORKS		= 0;
int	CONFIG_ESCALATOR_FORKS		= 1;
int	CONFIG_SELFMON_FORKS		= 1;
int	CONFIG_DATASENDER_FORKS		= 0;
int	CONFIG_HEARTBEAT_FORKS		= 0;
int	CONFIG_COLLECTOR_FORKS		= 0;
int	CONFIG_PASSIVE_FORKS		= 0;
int	CONFIG_ACTIVE_FORKS		= 0;
int	CONFIG_TASKMANAGER_FORKS	= 1;
int	CONFIG_IPMIMANAGER_FORKS	= 0;
int	CONFIG_ALERTMANAGER_FORKS	= 1;
int	CONFIG_PREPROCMAN_FORKS		= 1;
int	CONFIG_PREPROCESSOR_FORKS	= 3;
int	CONFIG_GLB_PREPROCESSOR_FORKS	= 1;
int	CONFIG_LLDMANAGER_FORKS		= 1;
int	CONFIG_LLDWORKER_FORKS		= 2;
int	CONFIG_ALERTDB_FORKS		= 1;
int	CONFIG_HISTORYPOLLER_FORKS	= 5;
int	CONFIG_AVAILMAN_FORKS		= 1;
int	CONFIG_REPORTMANAGER_FORKS	= 0;
int	CONFIG_REPORTWRITER_FORKS	= 0;

int	CONFIG_LISTEN_PORT		= ZBX_DEFAULT_SERVER_PORT;
int	CONFIG_API_LISTEN_PORT		= GLB_DEFAULT_API_PORT;

char	*CONFIG_LISTEN_IP		= NULL;
char	*CONFIG_SOURCE_IP		= NULL;
int	CONFIG_TRAPPER_TIMEOUT		= 300;
char	*CONFIG_SERVER			= NULL;		/* not used in zabbix_server, required for linking */

int	CONFIG_HOUSEKEEPING_FREQUENCY	= 1;
int	CONFIG_MAX_HOUSEKEEPER_DELETE	= 5000;		/* applies for every separate field value */
int	CONFIG_HISTSYNCER_FORKS		= 4;
int	CONFIG_HISTSYNCER_FREQUENCY	= 1;
int	CONFIG_CONFSYNCER_FORKS		= 1;
int	CONFIG_CONFSYNCER_FREQUENCY	= 60;
int CONFIG_GLB_REQUEUE_TIME =  60; //syncs the async queue once a minute
int CONFIG_EXT_SERVER_FORKS = 1;

int	CONFIG_VMWARE_FORKS		= 0;
int	CONFIG_VMWARE_FREQUENCY		= 60;
int	CONFIG_VMWARE_PERF_FREQUENCY	= 60;
int	CONFIG_VMWARE_TIMEOUT		= 10;

zbx_uint64_t	CONFIG_CONF_CACHE_SIZE		= 8 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_HISTORY_CACHE_SIZE	= 16 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_HISTORY_INDEX_CACHE_SIZE	= 4 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_TRENDS_CACHE_SIZE	= 4 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_TREND_FUNC_CACHE_SIZE	= 4 * ZBX_MEBIBYTE;
u_int64_t			CONFIG_VALUE_CACHE_SIZE		= 512 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_VMWARE_CACHE_SIZE	= 8 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_EXPORT_FILE_SIZE		= ZBX_GIBIBYTE;
u_int64_t 		CONFIG_IPC_BUFFER_SIZE		= 512 * ZBX_MEBIBYTE; 

int CONFIG_SELF_MONITOR_PORT		= DEFAULT_SELF_MONITOR_PORT;
char	*CONFIG_SELF_MONITOR_IP		= NULL;

int	CONFIG_UNREACHABLE_PERIOD	= 45;
int	CONFIG_UNREACHABLE_DELAY	= 15;
int	CONFIG_UNAVAILABLE_DELAY	= 60;
int	CONFIG_LOG_LEVEL		= LOG_LEVEL_WARNING;
char	*CONFIG_ALERT_SCRIPTS_PATH	= NULL;
char	*CONFIG_EXTERNALSCRIPTS		= NULL;
char	*CONFIG_WORKERS_DIR		= NULL;
char	*CONFIG_TMPDIR			= NULL;
char	*CONFIG_FPING_LOCATION		= NULL;
char	*CONFIG_FPING6_LOCATION		= NULL;
char 	*CONFIG_GLBMAP_LOCATION		= NULL;
char 	*CONFIG_GLBMAP_OPTIONS		= NULL;
//int		CONFIG_GLB_REUEUE_TIME	= 120;

char	*CONFIG_DBHOST			= NULL;
char	*CONFIG_DBNAME			= NULL;
char	*CONFIG_DBSCHEMA		= NULL;
char	*CONFIG_DBUSER			= NULL;
char	*CONFIG_DBPASSWORD		= NULL;
char	*CONFIG_VAULTTOKEN		= NULL;
char	*CONFIG_VAULTURL		= NULL;
char	*CONFIG_VAULTDBPATH		= NULL;
char	*CONFIG_DBSOCKET		= NULL;
char	*CONFIG_DB_TLS_CONNECT		= NULL;
char	*CONFIG_DB_TLS_CERT_FILE	= NULL;
char	*CONFIG_DB_TLS_KEY_FILE		= NULL;
char	*CONFIG_DB_TLS_CA_FILE		= NULL;
char	*CONFIG_DB_TLS_CIPHER		= NULL;
char	*CONFIG_DB_TLS_CIPHER_13	= NULL;
char	*CONFIG_EXPORT_DIR		= NULL;
char	*CONFIG_EXPORT_TYPE		= NULL;
int	CONFIG_DBPORT			= 0;
int	CONFIG_ENABLE_REMOTE_COMMANDS	= 0;
int	CONFIG_LOG_REMOTE_COMMANDS	= 0;
int	CONFIG_UNSAFE_USER_PARAMETERS	= 0;

char	*CONFIG_SNMPTRAP_FILE		= NULL;
char 	*ICMP_METHOD_STR = NULL;

char	*CONFIG_JAVA_GATEWAY		= NULL;
int	CONFIG_JAVA_GATEWAY_PORT	= ZBX_DEFAULT_GATEWAY_PORT;

char	*CONFIG_SSH_KEY_LOCATION	= NULL;

int	CONFIG_LOG_SLOW_QUERIES		= 0;	/* ms; 0 - disable */

int	CONFIG_SERVER_STARTUP_TIME	= 0;	/* zabbix server startup time */

int	CONFIG_PROXYPOLLER_FORKS	= 3;	/* parameters for passive proxies */

/* how often Zabbix server sends configuration data to proxy, in seconds */
//int	CONFIG_PROXYCONFIG_FREQUENCY	= SEC_PER_HOUR;
int	CONFIG_PROXYCONFIG_FREQUENCY	= 60; // todo - make it somewhat longer or ZERO the nextconfig on apply topology
int	CONFIG_PROXYDATA_FREQUENCY	= 5;	/* 1s is too frequent for n/a proxies */

char	*CONFIG_LOAD_MODULE_PATH	= NULL;
char	**CONFIG_LOAD_MODULE		= NULL;
char 	**CONFIG_HISTORY_MODULE		= NULL;
int		CONFIG_SNMP_RETRIES		=	3;

//char	**CONFIG_EXT_SERVERS	= NULL;

char	*CONFIG_USER			= NULL;

/* web monitoring */
char	*CONFIG_SSL_CA_LOCATION		= NULL;
char	*CONFIG_SSL_CERT_LOCATION	= NULL;
char	*CONFIG_SSL_KEY_LOCATION	= NULL;

/* TLS parameters */
unsigned int	configured_tls_connect_mode = ZBX_TCP_SEC_UNENCRYPTED;	/* not used in server, defined for linking */
									/* with tls.c */
unsigned int	configured_tls_accept_modes = ZBX_TCP_SEC_UNENCRYPTED;	/* not used in server, defined for linking */
									/* with tls.c */
char	*CONFIG_TLS_CA_FILE		= NULL;
char	*CONFIG_TLS_CRL_FILE		= NULL;
char	*CONFIG_TLS_CERT_FILE		= NULL;
char	*CONFIG_TLS_KEY_FILE		= NULL;
char	*CONFIG_TLS_CIPHER_CERT13	= NULL;
char	*CONFIG_TLS_CIPHER_CERT		= NULL;
char	*CONFIG_TLS_CIPHER_PSK13	= NULL;
char	*CONFIG_TLS_CIPHER_PSK		= NULL;
char	*CONFIG_TLS_CIPHER_ALL13	= NULL;
char	*CONFIG_TLS_CIPHER_ALL		= NULL;
char	*CONFIG_TLS_CIPHER_CMD13	= NULL;	/* not used in server, defined for linking with tls.c */
char	*CONFIG_TLS_CIPHER_CMD		= NULL;	/* not used in server, defined for linking with tls.c */
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
/* the following TLS parameters are not used in server, they are defined for linking with tls.c */
char	*CONFIG_TLS_CONNECT		= NULL;
char	*CONFIG_TLS_ACCEPT		= NULL;
char	*CONFIG_TLS_SERVER_CERT_ISSUER	= NULL;
char	*CONFIG_TLS_SERVER_CERT_SUBJECT	= NULL;
char	*CONFIG_TLS_PSK_IDENTITY	= NULL;
char	*CONFIG_TLS_PSK_FILE		= NULL;
#endif

static char	*CONFIG_SOCKET_PATH	= NULL;
char	CONFIG_CLUSTER_DOMAINS[MAX_ZBX_HOSTNAME_LEN];
int 	CONFIG_CLUSTER_SERVER_ID =0;
int		CONFIG_CLUSTER_REROUTE_DATA	= 1;
zbx_vector_ptr_t *API_CALLBACKS[GLB_MODULE_API_TOTAL_CALLBACKS];

//char	*CONFIG_HISTORY_STORAGE_URL		= NULL;
//char	*CONFIG_HISTORY_STORAGE_OPTS		= NULL;
//int	CONFIG_HISTORY_STORAGE_PIPELINES	= 0;

char	CONFIG_HOSTNAME[MAX_ZBX_HOSTNAME_LEN];

char	*CONFIG_STATS_ALLOWED_IP	= NULL;
int	CONFIG_TCP_MAX_BACKLOG_SIZE	= SOMAXCONN;

int	CONFIG_DOUBLE_PRECISION		= ZBX_DB_DBL_PRECISION_ENABLED;

char	*CONFIG_WEBSERVICE_URL	= NULL;

volatile sig_atomic_t	zbx_diaginfo_scope = ZBX_DIAGINFO_UNDEFINED;

int	get_process_info_by_thread(int local_server_num, unsigned char *local_process_type, int *local_process_num);

int	get_process_info_by_thread(int local_server_num, unsigned char *local_process_type, int *local_process_num)
{
	int	server_count = 0;

	if (0 == local_server_num)
	{
		/* fail if the main process is queried */
		return FAIL;
	}
	else if (local_server_num <= (server_count += CONFIG_CONFSYNCER_FORKS))
	{
		/* make initial configuration sync before worker processes are forked */
		*local_process_type = ZBX_PROCESS_TYPE_CONFSYNCER;
		*local_process_num = local_server_num - server_count + CONFIG_CONFSYNCER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_ALERTMANAGER_FORKS))
	{
		/* data collection processes might utilize CPU fully, start manager and worker processes beforehand */
		*local_process_type = ZBX_PROCESS_TYPE_ALERTMANAGER;
		*local_process_num = local_server_num - server_count + CONFIG_ALERTMANAGER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_ALERTER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_ALERTER;
		*local_process_num = local_server_num - server_count + CONFIG_ALERTER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_PREPROCMAN_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_PREPROCMAN;
		*local_process_num = local_server_num - server_count + CONFIG_PREPROCMAN_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_PREPROCESSOR_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_PREPROCESSOR;
		*local_process_num = local_server_num - server_count + CONFIG_PREPROCESSOR_FORKS;
	} else if (local_server_num <= (server_count += CONFIG_GLB_PREPROCESSOR_FORKS))
	{
		*local_process_type = GLB_PROCESS_TYPE_PREPROCESSOR;
		*local_process_num = local_server_num - server_count + CONFIG_GLB_PREPROCESSOR_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_LLDMANAGER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_LLDMANAGER;
		*local_process_num = local_server_num - server_count + CONFIG_LLDMANAGER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_LLDWORKER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_LLDWORKER;
		*local_process_num = local_server_num - server_count + CONFIG_LLDWORKER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_IPMIMANAGER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_IPMIMANAGER;
		*local_process_num = local_server_num - server_count + CONFIG_TASKMANAGER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_HOUSEKEEPER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_HOUSEKEEPER;
		*local_process_num = local_server_num - server_count + CONFIG_HOUSEKEEPER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_TIMER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_TIMER;
		*local_process_num = local_server_num - server_count + CONFIG_TIMER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_HTTPPOLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_HTTPPOLLER;
		*local_process_num = local_server_num - server_count + CONFIG_HTTPPOLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_DISCOVERER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_DISCOVERER;
		*local_process_num = local_server_num - server_count + CONFIG_DISCOVERER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_HISTSYNCER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_HISTSYNCER;
		*local_process_num = local_server_num - server_count + CONFIG_HISTSYNCER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_ESCALATOR_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_ESCALATOR;
		*local_process_num = local_server_num - server_count + CONFIG_ESCALATOR_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_IPMIPOLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_IPMIPOLLER;
		*local_process_num = local_server_num - server_count + CONFIG_IPMIPOLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_JAVAPOLLER_FORKS ))
	{
		*local_process_type = ZBX_PROCESS_TYPE_JAVAPOLLER;
		*local_process_num = local_server_num - server_count + CONFIG_JAVAPOLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_SNMPTRAPPER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_SNMPTRAPPER;
		*local_process_num = local_server_num - server_count + CONFIG_SNMPTRAPPER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_PROXYPOLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_PROXYPOLLER;
		*local_process_num = local_server_num - server_count + CONFIG_PROXYPOLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_SELFMON_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_SELFMON;
		*local_process_num = local_server_num - server_count + CONFIG_SELFMON_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_VMWARE_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_VMWARE;
		*local_process_num = local_server_num - server_count + CONFIG_VMWARE_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_TASKMANAGER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_TASKMANAGER;
		*local_process_num = local_server_num - server_count + CONFIG_TASKMANAGER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_POLLER_FORKS ))
	{
		*local_process_type = ZBX_PROCESS_TYPE_POLLER;
		*local_process_num = local_server_num - server_count + CONFIG_POLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_GLB_SNMP_FORKS))
	{
		*local_process_type = GLB_PROCESS_TYPE_SNMP;
		*local_process_num = local_server_num - server_count + CONFIG_GLB_SNMP_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_GLB_PINGER_FORKS))
	{
		*local_process_type = GLB_PROCESS_TYPE_PINGER;
		*local_process_num = local_server_num - server_count + CONFIG_GLB_PINGER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_GLB_WORKER_FORKS))
	{
		*local_process_type = GLB_PROCESS_TYPE_WORKER;
		*local_process_num = local_server_num - server_count + CONFIG_GLB_WORKER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_GLB_AGENT_FORKS))
	{
		*local_process_type = GLB_PROCESS_TYPE_AGENT;
		*local_process_num = local_server_num - server_count + CONFIG_GLB_AGENT_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_EXT_SERVER_FORKS))
	{
		*local_process_type = GLB_PROCESS_TYPE_SERVER;
		*local_process_num = local_server_num - server_count + CONFIG_EXT_SERVER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_UNREACHABLE_POLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_UNREACHABLE;
		*local_process_num = local_server_num - server_count + CONFIG_UNREACHABLE_POLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_TRAPPER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_TRAPPER;
		*local_process_num = local_server_num - server_count + CONFIG_TRAPPER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_API_TRAPPER_FORKS))
	{
		*local_process_type =GLB_PROCESS_TYPE_API_TRAPPER;
		*local_process_num = local_server_num - server_count + CONFIG_API_TRAPPER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_PINGER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_PINGER;
		*local_process_num = local_server_num - server_count + CONFIG_PINGER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_ALERTDB_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_ALERTSYNCER;
		*local_process_num = local_server_num - server_count + CONFIG_ALERTDB_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_HISTORYPOLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_HISTORYPOLLER;
		*local_process_num = local_server_num - server_count + CONFIG_HISTORYPOLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_AVAILMAN_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_AVAILMAN;
		*local_process_num = local_server_num - server_count + CONFIG_AVAILMAN_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_REPORTMANAGER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_REPORTMANAGER;
		*local_process_num = local_server_num - server_count + CONFIG_REPORTMANAGER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_REPORTWRITER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_REPORTWRITER;
		*local_process_num = local_server_num - server_count + CONFIG_REPORTWRITER_FORKS;
	}
	else
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_set_defaults                                                 *
 *                                                                            *
 * Purpose: set configuration defaults                                        *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 ******************************************************************************/
static void	zbx_set_defaults(void)
{
	CONFIG_SERVER_STARTUP_TIME = time(NULL);

	if (NULL == CONFIG_DBHOST)
		CONFIG_DBHOST = zbx_strdup(CONFIG_DBHOST, "localhost");

	if (NULL == CONFIG_SNMPTRAP_FILE)
		CONFIG_SNMPTRAP_FILE = zbx_strdup(CONFIG_SNMPTRAP_FILE, "/tmp/zabbix_traps.tmp");

	if (NULL == CONFIG_PID_FILE)
		CONFIG_PID_FILE = zbx_strdup(CONFIG_PID_FILE, "/tmp/zabbix_server.pid");

	if (NULL == CONFIG_ALERT_SCRIPTS_PATH)
		CONFIG_ALERT_SCRIPTS_PATH = zbx_strdup(CONFIG_ALERT_SCRIPTS_PATH, DEFAULT_ALERT_SCRIPTS_PATH);

	if (NULL == CONFIG_LOAD_MODULE_PATH)
		CONFIG_LOAD_MODULE_PATH = zbx_strdup(CONFIG_LOAD_MODULE_PATH, DEFAULT_LOAD_MODULE_PATH);

	if (NULL == CONFIG_TMPDIR)
		CONFIG_TMPDIR = zbx_strdup(CONFIG_TMPDIR, "/tmp");

	if (NULL == CONFIG_FPING_LOCATION)
		CONFIG_FPING_LOCATION = zbx_strdup(CONFIG_FPING_LOCATION, "/usr/sbin/fping");
	
	if (NULL == CONFIG_GLBMAP_LOCATION)
		CONFIG_GLBMAP_LOCATION = zbx_strdup(CONFIG_GLBMAP_LOCATION, "/usr/sbin/glbmap");

#ifdef HAVE_IPV6
	if (NULL == CONFIG_FPING6_LOCATION)
		CONFIG_FPING6_LOCATION = zbx_strdup(CONFIG_FPING6_LOCATION, "/usr/sbin/fping6");
#endif

	if (NULL == CONFIG_EXTERNALSCRIPTS)
		CONFIG_EXTERNALSCRIPTS = zbx_strdup(CONFIG_EXTERNALSCRIPTS, DEFAULT_EXTERNAL_SCRIPTS_PATH);
		
	if (NULL == CONFIG_WORKERS_DIR)
		CONFIG_WORKERS_DIR = zbx_strdup(CONFIG_WORKERS_DIR, DEFAULT_WORKER_SCRIPTS_PATH);
			
#ifdef HAVE_LIBCURL
	if (NULL == CONFIG_SSL_CERT_LOCATION)
		CONFIG_SSL_CERT_LOCATION = zbx_strdup(CONFIG_SSL_CERT_LOCATION, DEFAULT_SSL_CERT_LOCATION);

	if (NULL == CONFIG_SSL_KEY_LOCATION)
		CONFIG_SSL_KEY_LOCATION = zbx_strdup(CONFIG_SSL_KEY_LOCATION, DEFAULT_SSL_KEY_LOCATION);
#endif

#ifdef HAVE_SQLITE3
	CONFIG_MAX_HOUSEKEEPER_DELETE = 0;
#endif

	if (NULL == CONFIG_LOG_TYPE_STR)
		CONFIG_LOG_TYPE_STR = zbx_strdup(CONFIG_LOG_TYPE_STR, ZBX_OPTION_LOGTYPE_FILE);

	if (NULL == CONFIG_SOCKET_PATH)
		CONFIG_SOCKET_PATH = zbx_strdup(CONFIG_SOCKET_PATH, "/tmp");

	if (NULL == CONFIG_HOSTNAME)
	 	gethostname(CONFIG_HOSTNAME, MAX_ZBX_HOSTNAME_LEN);

	if (0 != CONFIG_IPMIPOLLER_FORKS)
		CONFIG_IPMIMANAGER_FORKS = 1;
	
	int i;

	for ( i = 0; i < GLB_MODULE_API_TOTAL_CALLBACKS ; i++ ) {
		API_CALLBACKS[i]=zbx_malloc(NULL, sizeof(zbx_vector_ptr_t));
		zbx_vector_ptr_create(API_CALLBACKS[i]);
	}

	if (NULL == CONFIG_VAULTURL)
		CONFIG_VAULTURL = zbx_strdup(CONFIG_VAULTURL, "https://127.0.0.1:8200");

	if (0 != CONFIG_REPORTWRITER_FORKS)
		CONFIG_REPORTMANAGER_FORKS = 1;

	if ( NULL != ICMP_METHOD_STR && NULL != strstr(ICMP_METHOD_STR,ZBX_ICMP_NAME) ) {
			zabbix_log(LOG_LEVEL_DEBUG, "Setting ICMP method to Zabbix ICMP (fping)");
		CONFIG_ICMP_METHOD = ZBX_ICMP;
	} else {
		zabbix_log(LOG_LEVEL_DEBUG, "Setting ICMP method to Glaber ICMP (async + glbmap)");
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_validate_config                                              *
 *                                                                            *
 * Purpose: validate configuration parameters                                 *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 ******************************************************************************/
static void	zbx_validate_config(ZBX_TASK_EX *task)
{
	char	*ch_error;
	int	err = 0;

	if (0 == CONFIG_UNREACHABLE_POLLER_FORKS && 0 != CONFIG_POLLER_FORKS + CONFIG_JAVAPOLLER_FORKS)
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"StartPollersUnreachable\" configuration parameter must not be 0"
				" if regular or Java pollers are started");
		err = 1;
	}

	if ((NULL == CONFIG_JAVA_GATEWAY || '\0' == *CONFIG_JAVA_GATEWAY) && 0 < CONFIG_JAVAPOLLER_FORKS)
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"JavaGateway\" configuration parameter is not specified or empty");
		err = 1;
	}

	if (0 != CONFIG_VALUE_CACHE_SIZE && 128 * ZBX_KIBIBYTE > CONFIG_VALUE_CACHE_SIZE)
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"ValueCacheSize\" configuration parameter must be either 0"
				" or greater than 128KB");
		err = 1;
	}

	if (0 != CONFIG_TREND_FUNC_CACHE_SIZE && 128 * ZBX_KIBIBYTE > CONFIG_TREND_FUNC_CACHE_SIZE)
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"TrendFunctionCacheSize\" configuration parameter must be either 0"
				" or greater than 128KB");
		err = 1;
	}

	if (NULL != CONFIG_SOURCE_IP && SUCCEED != is_supported_ip(CONFIG_SOURCE_IP))
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid \"SourceIP\" configuration parameter: '%s'", CONFIG_SOURCE_IP);
		err = 1;
	}

	if (NULL != CONFIG_STATS_ALLOWED_IP && FAIL == zbx_validate_peer_list(CONFIG_STATS_ALLOWED_IP, &ch_error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid entry in \"StatsAllowedIP\" configuration parameter: %s", ch_error);
		zbx_free(ch_error);
		err = 1;
	}

	if (SUCCEED != 	zbx_validate_export_type(CONFIG_EXPORT_TYPE, NULL))
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid \"ExportType\" configuration parameter: %s", CONFIG_EXPORT_TYPE);
		err = 1;
	}

#if !defined(HAVE_IPV6)
	err |= (FAIL == check_cfg_feature_str("Fping6Location", CONFIG_FPING6_LOCATION, "IPv6 support"));
#endif
#if !defined(HAVE_LIBCURL)
	err |= (FAIL == check_cfg_feature_str("SSLCALocation", CONFIG_SSL_CA_LOCATION, "cURL library"));
	err |= (FAIL == check_cfg_feature_str("SSLCertLocation", CONFIG_SSL_CERT_LOCATION, "cURL library"));
	err |= (FAIL == check_cfg_feature_str("SSLKeyLocation", CONFIG_SSL_KEY_LOCATION, "cURL library"));
	err |= (FAIL == check_cfg_feature_int("HistoryStorageDateIndex", CONFIG_HISTORY_STORAGE_PIPELINES,
			"cURL library"));
	err |= (FAIL == check_cfg_feature_str("VaultToken", CONFIG_VAULTTOKEN, "cURL library"));
	err |= (FAIL == check_cfg_feature_str("VaultDBPath", CONFIG_VAULTDBPATH, "cURL library"));

	err |= (FAIL == check_cfg_feature_int("StartReportWriters", CONFIG_REPORTWRITER_FORKS, "cURL library"));
#endif

#if !defined(HAVE_LIBXML2) || !defined(HAVE_LIBCURL)
	err |= (FAIL == check_cfg_feature_int("StartVMwareCollectors", CONFIG_VMWARE_FORKS, "VMware support"));

	/* parameters VMwareFrequency, VMwarePerfFrequency, VMwareCacheSize, VMwareTimeout are not checked here */
	/* because they have non-zero default values */
#endif

	if (SUCCEED != zbx_validate_log_parameters(task))
		err = 1;

#if !(defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	err |= (FAIL == check_cfg_feature_str("TLSCAFile", CONFIG_TLS_CA_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSCRLFile", CONFIG_TLS_CRL_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSCertFile", CONFIG_TLS_CERT_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSKeyFile", CONFIG_TLS_KEY_FILE, "TLS support"));
#endif
#if !(defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	err |= (FAIL == check_cfg_feature_str("TLSCipherCert", CONFIG_TLS_CIPHER_CERT, "GnuTLS or OpenSSL"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherPSK", CONFIG_TLS_CIPHER_PSK, "GnuTLS or OpenSSL"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherAll", CONFIG_TLS_CIPHER_ALL, "GnuTLS or OpenSSL"));
#endif
#if !defined(HAVE_OPENSSL)
	err |= (FAIL == check_cfg_feature_str("TLSCipherCert13", CONFIG_TLS_CIPHER_CERT13, "OpenSSL 1.1.1 or newer"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherPSK13", CONFIG_TLS_CIPHER_PSK13, "OpenSSL 1.1.1 or newer"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherAll13", CONFIG_TLS_CIPHER_ALL13, "OpenSSL 1.1.1 or newer"));
#endif

#if !defined(HAVE_OPENIPMI)
	err |= (FAIL == check_cfg_feature_int("StartIPMIPollers", CONFIG_IPMIPOLLER_FORKS, "IPMI support"));
#endif
	err |= (FAIL == zbx_db_validate_config_features());

	if (0 != CONFIG_REPORTWRITER_FORKS && NULL == CONFIG_WEBSERVICE_URL)
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"WebServiceURL\" configuration parameter must be set when "
				" setting \"StartReportWriters\" configuration parameter");
	}

#if !defined(HAVE_NETSNMP)
	err |= (FAIL == check_cfg_feature_int("StartGlbSNMPPollers", CONFIG_GLB_SNMP_FORKS, "SNMP support"));
#endif

	if ( 0 == CONFIG_PINGER_FORKS &&  ZBX_ICMP == CONFIG_ICMP_METHOD) {
		zbx_error("Cannot use default ICMP method fping without any PINGER poller enabled, set StartPingers > 0 in the server config file");
		exit(EXIT_FAILURE);
	}

	if ( 0 == CONFIG_GLB_PINGER_FORKS &&  GLB_ICMP == CONFIG_ICMP_METHOD) {
		zbx_error("Cannot use default ICMP method glbmap without any Glaber pinger poller enabled, set StartGlbPingers > 0 or set DefaultICMPMethod=fping in the server config file");
		exit(EXIT_FAILURE);
	}

	//if (NULL != *CONFIG_EXT_SERVERS) {
	//	zabbix_log(LOG_LEVEL_WARNING,"Enabling worker server process");
	//	CONFIG_EXT_SERVER_FORKS = 1;
	//}

	if (0 != err)
		exit(EXIT_FAILURE);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_load_config                                                  *
 *                                                                            *
 * Purpose: parse config file and update configuration parameters             *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: will terminate process if parsing fails                          *
 *                                                                            *
 ******************************************************************************/
static void	zbx_load_config(ZBX_TASK_EX *task)
{	int i;
	static struct cfg_line	cfg[] =
	{
		/* PARAMETER,			VAR,					TYPE,
			MANDATORY,	MIN,			MAX */
		{"SnmpRetries",			&CONFIG_SNMP_RETRIES,			TYPE_INT,
			PARM_OPT,	1,			100},
		{"DebugItem",			&CONFIG_DEBUG_ITEM,			TYPE_INT,
			PARM_OPT,	0,			0},
		{"DebugTrigger",			&CONFIG_DEBUG_TRIGGER,			TYPE_INT,
			PARM_OPT,	0,			0},
	//	{"DisableInPollerPreproc",			&CONFIG_DISABLE_INPOLLER_PREPROC,			TYPE_INT,
	//		PARM_OPT,	0,			0},
		{"EnableHostDeactivation",			&CONFIG_ENABLE_HOST_DEACTIVATION,			TYPE_INT,
			PARM_OPT,	0,			0},
//		{"ExternalWorker",			&CONFIG_EXT_WORKERS,			TYPE_MULTISTRING,
//			PARM_OPT,	0,			0},
		{"StartWorkerServers",		&CONFIG_EXT_SERVER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			8},
		{"StartDBSyncers",		&CONFIG_HISTSYNCER_FORKS,		TYPE_INT,
			PARM_OPT,	1,			32},
		{"StartDiscoverers",		&CONFIG_DISCOVERER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			250},
		{"StartHTTPPollers",		&CONFIG_HTTPPOLLER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			1000},
		{"StartPingers",		&CONFIG_PINGER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			1000},	
		{"StartGlbSNMPPollers",		&CONFIG_GLB_SNMP_FORKS,			TYPE_INT,
			PARM_OPT,	0,			32},
		{"SNMPMaxContention",		&CONFIG_GLB_SNMP_CONTENTION,			TYPE_INT,
			PARM_OPT,	1,			32},
		{"StartGlbAgentPollers",		&CONFIG_GLB_AGENT_FORKS,			TYPE_INT,
			PARM_OPT,	0,			10},	
		{"StartGlbPingers",		&CONFIG_GLB_PINGER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			10},
		{"StartGlbWorkers",		&CONFIG_GLB_WORKER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			10},
		{"DefaultICMPMethod",		&ICMP_METHOD_STR,			TYPE_STRING,
			PARM_OPT,	0,			0},		
		{"ValueCacheDumpLocation",		&CONFIG_VCDUMP_LOCATION,			TYPE_STRING,
			PARM_OPT,	0,			0},		
		{"ValueCacheDumpFrequency",		&CONFIG_VCDUMP_FREQUENCY,			TYPE_INT,
			PARM_OPT,	10,			SEC_PER_HOUR},			
		{"StartPreprocessorManagers",		&CONFIG_PREPROCMAN_FORKS,			TYPE_INT,
			PARM_OPT,	1,			64},
		{"StartPollers",		&CONFIG_POLLER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			1000},
		{"StartPollersUnreachable",	&CONFIG_UNREACHABLE_POLLER_FORKS,	TYPE_INT,
			PARM_OPT,	0,			1000},
		{"StartIPMIPollers",		&CONFIG_IPMIPOLLER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			1000},
		{"StartTimers",			&CONFIG_TIMER_FORKS,			TYPE_INT,
			PARM_OPT,	1,			1000},
		{"StartTrappers",		&CONFIG_TRAPPER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			1000},
		{"StartAPITrappers",		&CONFIG_API_TRAPPER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			1000},
		{"StartJavaPollers",		&CONFIG_JAVAPOLLER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			1000},
		{"StartEscalators",		&CONFIG_ESCALATOR_FORKS,		TYPE_INT,
			PARM_OPT,	1,			100},
		{"JavaGateway",			&CONFIG_JAVA_GATEWAY,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"JavaGatewayPort",		&CONFIG_JAVA_GATEWAY_PORT,		TYPE_INT,
			PARM_OPT,	1024,			32767},
		{"SNMPTrapperFile",		&CONFIG_SNMPTRAP_FILE,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"StartSNMPTrapper",		&CONFIG_SNMPTRAPPER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			1},
		{"CacheSize",			&CONFIG_CONF_CACHE_SIZE,		TYPE_UINT64,
			PARM_OPT,	128 * ZBX_KIBIBYTE,	__UINT64_C(64) * ZBX_GIBIBYTE},
		{"HistoryCacheSize",		&CONFIG_HISTORY_CACHE_SIZE,		TYPE_UINT64,
			PARM_OPT,	128 * ZBX_KIBIBYTE,	__UINT64_C(2) * ZBX_GIBIBYTE},
		{"HistoryIndexCacheSize",	&CONFIG_HISTORY_INDEX_CACHE_SIZE,	TYPE_UINT64,
			PARM_OPT,	128 * ZBX_KIBIBYTE,	__UINT64_C(2) * ZBX_GIBIBYTE},
		{"TrendCacheSize",		&CONFIG_TRENDS_CACHE_SIZE,		TYPE_UINT64,
			PARM_OPT,	128 * ZBX_KIBIBYTE,	__UINT64_C(2) * ZBX_GIBIBYTE},
		{"TrendFunctionCacheSize",	&CONFIG_TREND_FUNC_CACHE_SIZE,		TYPE_UINT64,
			PARM_OPT,	0,			__UINT64_C(2) * ZBX_GIBIBYTE},
		{"ValueCacheSize",		&CONFIG_VALUE_CACHE_SIZE,		TYPE_UINT64,
			PARM_OPT,	0,			__UINT64_C(64) * ZBX_GIBIBYTE},
		{"IPCBufferSize",		&CONFIG_IPC_BUFFER_SIZE,		TYPE_UINT64,
			PARM_OPT,	1024*1024,			__UINT64_C(64) * ZBX_GIBIBYTE},	
		{"CacheUpdateFrequency",	&CONFIG_CONFSYNCER_FREQUENCY,		TYPE_INT,
			PARM_OPT,	1,			24 * SEC_PER_HOUR},
		{"QueuesUpdateFrequency",	&CONFIG_GLB_REQUEUE_TIME,		TYPE_INT,
			PARM_OPT,	1,			SEC_PER_HOUR},
		{"HousekeepingFrequency",	&CONFIG_HOUSEKEEPING_FREQUENCY,		TYPE_INT,
			PARM_OPT,	0,			24},
		{"MaxHousekeeperDelete",	&CONFIG_MAX_HOUSEKEEPER_DELETE,		TYPE_INT,
			PARM_OPT,	0,			1000000},
		{"TmpDir",			&CONFIG_TMPDIR,				TYPE_STRING,
			PARM_OPT,	0,			0},
		{"FpingLocation",		&CONFIG_FPING_LOCATION,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"GlbmapLocation",		&CONFIG_GLBMAP_LOCATION,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"GlbmapOptions",		&CONFIG_GLBMAP_OPTIONS,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"Fping6Location",		&CONFIG_FPING6_LOCATION,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"Timeout",			&CONFIG_TIMEOUT,			TYPE_INT,
			PARM_OPT,	1,			30},
		{"TrapperTimeout",		&CONFIG_TRAPPER_TIMEOUT,		TYPE_INT,
			PARM_OPT,	1,			300},
		{"UnreachablePeriod",		&CONFIG_UNREACHABLE_PERIOD,		TYPE_INT,
			PARM_OPT,	1,			SEC_PER_HOUR},
		{"UnreachableDelay",		&CONFIG_UNREACHABLE_DELAY,		TYPE_INT,
			PARM_OPT,	1,			SEC_PER_HOUR},
		{"UnavailableDelay",		&CONFIG_UNAVAILABLE_DELAY,		TYPE_INT,
			PARM_OPT,	1,			SEC_PER_HOUR},
		{"ListenIP",			&CONFIG_LISTEN_IP,			TYPE_STRING_LIST,
			PARM_OPT,	0,			0},
		{"ListenPort",			&CONFIG_LISTEN_PORT,			TYPE_INT,
			PARM_OPT,	1024,			32767},
		{"APIListenPort",		&CONFIG_API_LISTEN_PORT,			TYPE_INT,
			PARM_OPT,	1024,			32767},
		{"SourceIP",			&CONFIG_SOURCE_IP,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"SelfMonitorIP",			&CONFIG_SELF_MONITOR_IP,			TYPE_STRING_LIST,
			PARM_OPT,	0,			0},
		{"SelfMonitorPort",			&CONFIG_SELF_MONITOR_PORT,			TYPE_INT,
			PARM_OPT,	1024,			32767},	
		{"DebugLevel",			&CONFIG_LOG_LEVEL,			TYPE_INT,
			PARM_OPT,	0,			5},
		{"PidFile",			&CONFIG_PID_FILE,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"LogType",			&CONFIG_LOG_TYPE_STR,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"LogFile",			&CONFIG_LOG_FILE,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"LogFileSize",			&CONFIG_LOG_FILE_SIZE,			TYPE_INT,
			PARM_OPT,	0,			102400},
		{"AlertScriptsPath",		&CONFIG_ALERT_SCRIPTS_PATH,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"ExternalScripts",		&CONFIG_EXTERNALSCRIPTS,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"WorkerScripts",		&CONFIG_WORKERS_DIR,		TYPE_STRING,
			PARM_OPT,	0,			0},	
		{"DBHost",			&CONFIG_DBHOST,				TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBName",			&CONFIG_DBNAME,				TYPE_STRING,
			PARM_MAND,	0,			0},
		{"DBSchema",			&CONFIG_DBSCHEMA,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBUser",			&CONFIG_DBUSER,				TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBPassword",			&CONFIG_DBPASSWORD,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"VaultToken",			&CONFIG_VAULTTOKEN,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"VaultURL",			&CONFIG_VAULTURL,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"VaultDBPath",			&CONFIG_VAULTDBPATH,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBSocket",			&CONFIG_DBSOCKET,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBPort",			&CONFIG_DBPORT,				TYPE_INT,
			PARM_OPT,	1024,			65535},
		{"DBTLSConnect",		&CONFIG_DB_TLS_CONNECT,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBTLSCertFile",		&CONFIG_DB_TLS_CERT_FILE,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBTLSKeyFile",		&CONFIG_DB_TLS_KEY_FILE,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBTLSCAFile",			&CONFIG_DB_TLS_CA_FILE,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBTLSCipher",			&CONFIG_DB_TLS_CIPHER,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBTLSCipher13",		&CONFIG_DB_TLS_CIPHER_13,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"SSHKeyLocation",		&CONFIG_SSH_KEY_LOCATION,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"LogSlowQueries",		&CONFIG_LOG_SLOW_QUERIES,		TYPE_INT,
			PARM_OPT,	0,			3600000},
		{"StartProxyPollers",		&CONFIG_PROXYPOLLER_FORKS,		TYPE_INT,
			PARM_MAND,	2,			250},
		{"ProxyConfigFrequency",	&CONFIG_PROXYCONFIG_FREQUENCY,		TYPE_INT,
			PARM_OPT,	1,			SEC_PER_WEEK},
		{"ProxyDataFrequency",		&CONFIG_PROXYDATA_FREQUENCY,		TYPE_INT,
			PARM_OPT,	1,			SEC_PER_HOUR},
		{"LoadModulePath",		&CONFIG_LOAD_MODULE_PATH,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"LoadModule",			&CONFIG_LOAD_MODULE,			TYPE_MULTISTRING,
			PARM_OPT,	0,			0},
		{"HistoryModule",			&CONFIG_HISTORY_MODULE,			TYPE_MULTISTRING,
			PARM_OPT,	0,			0},
		{"StartVMwareCollectors",	&CONFIG_VMWARE_FORKS,			TYPE_INT,
			PARM_OPT,	0,			250},
		{"VMwareFrequency",		&CONFIG_VMWARE_FREQUENCY,		TYPE_INT,
			PARM_OPT,	10,			SEC_PER_DAY},
		{"VMwarePerfFrequency",		&CONFIG_VMWARE_PERF_FREQUENCY,		TYPE_INT,
			PARM_OPT,	10,			SEC_PER_DAY},
		{"VMwareCacheSize",		&CONFIG_VMWARE_CACHE_SIZE,		TYPE_UINT64,
			PARM_OPT,	256 * ZBX_KIBIBYTE,	__UINT64_C(2) * ZBX_GIBIBYTE},
		{"VMwareTimeout",		&CONFIG_VMWARE_TIMEOUT,			TYPE_INT,
			PARM_OPT,	1,			300},
		{"AllowRoot",			&CONFIG_ALLOW_ROOT,			TYPE_INT,
			PARM_OPT,	0,			1},
		{"User",			&CONFIG_USER,				TYPE_STRING,
			PARM_OPT,	0,			0},
		{"SSLCALocation",		&CONFIG_SSL_CA_LOCATION,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"SSLCertLocation",		&CONFIG_SSL_CERT_LOCATION,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"SSLKeyLocation",		&CONFIG_SSL_KEY_LOCATION,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCAFile",			&CONFIG_TLS_CA_FILE,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCRLFile",			&CONFIG_TLS_CRL_FILE,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCertFile",			&CONFIG_TLS_CERT_FILE,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSKeyFile",			&CONFIG_TLS_KEY_FILE,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherCert13",		&CONFIG_TLS_CIPHER_CERT13,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherCert",		&CONFIG_TLS_CIPHER_CERT,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherPSK13",		&CONFIG_TLS_CIPHER_PSK13,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherPSK",		&CONFIG_TLS_CIPHER_PSK,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherAll13",		&CONFIG_TLS_CIPHER_ALL13,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherAll",		&CONFIG_TLS_CIPHER_ALL,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"SocketDir",			&CONFIG_SOCKET_PATH,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"StartAlerters",		&CONFIG_ALERTER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			100},
		{"StartPreprocessorsPerManager",		&CONFIG_PREPROCESSOR_FORKS,		TYPE_INT,
			PARM_OPT,	1,			1000},
		{"StartGLBPreprocessors",		&CONFIG_GLB_PREPROCESSOR_FORKS,		TYPE_INT,
			PARM_OPT,	1,			1000},
		{"ExportDir",			&CONFIG_EXPORT_DIR,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"ExportType",			&CONFIG_EXPORT_TYPE,			TYPE_STRING_LIST,
			PARM_OPT,	0,			0},
		{"ExportFileSize",		&CONFIG_EXPORT_FILE_SIZE,		TYPE_UINT64,
			PARM_OPT,	ZBX_MEBIBYTE,	ZBX_GIBIBYTE},
		{"StartLLDProcessors",		&CONFIG_LLDWORKER_FORKS,		TYPE_INT,
			PARM_OPT,	1,			100},
		{"StatsAllowedIP",		&CONFIG_STATS_ALLOWED_IP,		TYPE_STRING_LIST,
			PARM_OPT,	0,			0},
		{"StartHistoryPollers",		&CONFIG_HISTORYPOLLER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			1000},
		{"StartReportWriters",		&CONFIG_REPORTWRITER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			100},
		{"WebServiceURL",		&CONFIG_WEBSERVICE_URL,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"Hostname",			&CONFIG_HOSTNAME,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"ServerID",		&CONFIG_CLUSTER_SERVER_ID,			TYPE_INT,
			PARM_OPT,	0,			ZBX_CLUSTER_MAX_SERVERS-1},
		{"RerouteItems",		&CONFIG_CLUSTER_REROUTE_DATA,			TYPE_INT,
			PARM_OPT,	0,			1},	
		{"ListenBacklog",		&CONFIG_TCP_MAX_BACKLOG_SIZE,		TYPE_INT,
			PARM_OPT,	0,			INT_MAX},
		{NULL}
	};


	/* initialize multistrings */
	zbx_strarr_init(&CONFIG_LOAD_MODULE);
	zbx_strarr_init(&CONFIG_HISTORY_MODULE);

	parse_cfg_file(CONFIG_FILE, cfg, ZBX_CFG_FILE_REQUIRED, ZBX_CFG_STRICT);

	zbx_set_defaults();

	CONFIG_LOG_TYPE = zbx_get_log_type(CONFIG_LOG_TYPE_STR);

	zbx_validate_config(task);
#if defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL)
	zbx_db_validate_config();
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_validate_config();
#endif
	CONFIG_PREPROCESSOR_FORKS =	CONFIG_PREPROCESSOR_FORKS * CONFIG_PREPROCMAN_FORKS;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_free_config                                                  *
 *                                                                            *
 * Purpose: free configuration memory                                         *
 *                                                                            *
 ******************************************************************************/
static void	zbx_free_config(void)
{
	zbx_strarr_free(CONFIG_LOAD_MODULE);
	zbx_strarr_free(CONFIG_HISTORY_MODULE);
}

/******************************************************************************
 *                                                                            *
 * Function: main                                                             *
 *                                                                            *
 * Purpose: executes server processes                                         *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
int	main(int argc, char **argv)
{
	ZBX_TASK_EX	t = {ZBX_TASK_START};
	char		ch, *error = NULL;
	int		opt_c = 0, opt_r = 0;
	struct rlimit limits;

#if defined(PS_OVERWRITE_ARGV) || defined(PS_PSTAT_ARGV)
	argv = setproctitle_save_env(argc, argv);
#endif
	progname = get_program_name(argv[0]);
	
	/* parse the command-line */
	while ((char)EOF != (ch = (char)zbx_getopt_long(argc, argv, shortopts, longopts, NULL)))
	{
		switch (ch)
		{
			case 'c':
				opt_c++;
				if (NULL == CONFIG_FILE)
					CONFIG_FILE = zbx_strdup(CONFIG_FILE, zbx_optarg);
				break;
			case 'R':
				opt_r++;
				if (SUCCEED != parse_rtc_options(zbx_optarg, program_type, &t.data))
					exit(EXIT_FAILURE);

				t.task = ZBX_TASK_RUNTIME_CONTROL;
				break;
			case 'h':
				help();
				exit(EXIT_SUCCESS);
				break;
			case 'V':
				version();
				exit(EXIT_SUCCESS);
				break;
			case 'f':
				t.flags |= ZBX_TASK_FLAG_FOREGROUND;
				break;
			default:
				usage();
				exit(EXIT_FAILURE);
				break;
		}
	}

	/* every option may be specified only once */
	if (1 < opt_c || 1 < opt_r)
	{
		if (1 < opt_c)
			zbx_error("option \"-c\" or \"--config\" specified multiple times");
		if (1 < opt_r)
			zbx_error("option \"-R\" or \"--runtime-control\" specified multiple times");

		exit(EXIT_FAILURE);
	}

	/* Parameters which are not option values are invalid. The check relies on zbx_getopt_internal() which */
	/* always permutes command line arguments regardless of POSIXLY_CORRECT environment variable. */
	if (argc > zbx_optind)
	{
		int	i;

		for (i = zbx_optind; i < argc; i++)
			zbx_error("invalid parameter \"%s\"", argv[i]);

		exit(EXIT_FAILURE);
	}

	if (NULL == CONFIG_FILE)
		CONFIG_FILE = zbx_strdup(NULL, DEFAULT_CONFIG_FILE);

	
	/* required for simple checks */
	init_metrics();

	zbx_load_config(&t);

	if (ZBX_TASK_RUNTIME_CONTROL == t.task)
		exit(SUCCEED == zbx_sigusr_send(t.data) ? EXIT_SUCCESS : EXIT_FAILURE);

	zbx_initialize_events();

	//for async version we need lots of sockets 
	//to be opened. Checking for that limit
	getrlimit(RLIMIT_NOFILE,&limits);

        if (ZBX_MIN_OPEN_FILES>limits.rlim_cur ) {
	    zbx_error("WARNING!!! the system has only %ld open files limit, which is too low for ASYNC version",limits.rlim_cur);
	    zbx_error("Will try to set the limit to %d:",ZBX_DESIRED_OPEN_FILES);

	    limits.rlim_cur=ZBX_DESIRED_OPEN_FILES;
	    limits.rlim_max=ZBX_DESIRED_OPEN_FILES;

	    setrlimit(RLIMIT_NOFILE,&limits);
	    getrlimit(RLIMIT_NOFILE,&limits);

	    if (ZBX_MIN_OPEN_FILES>limits.rlim_cur ) {
		zbx_error("Couldn't set max open files to %d. Please set it manualy via ulimit -n. Exisiting now.",ZBX_DESIRED_OPEN_FILES);
		exit(EXIT_FAILURE);
	    } else {
		zbx_error("Succesifully set max open files to %d. But it's better to set it manualy via ulimit -n. ",ZBX_DESIRED_OPEN_FILES);
	    }
	}

	if (ZBX_TASK_RUNTIME_CONTROL == t.task)
		exit(SUCCEED == zbx_sigusr_send(t.data) ? EXIT_SUCCESS : EXIT_FAILURE);

	zbx_initialize_events();

	return daemon_start(CONFIG_ALLOW_ROOT, CONFIG_USER, t.flags);
}

static void	zbx_main_sigusr_handler(int flags)
{
	if (ZBX_RTC_DIAGINFO == ZBX_RTC_GET_MSG(flags))
	{
		int	scope = ZBX_RTC_GET_SCOPE(flags);

		if (ZBX_DIAGINFO_ALL == scope)
		{
			zbx_diaginfo_scope = (1 << ZBX_DIAGINFO_HISTORYCACHE) | (1 << ZBX_DIAGINFO_VALUECACHE) |
					(1 << ZBX_DIAGINFO_PREPROCESSING) | (1 << ZBX_DIAGINFO_LLD) |
					(1 << ZBX_DIAGINFO_ALERTING) | (1 << ZBX_DIAGINFO_LOCKS);
		}
		else
			zbx_diaginfo_scope = 1 << scope;
	}
	
}

static void	zbx_check_db(void)
{
	struct zbx_json	db_ver;

	zbx_json_initarray(&db_ver, ZBX_JSON_STAT_BUF_LEN);

	if (SUCCEED != DBcheck_capabilities(DBextract_version(&db_ver)) || SUCCEED != DBcheck_version())
	{
		zbx_json_free(&db_ver);
		exit(EXIT_FAILURE);
	}

	DBflush_version_requirements(db_ver.buffer);
	zbx_json_free(&db_ver);
}

int	MAIN_ZABBIX_ENTRY(int flags)
{
	zbx_socket_t	listen_sock, api_listen_sock;
	char		*error = NULL;
	int		i, db_type;

	if (0 != (flags & ZBX_TASK_FLAG_FOREGROUND))
	{
		printf("Starting Glaber Server. Version %s, based on Zabbix %s (revision %s).\nPress Ctrl+C to exit.\n\n", GLABER_VERSION,
 				ZABBIX_VERSION, ZABBIX_REVISION);
 	}
 
	if ( !CONFIG_CLUSTER_SERVER_ID) 
		printf("Staring in STANDALONE mode");
	else
		printf("Starting in CLUSTER mode");

	if (FAIL == zbx_ipc_service_init_env(CONFIG_SOCKET_PATH, &error))
	{
		zbx_error("Cannot initialize IPC services: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zbx_locks_create(&error))
	{
		zbx_error("cannot create locks: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zabbix_open_log(CONFIG_LOG_TYPE, CONFIG_LOG_LEVEL, CONFIG_LOG_FILE, &error))
	{
		zbx_error("cannot open log: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

#ifdef HAVE_NETSNMP
#	define SNMP_FEATURE_STATUS	"YES"
#else
#	define SNMP_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_OPENIPMI
#	define IPMI_FEATURE_STATUS	"YES"
#else
#	define IPMI_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_LIBCURL
#	define LIBCURL_FEATURE_STATUS	"YES"
#else
#	define LIBCURL_FEATURE_STATUS	" NO"
#endif
#if defined(HAVE_LIBCURL) && defined(HAVE_LIBXML2)
#	define VMWARE_FEATURE_STATUS	"YES"
#else
#	define VMWARE_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_SMTP_AUTHENTICATION
#	define SMTP_AUTH_FEATURE_STATUS	"YES"
#else
#	define SMTP_AUTH_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_UNIXODBC
#	define ODBC_FEATURE_STATUS	"YES"
#else
#	define ODBC_FEATURE_STATUS	" NO"
#endif
#if defined(HAVE_SSH2) || defined(HAVE_SSH)
#	define SSH_FEATURE_STATUS	"YES"
#else
#	define SSH_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_IPV6
#	define IPV6_FEATURE_STATUS	"YES"
#else
#	define IPV6_FEATURE_STATUS	" NO"
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
#	define TLS_FEATURE_STATUS	"YES"
#else
#	define TLS_FEATURE_STATUS	" NO"
#endif

	zabbix_log(LOG_LEVEL_INFORMATION, "Starting Glaber Server %s. Based on Zabbix %s (revision %s).",
			GLABER_VERSION, ZABBIX_VERSION, ZABBIX_REVISION);

	zabbix_log(LOG_LEVEL_INFORMATION, "****** Enabled features ******");
	zabbix_log(LOG_LEVEL_INFORMATION, "SNMP monitoring:           " SNMP_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "IPMI monitoring:           " IPMI_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "Web monitoring:            " LIBCURL_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "VMware monitoring:         " VMWARE_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "SMTP authentication:       " SMTP_AUTH_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "ODBC:                      " ODBC_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "SSH support:               " SSH_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "IPv6 support:              " IPV6_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "TLS support:               " TLS_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "******************************");

	zabbix_log(LOG_LEVEL_INFORMATION, "using configuration file: %s", CONFIG_FILE);

//#define HAVE_TESTS 1

#if defined(HAVE_GLB_TESTS)
	LOG_INF("Running tests");
	tests_server_run();
	LOG_INF("Finished tests - SUCCEED");
#endif

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (SUCCEED != zbx_coredump_disable())
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot disable core dump, exiting...");
		exit(EXIT_FAILURE);
	}
#endif
	
	if (FAIL == zbx_load_modules(CONFIG_LOAD_MODULE_PATH, CONFIG_LOAD_MODULE, CONFIG_TIMEOUT, 1))
	{
		zabbix_log(LOG_LEVEL_CRIT, "loading modules failed, exiting...");
		exit(EXIT_FAILURE);
	}
	
	if (SUCCEED != glb_history_init(CONFIG_HISTORY_MODULE,&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize history storage: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}
	
	zbx_free_config();
	
	if (SUCCEED != init_database_cache(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize database cache: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != init_configuration_cache(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize configuration cache: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	DC_set_debug_item(CONFIG_DEBUG_ITEM);
	DC_set_debug_trigger(CONFIG_DEBUG_TRIGGER);
	
	if (SUCCEED != init_selfmon_collector(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize self-monitoring: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (0 != CONFIG_VMWARE_FORKS && SUCCEED != zbx_vmware_init(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize VMware cache: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	
	if (FAIL == glb_state_init()) {
		zbx_error("Cannot initialize Glaber CACHE");
		exit(EXIT_FAILURE);
	}

	if (FAIL == apm_init()) {
		zbx_error("Cannot initialize internal monitoring IPC");
		exit(EXIT_FAILURE);
	}

	if (FAIL == poller_notify_ipc_init(64 * ZBX_MEBIBYTE)) {
		zbx_error("Cannot initialize Processing notify IPC");
		exit(EXIT_FAILURE);
	}
	

	if (FAIL == preproc_ipc_init(128 * ZBX_MEBIBYTE)) {
		zbx_error("Cannot initialize Processing notify IPC");
		exit(EXIT_FAILURE);
	}
	

	if (NULL != CONFIG_VCDUMP_LOCATION && FAIL == glb_state_items_load()) {
		zabbix_log(LOG_LEVEL_CRIT, "Failed to check read-write permissions on cache file %s, check permissions",CONFIG_VCDUMP_LOCATION);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zbx_create_itservices_lock(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot create IT services lock: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zbx_tfc_init(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize trends read cache: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (FAIL == zbx_export_init(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize export: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zbx_vault_init_token_from_env(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize vault token: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zbx_vault_init_db_credentials(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize database credentials from vault: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (ZBX_DB_UNKNOWN == (db_type = zbx_db_get_database_type()))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot use database \"%s\": database is not a Zabbix database",
				CONFIG_DBNAME);
		exit(EXIT_FAILURE);
	}
	else if (ZBX_DB_SERVER != db_type)
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot use database \"%s\": its \"users\" table is empty (is this the"
				" Zabbix proxy database?)", CONFIG_DBNAME);
		exit(EXIT_FAILURE);
	}

	zbx_check_db();

	DBcheck_character_set();

	if (SUCCEED != zbx_db_check_instanceid())
		exit(EXIT_FAILURE);

	threads_num = CONFIG_CONFSYNCER_FORKS + CONFIG_POLLER_FORKS
			+ CONFIG_UNREACHABLE_POLLER_FORKS + CONFIG_TRAPPER_FORKS + CONFIG_PINGER_FORKS
			+ CONFIG_GLB_SNMP_FORKS + CONFIG_GLB_AGENT_FORKS + CONFIG_API_TRAPPER_FORKS
			+ CONFIG_GLB_PINGER_FORKS + CONFIG_GLB_WORKER_FORKS +  CONFIG_EXT_SERVER_FORKS
			+ CONFIG_ALERTER_FORKS + CONFIG_HOUSEKEEPER_FORKS + CONFIG_TIMER_FORKS
			+ CONFIG_HTTPPOLLER_FORKS + CONFIG_DISCOVERER_FORKS + CONFIG_HISTSYNCER_FORKS
			+ CONFIG_ESCALATOR_FORKS + CONFIG_IPMIPOLLER_FORKS + CONFIG_JAVAPOLLER_FORKS
			+ CONFIG_SNMPTRAPPER_FORKS + CONFIG_PROXYPOLLER_FORKS + CONFIG_SELFMON_FORKS
			+ CONFIG_VMWARE_FORKS + CONFIG_TASKMANAGER_FORKS + CONFIG_IPMIMANAGER_FORKS
			+ CONFIG_ALERTMANAGER_FORKS + CONFIG_PREPROCMAN_FORKS + CONFIG_PREPROCESSOR_FORKS
			+ CONFIG_GLB_PREPROCESSOR_FORKS
			+ CONFIG_LLDMANAGER_FORKS + CONFIG_LLDWORKER_FORKS + CONFIG_ALERTDB_FORKS
			+ CONFIG_HISTORYPOLLER_FORKS + CONFIG_AVAILMAN_FORKS + CONFIG_REPORTMANAGER_FORKS
			+ CONFIG_REPORTWRITER_FORKS;
	threads = (pid_t *)zbx_calloc(threads, threads_num, sizeof(pid_t));
	threads_flags = (int *)zbx_calloc(threads_flags, threads_num, sizeof(int));

	if (0 != CONFIG_TRAPPER_FORKS)
	{
		if (FAIL == zbx_tcp_listen(&listen_sock, CONFIG_LISTEN_IP, (unsigned short)CONFIG_LISTEN_PORT))
		{
			zabbix_log(LOG_LEVEL_CRIT, "listener failed: %s", zbx_socket_strerror());
			exit(EXIT_FAILURE);
		}
	}

	if (0 != CONFIG_API_TRAPPER_FORKS)
	{
		if (FAIL == zbx_tcp_listen(&api_listen_sock, CONFIG_LISTEN_IP, (unsigned short)CONFIG_API_LISTEN_PORT))
		{
			zabbix_log(LOG_LEVEL_CRIT, "API listener failed: %s", zbx_socket_strerror());
			exit(EXIT_FAILURE);
		}
	}
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_init_parent();
#endif
	zabbix_log(LOG_LEVEL_INFORMATION, "server #0 started [main process]");

	for (i = 0; i < threads_num; i++)
	{
		zbx_thread_args_t	thread_args;
		unsigned char		poller_type;

		if (FAIL == get_process_info_by_thread(i + 1, &thread_args.process_type, &thread_args.process_num))
		{
			zabbix_log(LOG_LEVEL_WARNING, "Couldn't get process info by thread num: %d",i);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		thread_args.server_num = i + 1;
		thread_args.args = NULL;

		switch (thread_args.process_type)
		{
			case ZBX_PROCESS_TYPE_CONFSYNCER:
				zbx_thread_start(dbconfig_thread, &thread_args, &threads[i]);
				DCconfig_wait_sync();

				DBconnect(ZBX_DB_CONNECT_NORMAL);

				if (SUCCEED != zbx_check_postinit_tasks(&error))
				{
					zabbix_log(LOG_LEVEL_CRIT, "cannot complete post initialization tasks: %s",
							error);
					zbx_free(error);
					exit(EXIT_FAILURE);
				}

				/* update maintenance states */
				zbx_dc_update_maintenances();

				DBclose();

//				zbx_vc_enable();
				break;
			case ZBX_PROCESS_TYPE_POLLER:
				poller_type = ZBX_POLLER_TYPE_NORMAL;
				thread_args.args = &poller_type;
				zbx_thread_start(poller_thread, &thread_args, &threads[i]);
				break;
			case GLB_PROCESS_TYPE_SNMP:
				poller_type = ITEM_TYPE_SNMP;
				thread_args.args = &poller_type;
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_PINGER:
				poller_type = ITEM_TYPE_SIMPLE;
				thread_args.args = &poller_type;
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_WORKER:
				poller_type = ITEM_TYPE_EXTERNAL;
				thread_args.args = &poller_type;
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_SERVER:
				poller_type = ITEM_TYPE_WORKER_SERVER;
				thread_args.args = &poller_type;
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_AGENT:
				poller_type = ITEM_TYPE_ZABBIX;
				thread_args.args = &poller_type;
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;		
			case ZBX_PROCESS_TYPE_UNREACHABLE:
				poller_type = ZBX_POLLER_TYPE_UNREACHABLE;
				thread_args.args = &poller_type;
				zbx_thread_start(poller_thread, &thread_args, &threads[i]);
				break;
			case GLB_PROCESS_TYPE_API_TRAPPER:
				thread_args.args = &api_listen_sock;
				zbx_thread_start(trapper_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_TRAPPER:
				thread_args.args = &listen_sock;
				zbx_thread_start(trapper_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_PINGER:
				zbx_thread_start(pinger_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_ALERTER:
				zbx_thread_start(alerter_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_HOUSEKEEPER:
				zbx_thread_start(housekeeper_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_TIMER:
				zbx_thread_start(timer_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_HTTPPOLLER:
				zbx_thread_start(httppoller_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_DISCOVERER:
				zbx_thread_start(discoverer_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_HISTSYNCER:
				threads_flags[i] = ZBX_THREAD_WAIT_EXIT;
				zbx_thread_start(dbsyncer_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_ESCALATOR:
				zbx_thread_start(escalator_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_JAVAPOLLER:
				poller_type = ZBX_POLLER_TYPE_JAVA;
				thread_args.args = &poller_type;
				zbx_thread_start(poller_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_SNMPTRAPPER:
				zbx_thread_start(snmptrapper_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_PROXYPOLLER:
				zbx_thread_start(proxypoller_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_SELFMON:
				zbx_thread_start(selfmon_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_VMWARE:
				zbx_thread_start(vmware_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_TASKMANAGER:
				zbx_thread_start(taskmanager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_PREPROCMAN:
				zbx_thread_start(preprocessing_manager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_PREPROCESSOR:
				zbx_thread_start(preprocessing_worker_thread, &thread_args, &threads[i]);
				break;
			case GLB_PROCESS_TYPE_PREPROCESSOR:
				zbx_thread_start(glb_preprocessing_worker_thread, &thread_args, &threads[i]);
				break;
#ifdef HAVE_OPENIPMI
			case ZBX_PROCESS_TYPE_IPMIMANAGER:
				zbx_thread_start(ipmi_manager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_IPMIPOLLER:
				zbx_thread_start(ipmi_poller_thread, &thread_args, &threads[i]);
				break;
#endif
			case ZBX_PROCESS_TYPE_ALERTMANAGER:
				zbx_thread_start(alert_manager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_LLDMANAGER:
				zbx_thread_start(lld_manager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_LLDWORKER:
				zbx_thread_start(lld_worker_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_ALERTSYNCER:
				zbx_thread_start(alert_syncer_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_HISTORYPOLLER:
			//	poller_type = ZBX_POLLER_TYPE_HISTORY;
				poller_type = ITEM_TYPE_CALCULATED;
				thread_args.args = &poller_type;
				//zbx_thread_start(poller_thread, &thread_args, &threads[i]);
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_AVAILMAN:
				threads_flags[i] = ZBX_THREAD_WAIT_EXIT;
				zbx_thread_start(availability_manager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_REPORTMANAGER:
				zbx_thread_start(report_manager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_REPORTWRITER:
				zbx_thread_start(report_writer_thread, &thread_args, &threads[i]);
				break;
		}
	}

	if (SUCCEED == zbx_is_export_enabled(ZBX_FLAG_EXPTYPE_EVENTS))
		zbx_problems_export_init("main-process", 0);

	if (SUCCEED == zbx_is_export_enabled(ZBX_FLAG_EXPTYPE_HISTORY))
		zbx_history_export_init("main-process", 0);

	if (SUCCEED == zbx_is_export_enabled(ZBX_FLAG_EXPTYPE_TRENDS))
		zbx_trends_export_init("main-process", 0);


	zbx_set_sigusr_handler(zbx_main_sigusr_handler);

	while (-1 == wait(&i))	/* wait for any child to exit */
	{
		if (EINTR != errno)
		{
			zabbix_log(LOG_LEVEL_ERR, "failed to wait on child processes: %s", zbx_strerror(errno));
			break;
		}

		/* check if the wait was interrupted because of diaginfo remote command */
		if (ZBX_DIAGINFO_UNDEFINED != zbx_diaginfo_scope)
		{
			zbx_diag_log_info(zbx_diaginfo_scope);
			zbx_diaginfo_scope = ZBX_DIAGINFO_UNDEFINED;
		}
	}

	/* all exiting child processes should be caught by signal handlers */
	THIS_SHOULD_NEVER_HAPPEN;

	zbx_on_exit(FAIL);

	return SUCCEED;
}

void	zbx_on_exit(int ret)
{
	zabbix_log(LOG_LEVEL_DEBUG, "zbx_on_exit() called");

	if (SUCCEED == DBtxn_ongoing())
		DBrollback();

	if (NULL != threads)
	{
		zbx_threads_wait(threads, threads_flags, threads_num, ret);	/* wait for all child processes to exit */
		zbx_free(threads);
		zbx_free(threads_flags);
	}
#ifdef HAVE_PTHREAD_PROCESS_SHARED
	zbx_locks_disable();
#endif
	free_metrics();
	zbx_ipc_service_free_env();

	DBconnect(ZBX_DB_CONNECT_EXIT);

	free_database_cache();

	DBclose();

	free_configuration_cache();

	/* free history value cache */
//	zbx_vc_destroy();
	
	glb_state_destroy();

	zbx_destroy_itservices_lock();

	/* free vmware support */
	if (0 != CONFIG_VMWARE_FORKS)
		zbx_vmware_destroy();

	free_selfmon_collector();

	zbx_uninitialize_events();

	zbx_unload_modules();

	//i see this too frequently during dev, so let it be glaber
	//zabbix_log(LOG_LEVEL_INFORMATION, "Zabbix Server stopped. Zabbix %s (revision %s).",
	//		ZABBIX_VERSION, ZABBIX_REVISION);
	zabbix_log(LOG_LEVEL_INFORMATION, "Glaber Server stopped. Glaber %s (revision %s).",
		GLABER_VERSION,ZABBIX_REVISION);

	zabbix_close_log();

	//freeing callbacks 
	int i,j;
	for ( i=0; i<GLB_MODULE_API_TOTAL_CALLBACKS; i++) {
		//freeing all the callback structs
		for (j=0; j < API_CALLBACKS[i]->values_num; j++) {
			zbx_free(API_CALLBACKS[i]->values[j]);
		}
		//freeing vector
		zbx_vector_ptr_destroy(API_CALLBACKS[i]);
	}


#if defined(PS_OVERWRITE_ARGV)
	setproctitle_free_env();
#endif

	exit(EXIT_SUCCESS);
}
