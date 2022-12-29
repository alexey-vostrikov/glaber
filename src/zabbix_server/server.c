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

#include "config.h"

#ifdef HAVE_SQLITE3
#	error SQLite is not supported as a main Zabbix database backend.
#endif

#include "zbxexport.h"
#include "zbxself.h"

#include "cfg.h"
#include "zbxdbupgrade.h"
#include "log.h"
#include "zbxgetopt.h"
#include "zbxmutexs.h"
#include "zbxmodules.h"
#include "zbxnix.h"
#include "zbxcomms.h"

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
#include "vmware/vmware.h"
#include "taskmanager/taskmanager.h"
#include "preprocessor/preproc_manager.h"
#include "preprocessor/preproc_worker.h"
#include "availability/avail_manager.h"
#include "service/service_manager.h"
#include "housekeeper/trigger_housekeeper.h"
#include "lld/lld_manager.h"
#include "lld/lld_worker.h"
#include "reporter/report_manager.h"
#include "reporter/report_writer.h"
#include "events.h"
#include "preprocessor/glb_preproc_worker.h"
#include "glb_preproc.h"
#include "tests/server_tests.h"
#include "setproctitle.h"
#include "zbxhistory.h"
#include "postinit.h"
#include "../libs/zbxvault/vault.h"
#include "zbxtrends.h"
#include "ha/ha.h"
#include "zbxrtc.h"
#include "rtc/rtc_server.h"
#include "zbxha.h"
#include "zbxstats.h"
#include "stats/zabbix_stats.h"
#include "zbxdiag.h"
#include "diag/diag_server.h"
#include "zbxip.h"
#include "zbxsysinfo.h"
#include "zbx_rtc_constants.h"
#include "zbxthreads.h"
#include "../libs/zbxexec/worker.h"
#include "../libs/zbxipcservice/glb_ipc.h"
#include "../libs/glb_state/glb_state.h"
#include "../libs/glb_state/glb_state_items.h"
#include "../libs/apm/apm.h"

#if defined(HAVE_GLB_TESTS)
#include "./tests/server_tests.h"
#endif

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
	"      " ZBX_CONFIG_CACHE_RELOAD "             Reload configuration cache",
	"      " ZBX_HOUSEKEEPER_EXECUTE "             Execute the housekeeper",
	"      " ZBX_TRIGGER_HOUSEKEEPER_EXECUTE "     Execute the trigger housekeeper",
	"      " ZBX_LOG_LEVEL_INCREASE "=target       Increase log level, affects all processes if",
	"                                        target is not specified",
	"      " ZBX_LOG_LEVEL_DECREASE "=target       Decrease log level, affects all processes if",
	"                                        target is not specified",
	"      " ZBX_SNMP_CACHE_RELOAD "               Reload SNMP cache",
	"      " ZBX_SECRETS_RELOAD "                  Reload secrets from Vault",
	"      " ZBX_DIAGINFO "=section                Log internal diagnostic information of the",
	"                                        section (historycache, preprocessing, alerting,",
	"                                        lld, valuecache, locks) or everything if section is",
	"                                        not specified",
	"      " ZBX_SERVICE_CACHE_RELOAD "             Reload service manager cache",
	"      " ZBX_HA_STATUS "                        Display HA cluster status",
	"      " ZBX_HA_REMOVE_NODE "=target            Remove the HA node specified by its name or ID",
	"      " ZBX_HA_SET_FAILOVER_DELAY "=delay      Set HA failover delay",
	"      " ZBX_PROXY_CONFIG_CACHE_RELOAD "[=name] Reload configuration cache on proxy by its name,",
	"                                        comma-separated list can be used to pass multiple names.",
	"                                        All proxies will be reloaded if no names were specified.",
	"",
	"      Log level control targets:",
	"        process-type              All processes of specified type",
	"                                  (alerter, alert manager, configuration syncer,",
	"                                  discoverer, escalator, ha manager, history syncer,",
	"                                  housekeeper, http poller, icmp pinger,",
	"                                  ipmi manager, ipmi poller, java poller,",
	"                                  poller, preprocessing manager,",
	"                                  preprocessing worker, proxy poller,",
	"                                  self-monitoring, snmp trapper, task manager,",
	"                                  timer, trapper, unreachable poller,",
	"                                  vmware collector, history poller,",
	"                                  availability manager, service manager, odbc poller)",
	"        process-type,N            Process type and number (e.g., poller,3)",
	"        pid                       Process identifier",
	"",
	"  -h --help                       Display this help message",
	"  -V --version                    Display version number",
	"",
	"Some configuration parameter default locations:",
	"  AlertScriptsPath                \"" DEFAULT_ALERT_SCRIPTS_PATH "\"",
	"  ExternalScripts                 \"" DEFAULT_EXTERNAL_SCRIPTS_PATH "\"",
	"  WorkerScripts                   \"" DEFAULT_EXTERNAL_SCRIPTS_PATH "\"",
#ifdef HAVE_LIBCURL
	"  SSLCertLocation                 \"" DEFAULT_SSL_CERT_LOCATION "\"",
	"  SSLKeyLocation                  \"" DEFAULT_SSL_KEY_LOCATION "\"",
#endif
	"  LoadModulePath                  \"" DEFAULT_LOAD_MODULE_PATH "\"",
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

static int	ha_status = ZBX_NODE_STATUS_UNKNOWN;
static int	ha_failover_delay = ZBX_HA_DEFAULT_FAILOVER_DELAY;
zbx_cuid_t	ha_sessionid;
static char	*CONFIG_PID_FILE = NULL;

unsigned char	program_type = ZBX_PROGRAM_TYPE_SERVER;
static unsigned char	get_program_type(void)
{
	return program_type;
}

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
int CONFIG_EXT_SERVER_FORKS = 1;

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
int	CONFIG_SERVICEMAN_FORKS		= 1;
int	CONFIG_TRIGGERHOUSEKEEPER_FORKS = 1;
int	CONFIG_ODBCPOLLER_FORKS		= 1;

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
int	CONFIG_CONFSYNCER_FREQUENCY	= 10;

int	CONFIG_PROBLEMHOUSEKEEPING_FREQUENCY = 60;

int	CONFIG_VMWARE_FORKS		= 0;
int	CONFIG_VMWARE_FREQUENCY		= 60;
int	CONFIG_VMWARE_PERF_FREQUENCY	= 60;
int	CONFIG_VMWARE_TIMEOUT		= 10;

zbx_uint64_t	CONFIG_CONF_CACHE_SIZE		= 32 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_HISTORY_CACHE_SIZE	= 16 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_HISTORY_INDEX_CACHE_SIZE	= 4 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_TRENDS_CACHE_SIZE	= 4 * ZBX_MEBIBYTE;
static zbx_uint64_t	CONFIG_TREND_FUNC_CACHE_SIZE	= 4 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_VALUE_CACHE_SIZE		= 512 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_VMWARE_CACHE_SIZE	= 8 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_EXPORT_FILE_SIZE		= ZBX_GIBIBYTE;
u_int64_t 		CONFIG_IPC_BUFFER_SIZE		= 128 * ZBX_MEBIBYTE;

int CONFIG_SELF_MONITOR_PORT		= DEFAULT_SELF_MONITOR_PORT;
char	*CONFIG_SELF_MONITOR_IP		= NULL;

char 	**CONFIG_HISTORY_MODULE		= NULL;
int		CONFIG_SNMP_RETRIES		=	2;

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
char	*CONFIG_VAULT			= NULL;
char	*CONFIG_VAULTURL		= NULL;
char	*CONFIG_VAULTTOKEN		= NULL;
char	*CONFIG_VAULTTLSCERTFILE	= NULL;
char	*CONFIG_VAULTTLSKEYFILE		= NULL;
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
int	CONFIG_ALLOW_UNSUPPORTED_DB_VERSIONS = 0;
int	CONFIG_ENABLE_REMOTE_COMMANDS	= 0;
int	CONFIG_LOG_REMOTE_COMMANDS	= 0;
int	CONFIG_UNSAFE_USER_PARAMETERS	= 0;
int CONFIG_DISABLE_SNMPV1_ASYNC = 0;

char	*CONFIG_SNMPTRAP_FILE		= NULL;
char 	*ICMP_METHOD_STR = NULL;

char	*CONFIG_JAVA_GATEWAY		= NULL;
int	CONFIG_JAVA_GATEWAY_PORT	= ZBX_DEFAULT_GATEWAY_PORT;

char	*CONFIG_SSH_KEY_LOCATION	= NULL;

int	CONFIG_LOG_SLOW_QUERIES		= 0;	/* ms; 0 - disable */

int	CONFIG_SERVER_STARTUP_TIME	= 0;	/* zabbix server startup time */

int	CONFIG_PROXYPOLLER_FORKS	= 1;	/* parameters for passive proxies */

/* how often Zabbix server sends configuration data to passive proxy, in seconds */
int	CONFIG_PROXYCONFIG_FREQUENCY	= 10;
int	CONFIG_PROXYDATA_FREQUENCY	= 1;	/* 1s */

char	*CONFIG_LOAD_MODULE_PATH	= NULL;
char	**CONFIG_LOAD_MODULE		= NULL;

char	*CONFIG_USER			= NULL;

/* web monitoring */
char	*CONFIG_SSL_CA_LOCATION		= NULL;
char	*CONFIG_SSL_CERT_LOCATION	= NULL;
char	*CONFIG_SSL_KEY_LOCATION	= NULL;

static zbx_config_tls_t	*zbx_config_tls = NULL;

char	*CONFIG_HA_NODE_NAME		= NULL;
char	*CONFIG_NODE_ADDRESS	= NULL;

static char	*CONFIG_SOCKET_PATH	= NULL;
char	CONFIG_CLUSTER_DOMAINS[ZBX_MAX_HOSTNAME_LEN];
int 	CONFIG_CLUSTER_SERVER_ID =0;
int		CONFIG_CLUSTER_REROUTE_DATA	= 1;
zbx_vector_ptr_t *API_CALLBACKS[GLB_MODULE_API_TOTAL_CALLBACKS];

char	CONFIG_HOSTNAME[ZBX_MAX_HOSTNAME_LEN];

char	*CONFIG_STATS_ALLOWED_IP	= NULL;
int	CONFIG_TCP_MAX_BACKLOG_SIZE	= SOMAXCONN;

int	CONFIG_DOUBLE_PRECISION		= ZBX_DB_DBL_PRECISION_ENABLED;

char	*CONFIG_WEBSERVICE_URL	= NULL;

int	CONFIG_SERVICEMAN_SYNC_FREQUENCY	= 60;

static char	*config_file		= NULL;
static int	config_allow_root	= 0;

struct zbx_db_version_info_t	db_version_info;

int	get_process_info_by_thread(int local_server_num, unsigned char *local_process_type, int *local_process_num);

int	get_process_info_by_thread(int local_server_num, unsigned char *local_process_type, int *local_process_num)
{
	int	server_count = 0;

	if (0 == local_server_num)
	{
		/* fail if the main process is queried */
		return FAIL;
	}
	else if (local_server_num <= (server_count += CONFIG_SERVICEMAN_FORKS))
	{
		/* start service manager process and load configuration cache in parallel */
		*local_process_type = ZBX_PROCESS_TYPE_SERVICEMAN;
		*local_process_num = local_server_num - server_count + CONFIG_SERVICEMAN_FORKS;
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
	else if (local_server_num <= (server_count += CONFIG_TRIGGERHOUSEKEEPER_FORKS))
	{
		/* start service manager process and load configuration cache in parallel */
		*local_process_type = ZBX_PROCESS_TYPE_TRIGGERHOUSEKEEPER;
		*local_process_num = local_server_num - server_count + CONFIG_TRIGGERHOUSEKEEPER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_ODBCPOLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_ODBCPOLLER;
		*local_process_num = local_server_num - server_count + CONFIG_ODBCPOLLER_FORKS;
	}
	else
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: set configuration defaults                                        *
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
		CONFIG_WORKERS_DIR = zbx_strdup(CONFIG_WORKERS_DIR, DEFAULT_EXTERNAL_SCRIPTS_PATH);
			
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

	int i;

	for ( i = 0; i < GLB_MODULE_API_TOTAL_CALLBACKS ; i++ ) {
		API_CALLBACKS[i]=zbx_malloc(NULL, sizeof(zbx_vector_ptr_t));
		zbx_vector_ptr_create(API_CALLBACKS[i]);
	}
	
	if ( NULL != ICMP_METHOD_STR && NULL != strstr(ICMP_METHOD_STR,ZBX_ICMP_NAME) ) {
			zabbix_log(LOG_LEVEL_DEBUG, "Setting ICMP method to Zabbix ICMP (fping)");
		CONFIG_ICMP_METHOD = ZBX_ICMP;
	} else {
		zabbix_log(LOG_LEVEL_DEBUG, "Setting ICMP method to Glaber ICMP (async + glbmap)");
	}

	if (0 != CONFIG_IPMIPOLLER_FORKS)
		CONFIG_IPMIMANAGER_FORKS = 1;

	if (NULL == CONFIG_VAULTURL)
		CONFIG_VAULTURL = zbx_strdup(CONFIG_VAULTURL, "https://127.0.0.1:8200");

	if (0 != CONFIG_REPORTWRITER_FORKS)
		CONFIG_REPORTMANAGER_FORKS = 1;

	if (NULL == CONFIG_NODE_ADDRESS)
		CONFIG_NODE_ADDRESS = zbx_strdup(CONFIG_NODE_ADDRESS, "localhost");
}

/******************************************************************************
 *                                                                            *
 * Purpose: validate configuration parameters                                 *
 *                                                                            *
 ******************************************************************************/
static void	zbx_validate_config(ZBX_TASK_EX *task)
{
	char		*ch_error, *address = NULL;
	int		err = 0;
	unsigned short	port;

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

	if (NULL != CONFIG_SOURCE_IP && SUCCEED != zbx_is_supported_ip(CONFIG_SOURCE_IP))
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

	if (SUCCEED != zbx_validate_export_type(CONFIG_EXPORT_TYPE, NULL))
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid \"ExportType\" configuration parameter: %s", CONFIG_EXPORT_TYPE);
		err = 1;
	}

	if (FAIL == zbx_parse_serveractive_element(CONFIG_NODE_ADDRESS, &address, &port, 10051) ||
			(FAIL == zbx_is_supported_ip(address) && FAIL == zbx_validate_hostname(address)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid \"NodeAddress\" configuration parameter: address \"%s\""
				" is invalid", CONFIG_NODE_ADDRESS);
		err = 1;
	}
	zbx_free(address);

#if !defined(HAVE_IPV6)
	err |= (FAIL == check_cfg_feature_str("Fping6Location", CONFIG_FPING6_LOCATION, "IPv6 support"));
#endif
#if !defined(HAVE_LIBCURL)
	err |= (FAIL == check_cfg_feature_str("SSLCALocation", CONFIG_SSL_CA_LOCATION, "cURL library"));
	err |= (FAIL == check_cfg_feature_str("SSLCertLocation", CONFIG_SSL_CERT_LOCATION, "cURL library"));
	err |= (FAIL == check_cfg_feature_str("SSLKeyLocation", CONFIG_SSL_KEY_LOCATION, "cURL library"));
	err |= (FAIL == check_cfg_feature_int("HistoryStorageDateIndex", CONFIG_HISTORY_STORAGE_PIPELINES,
			"cURL library"));
	err |= (FAIL == check_cfg_feature_str("Vault", CONFIG_VAULT, "cURL library"));
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
	err |= (FAIL == check_cfg_feature_str("TLSCAFile", zbx_config_tls->ca_file, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSCRLFile", zbx_config_tls->crl_file, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSCertFile", zbx_config_tls->cert_file, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSKeyFile", zbx_config_tls->key_file, "TLS support"));
#endif
#if !(defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	err |= (FAIL == check_cfg_feature_str("TLSCipherCert", zbx_config_tls->cipher_cert,
			"GnuTLS or OpenSSL"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherPSK", zbx_config_tls->cipher_psk,
			"GnuTLS or OpenSSL"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherAll", zbx_config_tls->cipher_all,
			"GnuTLS or OpenSSL"));
#endif
#if !defined(HAVE_OPENSSL)
	err |= (FAIL == check_cfg_feature_str("TLSCipherCert13", zbx_config_tls->cipher_cert13,
			"OpenSSL 1.1.1 or newer"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherPSK13", zbx_config_tls->cipher_psk13,
			"OpenSSL 1.1.1 or newer"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherAll13", zbx_config_tls->cipher_all13,
			"OpenSSL 1.1.1 or newer"));
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
 * Purpose: parse config file and update configuration parameters             *
 *                                                                            *
 * Comments: will terminate process if parsing fails                          *
 *                                                                            *
 ******************************************************************************/
static void	zbx_load_config(ZBX_TASK_EX *task)
{
	struct cfg_line	cfg[] =
	{
		/* PARAMETER,			VAR,					TYPE,
			MANDATORY,	MIN,			MAX */
		{"SnmpDisableSNMPV1Async",			&CONFIG_DISABLE_SNMPV1_ASYNC,			TYPE_INT,
			PARM_OPT,	0,			1},
		{"SnmpRetries",			&CONFIG_SNMP_RETRIES,			TYPE_INT,
 			PARM_OPT,	1,			100},
		{"DebugItem",			&CONFIG_DEBUG_ITEM,			TYPE_INT,
			PARM_OPT,	0,			0},
		{"DebugTrigger",			&CONFIG_DEBUG_TRIGGER,			TYPE_INT,
			PARM_OPT,	0,			0},
		{"EnableHostDeactivation",			&CONFIG_ENABLE_HOST_DEACTIVATION,			TYPE_INT,
			PARM_OPT,	0,			0},
		{"StartWorkerServers",		&CONFIG_EXT_SERVER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			8},
		{"StartGlbSNMPPollers",		&CONFIG_GLB_SNMP_FORKS,			TYPE_INT,
			PARM_OPT,	0,			32},
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
		{"StartDBSyncers",		&CONFIG_HISTSYNCER_FORKS,		TYPE_INT,
			PARM_OPT,	1,			100},
		{"StartDiscoverers",		&CONFIG_DISCOVERER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			250},
		{"StartHTTPPollers",		&CONFIG_HTTPPOLLER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			1000},
		{"StartPingers",		&CONFIG_PINGER_FORKS,			TYPE_INT,
			PARM_OPT,	0,			1000},
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
		{"Vault",			&CONFIG_VAULT,				TYPE_STRING,
			PARM_OPT,	0,			0},
		{"VaultTLSCertFile",		&CONFIG_VAULTTLSCERTFILE,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"VaultTLSKeyFile",		&CONFIG_VAULTTLSKEYFILE,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"VaultURL",			&CONFIG_VAULTURL,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"VaultDBPath",			&CONFIG_VAULTDBPATH,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBSocket",			&CONFIG_DBSOCKET,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"DBPort",			&CONFIG_DBPORT,				TYPE_INT,
			PARM_OPT,	1024,			65535},
		{"AllowUnsupportedDBVersions",	&CONFIG_ALLOW_UNSUPPORTED_DB_VERSIONS,	TYPE_INT,
			PARM_OPT,	0,			1},
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
		{"AllowRoot",			&config_allow_root,			TYPE_INT,
			PARM_OPT,	0,			1},
		{"User",			&CONFIG_USER,				TYPE_STRING,
			PARM_OPT,	0,			0},
		{"SSLCALocation",		&CONFIG_SSL_CA_LOCATION,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"SSLCertLocation",		&CONFIG_SSL_CERT_LOCATION,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"SSLKeyLocation",		&CONFIG_SSL_KEY_LOCATION,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCAFile",			&(zbx_config_tls->ca_file),		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCRLFile",			&(zbx_config_tls->crl_file),		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCertFile",			&(zbx_config_tls->cert_file),		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSKeyFile",			&(zbx_config_tls->key_file),		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherCert13",		&(zbx_config_tls->cipher_cert13),	TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherCert",		&(zbx_config_tls->cipher_cert),		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherPSK13",		&(zbx_config_tls->cipher_psk13),	TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherPSK",		&(zbx_config_tls->cipher_psk),		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherAll13",		&(zbx_config_tls->cipher_all13),	TYPE_STRING,
			PARM_OPT,	0,			0},
		{"TLSCipherAll",		&(zbx_config_tls->cipher_all),		TYPE_STRING,
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
		{"ProblemHousekeepingFrequency",	&CONFIG_PROBLEMHOUSEKEEPING_FREQUENCY,	TYPE_INT,
			PARM_OPT,	1,			3600},
		{"ServiceManagerSyncFrequency",	&CONFIG_SERVICEMAN_SYNC_FREQUENCY,	TYPE_INT,
			PARM_OPT,	1,			3600},
		{"ListenBacklog",		&CONFIG_TCP_MAX_BACKLOG_SIZE,		TYPE_INT,
			PARM_OPT,	0,			INT_MAX},
		{"HANodeName",			&CONFIG_HA_NODE_NAME,			TYPE_STRING,
			PARM_OPT,	0,			0},
		{"NodeAddress",			&CONFIG_NODE_ADDRESS,		TYPE_STRING,
			PARM_OPT,	0,			0},
		{"StartODBCPollers",		&CONFIG_ODBCPOLLER_FORKS,		TYPE_INT,
			PARM_OPT,	0,			1000},
		{NULL}
	};

	/* initialize multistrings */
	zbx_strarr_init(&CONFIG_LOAD_MODULE);
	zbx_strarr_init(&CONFIG_HISTORY_MODULE);

	parse_cfg_file(config_file, cfg, ZBX_CFG_FILE_REQUIRED, ZBX_CFG_STRICT, ZBX_CFG_EXIT_FAILURE);
	zbx_set_defaults();

	CONFIG_LOG_TYPE = zbx_get_log_type(CONFIG_LOG_TYPE_STR);

	zbx_validate_config(task);
#if defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL)
	zbx_db_validate_config();
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_validate_config(zbx_config_tls, CONFIG_ACTIVE_FORKS, CONFIG_PASSIVE_FORKS, get_program_type);
#endif
	CONFIG_PREPROCESSOR_FORKS =	CONFIG_PREPROCESSOR_FORKS * CONFIG_PREPROCMAN_FORKS;
}

/******************************************************************************
 *                                                                            *
 * Purpose: free configuration memory                                         *
 *                                                                            *
 ******************************************************************************/
static void	zbx_free_config(void)
{
	zbx_strarr_free(&CONFIG_LOAD_MODULE);
}

/******************************************************************************
 *                                                                            *
 * Purpose: callback function for providing PID file path to libraries        *
 *                                                                            *
 ******************************************************************************/
static const char	*get_pid_file_path(void)
{
	return CONFIG_PID_FILE;
}

static void	zbx_on_exit(int ret)
{
	char	*error = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "zbx_on_exit() called with ret:%d", ret);

	if (NULL != threads)
	{
		zbx_threads_wait(threads, threads_flags, threads_num, ret);	/* wait for all child processes to exit */
		zbx_free(threads);
		zbx_free(threads_flags);
	}

#ifdef HAVE_PTHREAD_PROCESS_SHARED
		zbx_locks_disable();
#endif

	if (ZBX_NODE_STATUS_ACTIVE == ha_status)
	{
		DBconnect(ZBX_DB_CONNECT_EXIT);
		free_database_cache(ZBX_SYNC_ALL);
		DBclose();
	}

	if (SUCCEED != zbx_ha_stop(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot stop HA manager: %s", error);
		zbx_free(error);
		zbx_ha_kill();
	}

	if (ZBX_NODE_STATUS_ACTIVE == ha_status)
	{
		zbx_free_metrics();
		zbx_ipc_service_free_env();
		free_configuration_cache();

		/* free state cache */
		glb_state_destroy();

		/* free vmware support */
		zbx_vmware_destroy();

		zbx_free_selfmon_collector();
	}

	zbx_uninitialize_events();

	zbx_unload_modules();

	int i,j;
	for ( i=0; i<GLB_MODULE_API_TOTAL_CALLBACKS; i++) {
		for (j=0; j < API_CALLBACKS[i]->values_num; j++) {
			zbx_free(API_CALLBACKS[i]->values[j]);
		}
		zbx_vector_ptr_destroy(API_CALLBACKS[i]);
	}

	zabbix_log(LOG_LEVEL_INFORMATION, "Glaber Server stopped. Glaber %s (revision %s).",
		GLABER_VERSION,ZABBIX_REVISION);

	zabbix_close_log();

	zbx_locks_destroy();

#if defined(PS_OVERWRITE_ARGV)
	setproctitle_free_env();
#endif

	zbx_config_tls_free(zbx_config_tls);

	exit(EXIT_SUCCESS);
}

/******************************************************************************
 *                                                                            *
 * Purpose: executes server processes                                         *
 *                                                                            *
 ******************************************************************************/
int	main(int argc, char **argv)
{
	ZBX_TASK_EX	t = {ZBX_TASK_START};
	char	ch, *error = NULL;
	int		opt_c = 0, opt_r = 0;
	struct rlimit limits;

	/* see description of 'optarg' in 'man 3 getopt' */
	char		*zbx_optarg = NULL;

	/* see description of 'optind' in 'man 3 getopt' */
	int		zbx_optind = 0;

	zbx_config_tls = zbx_config_tls_new();
#if defined(PS_OVERWRITE_ARGV) || defined(PS_PSTAT_ARGV)
	argv = setproctitle_save_env(argc, argv);
#endif
	progname = get_program_name(argv[0]);

	/* parse the command-line */
	while ((char)EOF != (ch = (char)zbx_getopt_long(argc, argv, shortopts, longopts, NULL, &zbx_optarg,
			&zbx_optind)))
	{
		switch (ch)
		{
			case 'c':
				opt_c++;
				if (NULL == config_file)
					config_file = zbx_strdup(config_file, zbx_optarg);
				break;
			case 'R':
				opt_r++;
				t.opts = zbx_strdup(t.opts, zbx_optarg);
				t.task = ZBX_TASK_RUNTIME_CONTROL;
				break;
			case 'h':
				zbx_help();
				exit(EXIT_SUCCESS);
				break;
			case 'V':
				zbx_version();
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				printf("\n");
				zbx_tls_version();
#endif
				exit(EXIT_SUCCESS);
				break;
			case 'f':
				t.flags |= ZBX_TASK_FLAG_FOREGROUND;
				break;
			default:
				zbx_usage();
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

	if (NULL == config_file)
		config_file = zbx_strdup(NULL, DEFAULT_CONFIG_FILE);

	
	/* required for simple checks */
	zbx_init_metrics();
	zbx_load_config(&t);

	if (ZBX_TASK_RUNTIME_CONTROL == t.task)
	{
		int	ret;
		char	*error = NULL;

		if (FAIL == zbx_ipc_service_init_env(CONFIG_SOCKET_PATH, &error))
		{
			zbx_error("cannot initialize IPC services: %s", error);
			zbx_free(error);
			exit(EXIT_FAILURE);
		}

		if (SUCCEED != (ret = rtc_process(t.opts, &error)))
		{
			zbx_error("Cannot perform runtime control command: %s", error);
			zbx_free(error);
		}

		exit(SUCCEED == ret ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	return zbx_daemon_start(config_allow_root, CONFIG_USER, t.flags, get_pid_file_path, zbx_on_exit);
}

static void	zbx_check_db(void)
{
	struct zbx_json	db_version_json;
	int		result = SUCCEED;

	memset(&db_version_info, 0, sizeof(db_version_info));
	result = zbx_db_check_version_info(&db_version_info, CONFIG_ALLOW_UNSUPPORTED_DB_VERSIONS);

	if (SUCCEED == result)
	{
		zbx_db_extract_dbextension_info(&db_version_info);
	}

	if (SUCCEED == result && (
#ifdef HAVE_POSTGRESQL
			SUCCEED != zbx_db_check_tsdb_capabilities(&db_version_info, CONFIG_ALLOW_UNSUPPORTED_DB_VERSIONS) ||
#endif
			SUCCEED != DBcheck_version()))
	{
		result = FAIL;
	}

	DBconnect(ZBX_DB_CONNECT_NORMAL);

	if (SUCCEED == DBfield_exists("config", "dbversion_status"))
	{
		zbx_json_initarray(&db_version_json, ZBX_JSON_STAT_BUF_LEN);

	
#if defined(HAVE_POSTGRESQL)
		if (0 == zbx_strcmp_null(db_version_info.extension, ZBX_DB_EXTENSION_TIMESCALEDB))
		{
			zbx_tsdb_extract_compressed_chunk_flags(&db_version_info);
		}
#endif
		zbx_db_version_json_create(&db_version_json, &db_version_info);

//		if (SUCCEED == result)
//			zbx_history_check_version(&db_version_json, &result);

		zbx_db_flush_version_requirements(db_version_json.buffer);
		zbx_json_free(&db_version_json);
	}

	DBclose();

	if (SUCCEED != result)
	{
		zbx_db_version_info_clear(&db_version_info);
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: save Zabbix server status to database                             *
 *                                                                            *
 ******************************************************************************/
static void	zbx_db_save_server_status(void)
{
	struct zbx_json	json;

	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addstring(&json, "version", ZABBIX_VERSION, ZBX_JSON_TYPE_STRING);

	zbx_json_close(&json);

	DBconnect(ZBX_DB_CONNECT_NORMAL);

	if (ZBX_DB_OK > DBexecute("update config set server_status='%s'", json.buffer))
		zabbix_log(LOG_LEVEL_WARNING, "Failed to save server status to database");

	DBclose();

	zbx_json_free(&json);
}

/******************************************************************************
 *                                                                            *
 * Purpose: initialize shared resources and start processes                   *
 *                                                                            *
 ******************************************************************************/
static int	server_startup(zbx_socket_t *listen_sock, zbx_socket_t *api_listen_sock, int *ha_stat, int *ha_failover, zbx_rtc_t *rtc)
{
	int				i, ret = SUCCEED;
	char				*error = NULL;

	zbx_config_comms_args_t		zbx_config = {zbx_config_tls, NULL, 0};

	zbx_thread_args_t		thread_args;
	zbx_thread_poller_args		poller_args = {&zbx_config, get_program_type, ZBX_NO_POLLER};
	zbx_thread_trapper_args		trapper_args = {&zbx_config, get_program_type, listen_sock};
	zbx_thread_escalator_args	escalator_args = {zbx_config_tls, get_program_type};
	zbx_thread_proxy_poller_args	proxy_poller_args = {zbx_config_tls, get_program_type};
	zbx_thread_discoverer_args	discoverer_args = {zbx_config_tls, get_program_type};
	zbx_thread_report_writer_args	report_writer_args = {zbx_config_tls->ca_file, zbx_config_tls->cert_file,
							zbx_config_tls->key_file, CONFIG_SOURCE_IP, get_program_type};
	zbx_thread_housekeeper_args	housekeeper_args = {get_program_type, &db_version_info};

	if (SUCCEED != init_database_cache(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize database cache: %s", error);
		zbx_free(error);
		return FAIL;
	}

	if (SUCCEED != init_configuration_cache(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize configuration cache: %s", error);
		zbx_free(error);
		return FAIL;
	}

	if (SUCCEED != zbx_init_selfmon_collector(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize self-monitoring: %s", error);
		zbx_free(error);
		return FAIL;
	}

	DC_set_debug_item(CONFIG_DEBUG_ITEM);
	DC_set_debug_trigger(CONFIG_DEBUG_TRIGGER);

	if (0 != CONFIG_VMWARE_FORKS && SUCCEED != zbx_vmware_init(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize VMware cache: %s", error);
		zbx_free(error);
		return FAIL;
	}

	if (FAIL == glb_state_init()) {
		zbx_error("Cannot initialize Glaber state CACHE");
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



	if (SUCCEED != zbx_tfc_init(CONFIG_TREND_FUNC_CACHE_SIZE, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize trends read cache: %s", error);
		zbx_free(error);
		return FAIL;
	}

	if (0 != CONFIG_TRAPPER_FORKS)
	{
		if (FAIL == zbx_tcp_listen(listen_sock, CONFIG_LISTEN_IP, (unsigned short)CONFIG_LISTEN_PORT))
		{
			zabbix_log(LOG_LEVEL_CRIT, "listener failed: %s", zbx_socket_strerror());
			return FAIL;
		}
	}

	if (0 != CONFIG_API_TRAPPER_FORKS)
	{
		if (FAIL == zbx_tcp_listen(api_listen_sock, CONFIG_LISTEN_IP, (unsigned short)CONFIG_API_LISTEN_PORT))
		{
			zabbix_log(LOG_LEVEL_CRIT, "API listener failed: %s", zbx_socket_strerror());
			exit(EXIT_FAILURE);
		}
	}

	threads_num = CONFIG_CONFSYNCER_FORKS + CONFIG_POLLER_FORKS
			+ CONFIG_UNREACHABLE_POLLER_FORKS + CONFIG_TRAPPER_FORKS + CONFIG_PINGER_FORKS
			+ CONFIG_ALERTER_FORKS + CONFIG_HOUSEKEEPER_FORKS + CONFIG_TIMER_FORKS
			+ CONFIG_HTTPPOLLER_FORKS + CONFIG_DISCOVERER_FORKS + CONFIG_HISTSYNCER_FORKS
			+ CONFIG_ESCALATOR_FORKS + CONFIG_IPMIPOLLER_FORKS + CONFIG_JAVAPOLLER_FORKS
			+ CONFIG_SNMPTRAPPER_FORKS + CONFIG_PROXYPOLLER_FORKS + CONFIG_SELFMON_FORKS
			+ CONFIG_VMWARE_FORKS + CONFIG_TASKMANAGER_FORKS + CONFIG_IPMIMANAGER_FORKS
			+ CONFIG_ALERTMANAGER_FORKS + CONFIG_PREPROCMAN_FORKS + CONFIG_PREPROCESSOR_FORKS
			+ CONFIG_LLDMANAGER_FORKS + CONFIG_LLDWORKER_FORKS + CONFIG_ALERTDB_FORKS
			+ CONFIG_HISTORYPOLLER_FORKS + CONFIG_AVAILMAN_FORKS + CONFIG_REPORTMANAGER_FORKS
			+ CONFIG_REPORTWRITER_FORKS + CONFIG_SERVICEMAN_FORKS + CONFIG_TRIGGERHOUSEKEEPER_FORKS
			+ CONFIG_ODBCPOLLER_FORKS +
			+ CONFIG_GLB_SNMP_FORKS + CONFIG_GLB_AGENT_FORKS + CONFIG_API_TRAPPER_FORKS
			+ CONFIG_GLB_PINGER_FORKS + CONFIG_GLB_WORKER_FORKS +  CONFIG_EXT_SERVER_FORKS
			+ CONFIG_GLB_PREPROCESSOR_FORKS;

	threads = (pid_t *)zbx_calloc(threads, (size_t)threads_num, sizeof(pid_t));
	threads_flags = (int *)zbx_calloc(threads_flags, (size_t)threads_num, sizeof(int));

	zabbix_log(LOG_LEVEL_INFORMATION, "server #0 started [main process]");

	zbx_set_exit_on_terminate();

	for (i = 0; i < threads_num; i++)
	{
		if (FAIL == get_process_info_by_thread(i + 1, &thread_args.info.process_type,
				&thread_args.info.process_num))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		thread_args.info.server_num = i + 1;
		thread_args.args = NULL;

		switch (thread_args.info.process_type)
		{
			case ZBX_PROCESS_TYPE_HISTORYPOLLER:
				thread_args.args = &poller_args;
				LOG_INF("Starting  Calc glb poller of type %d",poller_args.poller_type );
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_SNMP:
				thread_args.args = &poller_args;
				LOG_INF("Starting  SNMP glb poller of type %d",poller_args.poller_type );
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_PINGER:
				thread_args.args = &poller_args;
				LOG_INF("Starting ICMP glb poller of type %d",poller_args.poller_type );
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_WORKER:
				thread_args.args = &poller_args;
				LOG_INF("Starting  WORKER glb poller of type %d",poller_args.poller_type );
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_SERVER:
				thread_args.args = &poller_args;
				LOG_INF("Starting  WORKER glb poller of type %d",poller_args.poller_type );
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_AGENT:
				thread_args.args = &poller_args;
				LOG_INF("Starting AGENT glb poller of type %d",poller_args.poller_type );
				zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_API_TRAPPER:
				trapper_args.listen_sock = api_listen_sock;
				thread_args.args = &trapper_args;
				LOG_INF("Starting GLB api trapper of type");
				zbx_thread_start(trapper_thread, &thread_args, &threads[i]);
				break;	
			case GLB_PROCESS_TYPE_PREPROCESSOR:
				zbx_thread_start(glb_preprocessing_worker_thread, &thread_args, &threads[i]);
				break;

			case ZBX_PROCESS_TYPE_SERVICEMAN:
				threads_flags[i] = ZBX_THREAD_PRIORITY_SECOND;
				zbx_thread_start(service_manager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_CONFSYNCER:
				zbx_thread_start(dbconfig_thread, &thread_args, &threads[i]);
				if (FAIL == (ret = zbx_rtc_wait_config_sync(rtc, rtc_process_request_ex)))
					goto out;

				if (SUCCEED != (ret = zbx_ha_get_status(CONFIG_HA_NODE_NAME, ha_stat, ha_failover,
						&error)))
				{
					zabbix_log(LOG_LEVEL_CRIT, "cannot obtain HA status: %s", error);
					zbx_free(error);
					goto out;
				}

				if (ZBX_NODE_STATUS_ACTIVE != *ha_stat)
					goto out;

				DBconnect(ZBX_DB_CONNECT_NORMAL);

				if (SUCCEED != zbx_check_postinit_tasks(&error))
				{
					zabbix_log(LOG_LEVEL_CRIT, "cannot complete post initialization tasks: %s",
							error);
					zbx_free(error);
					DBclose();

					ret = FAIL;
					goto out;
				}

				/* update maintenance states */
				zbx_dc_update_maintenances();

				DBclose();
				break;
			case ZBX_PROCESS_TYPE_POLLER:
				poller_args.poller_type = ZBX_POLLER_TYPE_NORMAL;
				thread_args.args = &poller_args;
				zbx_thread_start(poller_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_UNREACHABLE:
				poller_args.poller_type = ZBX_POLLER_TYPE_UNREACHABLE;
				thread_args.args = &poller_args;
				zbx_thread_start(poller_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_TRAPPER:
				thread_args.args = &trapper_args;
				zbx_thread_start(trapper_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_PINGER:
				zbx_thread_start(pinger_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_ALERTER:
				zbx_thread_start(alerter_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_HOUSEKEEPER:
				thread_args.args = &housekeeper_args;
				zbx_thread_start(housekeeper_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_TIMER:
				zbx_thread_start(timer_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_HTTPPOLLER:
				zbx_thread_start(httppoller_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_DISCOVERER:
				thread_args.args = &discoverer_args;
				zbx_thread_start(discoverer_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_HISTSYNCER:
				threads_flags[i] = ZBX_THREAD_PRIORITY_FIRST;
				zbx_thread_start(dbsyncer_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_ESCALATOR:
				thread_args.args = &escalator_args;
				zbx_thread_start(escalator_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_JAVAPOLLER:
				poller_args.poller_type = ZBX_POLLER_TYPE_JAVA;
				thread_args.args = &poller_args;
				zbx_thread_start(poller_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_SNMPTRAPPER:
				zbx_thread_start(snmptrapper_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_PROXYPOLLER:
				thread_args.args = &proxy_poller_args;
				zbx_thread_start(proxypoller_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_SELFMON:
				zbx_thread_start(zbx_selfmon_thread, &thread_args, &threads[i]);
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
		//	case ZBX_PROCESS_TYPE_HISTORYPOLLER:
		//		poller_args.poller_type =  ITEM_TYPE_CALCULATED;
		//		thread_args.args = &poller_args;
		//		zbx_thread_start(glbpoller_thread, &thread_args, &threads[i]);
		//		break;
			case ZBX_PROCESS_TYPE_AVAILMAN:
				threads_flags[i] = ZBX_THREAD_PRIORITY_FIRST;
				zbx_thread_start(availability_manager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_REPORTMANAGER:
				zbx_thread_start(report_manager_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_REPORTWRITER:
				thread_args.args = &report_writer_args;
				zbx_thread_start(report_writer_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_TRIGGERHOUSEKEEPER:
				zbx_thread_start(trigger_housekeeper_thread, &thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_ODBCPOLLER:
				poller_args.poller_type = ZBX_POLLER_TYPE_ODBC;
				thread_args.args = &poller_args;
				zbx_thread_start(poller_thread, &thread_args, &threads[i]);
				break;
		}
	}

	/* startup/postinit tasks can take a long time, update status */
	if (SUCCEED != (ret = zbx_ha_get_status(CONFIG_HA_NODE_NAME, ha_stat, ha_failover, &error)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot obtain HA status: %s", error);
		zbx_free(error);
	}
out:
	zbx_unset_exit_on_terminate();

	return ret;
}

static int	server_restart_logger(char **error)
{
	zabbix_close_log();
	zbx_locks_destroy();

	if (SUCCEED != zbx_locks_create(error))
		return FAIL;

	if (SUCCEED != zabbix_open_log(CONFIG_LOG_TYPE, CONFIG_LOG_LEVEL, CONFIG_LOG_FILE, error))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: terminate processes and destroy shared resources                  *
 *                                                                            *
 ******************************************************************************/
static void	server_teardown(zbx_rtc_t *rtc, zbx_socket_t *listen_sock, zbx_socket_t *api_listen_sock)
{
	int		i;
	char		*error = NULL;
	zbx_ha_config_t	*ha_config = NULL;

	/* hard kill all zabbix processes, no logging or other  */

	zbx_unset_child_signal_handler();

	rtc_reset(rtc);

#ifdef HAVE_PTHREAD_PROCESS_SHARED
	/* Disable locks so main process doesn't hang on logging if a process was              */
	/* killed during logging. The locks will be re-enabled after logger is reinitialized   */
	zbx_locks_disable();
#endif
	zbx_ha_kill();

	for (i = 0; i < threads_num; i++)
	{
		if (!threads[i])
			continue;

		kill(threads[i], SIGKILL);
	}

	for (i = 0; i < threads_num; i++)
	{
		if (!threads[i])
			continue;

		zbx_thread_wait(threads[i]);
	}

	zbx_free(threads);
	zbx_free(threads_flags);

	zbx_set_child_signal_handler();

	/* restart logger because it could have been stuck in lock */
	if (SUCCEED != server_restart_logger(&error))
	{
		zbx_error("cannot restart logger: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (NULL != listen_sock)
		zbx_tcp_unlisten(listen_sock);
	
	if (NULL != api_listen_sock)
		zbx_tcp_unlisten(api_listen_sock);

	/* destroy shared caches */
	zbx_tfc_destroy();
	zbx_vmware_destroy();
	zbx_free_selfmon_collector();
	free_configuration_cache();
	free_database_cache(ZBX_SYNC_NONE);

#ifdef HAVE_PTHREAD_PROCESS_SHARED
	zbx_locks_enable();
#endif

	ha_config = zbx_malloc(NULL, sizeof(zbx_ha_config_t));
	ha_config->ha_node_name =	CONFIG_HA_NODE_NAME;
	ha_config->ha_node_address =	CONFIG_NODE_ADDRESS;
	ha_config->default_node_ip =	CONFIG_LISTEN_IP;
	ha_config->default_node_port =	CONFIG_LISTEN_PORT;
	ha_config->ha_status =		ZBX_NODE_STATUS_STANDBY;

	if (SUCCEED != zbx_ha_start(rtc, ha_config, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot start HA manager: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}
}

int	MAIN_ZABBIX_ENTRY(int flags)
{
	char		*error = NULL;
	int		i, db_type, ret, ha_status_old;

	zbx_socket_t	listen_sock, api_listen_sock;
	time_t		standby_warning_time;
	zbx_rtc_t	rtc;
	zbx_timespec_t	rtc_timeout = {1, 0};
	zbx_ha_config_t	*ha_config = NULL;
	struct rlimit limits;
	//ZBX_TASK_EX	t = {ZBX_TASK_START};

	if (0 != (flags & ZBX_TASK_FLAG_FOREGROUND))
	{
		printf("Starting Glaber Server. Glaber %s (revision %s).\nPress Ctrl+C to exit.\n\n",
				GLABER_VERSION, ZABBIX_REVISION);
	}

	if (FAIL == zbx_ipc_service_init_env(CONFIG_SOCKET_PATH, &error))
	{
		zbx_error("cannot initialize IPC services: %s", error);
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

	zbx_new_cuid(ha_sessionid.str);
	
	//for async version we need lots of sockets 
	//to be opened. Checking for that limit
	getrlimit(RLIMIT_NOFILE,&limits);

    if (ZBX_MIN_OPEN_FILES>limits.rlim_cur ) {
//	    zbx_error("WARNING!!! the system has only %ld open files limit, which is too low for ASYNC version",limits.rlim_cur);
//	    zbx_error("Will try to set the limit to %d:",ZBX_DESIRED_OPEN_FILES);

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

	zbx_initialize_events();


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

	zabbix_log(LOG_LEVEL_INFORMATION, "using configuration file: %s", config_file);

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
	zbx_initialize_events();


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

	if (SUCCEED != zbx_rtc_init(&rtc, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize runtime control service: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zbx_vault_token_from_env_get(&CONFIG_VAULTTOKEN, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize vault token: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zbx_vault_init(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize vault: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zbx_vault_db_credentials_get(&CONFIG_DBUSER, &CONFIG_DBPASSWORD, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize database credentials from vault: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != DBinit(DCget_nextid, program_type, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize database: %s", error);
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

	if (SUCCEED != init_database_cache(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize database cache: %s", error);
		zbx_free(error);
		return FAIL;
	}

	DBcheck_character_set();
    zbx_check_db();
	zbx_db_save_server_status();

	if (SUCCEED != DBcheck_double_type())
	{
		CONFIG_DOUBLE_PRECISION = ZBX_DB_DBL_PRECISION_DISABLED;
		zbx_update_epsilon_to_float_precision();
		zabbix_log(LOG_LEVEL_WARNING, "database is not upgraded to use double precision values");
	}

	if (SUCCEED != zbx_db_check_instanceid())
		exit(EXIT_FAILURE);

	if (FAIL == zbx_export_init(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize export: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	zbx_unset_exit_on_terminate();

	ha_config = zbx_malloc(NULL, sizeof(zbx_ha_config_t));
	ha_config->ha_node_name =	CONFIG_HA_NODE_NAME;
	ha_config->ha_node_address =	CONFIG_NODE_ADDRESS;
	ha_config->default_node_ip =	CONFIG_LISTEN_IP;
	ha_config->default_node_port =	CONFIG_LISTEN_PORT;
	ha_config->ha_status =		ZBX_NODE_STATUS_UNKNOWN;

	if (SUCCEED != zbx_ha_start(&rtc, ha_config, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot start HA manager: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED == zbx_is_export_enabled(ZBX_FLAG_EXPTYPE_EVENTS))
		zbx_problems_export_init("main-process", 0);

	if (SUCCEED == zbx_is_export_enabled(ZBX_FLAG_EXPTYPE_HISTORY))
		zbx_history_export_init("main-process", 0);

	if (SUCCEED == zbx_is_export_enabled(ZBX_FLAG_EXPTYPE_TRENDS))
		zbx_trends_export_init("main-process", 0);

	if (SUCCEED != zbx_ha_get_status(CONFIG_HA_NODE_NAME, &ha_status, &ha_failover_delay, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot start server: %s", error);
		zbx_free(error);
		zbx_set_exiting_with_fail();
	}

	zbx_zabbix_stats_init(zbx_zabbix_stats_ext_get);
	zbx_diag_init(diag_add_section_info);

	if (ZBX_NODE_STATUS_ACTIVE == ha_status)
	{
		if (SUCCEED != server_startup(&listen_sock, &api_listen_sock, &ha_status, &ha_failover_delay, &rtc))
		{
			zbx_set_exiting_with_fail();
			ha_status = ZBX_NODE_STATUS_ERROR;
		}
		else
		{
			/* check if the HA status has not been changed during startup process */
			if (ZBX_NODE_STATUS_ACTIVE != ha_status)
				server_teardown(&rtc, &listen_sock, &api_listen_sock);
		}
	}

	if (ZBX_NODE_STATUS_ERROR != ha_status)
	{
		if (NULL != CONFIG_HA_NODE_NAME && '\0' != *CONFIG_HA_NODE_NAME)
		{
			zabbix_log(LOG_LEVEL_INFORMATION, "\"%s\" node started in \"%s\" mode", CONFIG_HA_NODE_NAME,
					zbx_ha_status_str(ha_status));

		}
	}

	ha_status_old = ha_status;

	if (ZBX_NODE_STATUS_STANDBY == ha_status)
		standby_warning_time = time(NULL);

	while (ZBX_IS_RUNNING())
	{
		time_t			now;
		zbx_ipc_client_t	*client;
		zbx_ipc_message_t	*message;

		(void)zbx_ipc_service_recv(&rtc.service, &rtc_timeout, &client, &message);

		if (NULL == message || ZBX_IPC_SERVICE_HA_RTC_FIRST <= message->code)
		{
			if (SUCCEED != zbx_ha_dispatch_message(CONFIG_HA_NODE_NAME, message, &ha_status,
					&ha_failover_delay, &error))
			{
				zabbix_log(LOG_LEVEL_CRIT, "HA manager error: %s", error);
				zbx_set_exiting_with_fail();
			}
		}
		else
		{
			if (ZBX_NODE_STATUS_ACTIVE == ha_status || ZBX_RTC_LOG_LEVEL_DECREASE == message->code ||
					ZBX_RTC_LOG_LEVEL_INCREASE == message->code)
			{
				zbx_rtc_dispatch(&rtc, client, message, rtc_process_request_ex);
			}
			else
			{
				const char	*result = "Runtime commands can be executed only in active mode\n";
				zbx_ipc_client_send(client, message->code, (const unsigned char *)result,
						(zbx_uint32_t)strlen(result) + 1);
			}
		}

		zbx_ipc_message_free(message);

		if (NULL != client)
			zbx_ipc_client_release(client);

		if (ZBX_NODE_STATUS_ERROR == ha_status)
			break;

		now = time(NULL);

		if (ZBX_NODE_STATUS_UNKNOWN != ha_status && ha_status != ha_status_old)
		{
			ha_status_old = ha_status;
			zabbix_log(LOG_LEVEL_INFORMATION, "\"%s\" node switched to \"%s\" mode",
					ZBX_NULL2EMPTY_STR(CONFIG_HA_NODE_NAME), zbx_ha_status_str(ha_status));

			switch (ha_status)
			{
				case ZBX_NODE_STATUS_ACTIVE:
					if (SUCCEED != server_startup(&listen_sock, &api_listen_sock, &ha_status, &ha_failover_delay, &rtc))
					{
						zbx_set_exiting_with_fail();
						ha_status = ZBX_NODE_STATUS_ERROR;
						continue;
					}

					if (ZBX_NODE_STATUS_ACTIVE != ha_status)
					{
						server_teardown(&rtc, &listen_sock, &api_listen_sock);
						ha_status_old = ha_status;
					}
					break;
				case ZBX_NODE_STATUS_STANDBY:
					server_teardown(&rtc, &listen_sock, &api_listen_sock);
					standby_warning_time = now;
					break;
				default:
					zabbix_log(LOG_LEVEL_CRIT, "unsupported status %d received from HA manager",
							ha_status);
					zbx_set_exiting_with_fail();
					continue;
			}
		}

		if (ZBX_NODE_STATUS_STANDBY == ha_status)
		{
			if (standby_warning_time + SEC_PER_HOUR <= now)
			{
				zabbix_log(LOG_LEVEL_INFORMATION, "\"%s\" node is working in \"%s\" mode",
						CONFIG_HA_NODE_NAME, zbx_ha_status_str(ha_status));
				standby_warning_time = now;
			}
		}

		if (0 < (ret = waitpid((pid_t)-1, &i, WNOHANG)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "PROCESS EXIT: %d", ret);
			zbx_set_exiting_with_fail();
			break;
		}

		if (-1 == ret && EINTR != errno)
		{
			zabbix_log(LOG_LEVEL_ERR, "failed to wait on child processes: %s", zbx_strerror(errno));
			zbx_set_exiting_with_fail();
			break;
		}
	}

	if (SUCCEED == ZBX_EXIT_STATUS())
		zbx_rtc_shutdown_subs(&rtc);

	if (SUCCEED != zbx_ha_pause(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot pause HA manager: %s", error);
		zbx_free(error);
	}

	zbx_db_version_info_clear(&db_version_info);

	zbx_on_exit(ZBX_EXIT_STATUS());

	return SUCCEED;
}
