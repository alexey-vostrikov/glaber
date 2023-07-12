/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

#include "log.h"

#include "zbxmutexs.h"
#include "zbxthreads.h"
#include "cfg.h"
#include "zbxstr.h"
#include "zbxtime.h"
#ifdef _WINDOWS
#	include "messages.h"
#	include "zbxwinservice.h"
#	include "zbxsysinfo.h"
static HANDLE		system_log_handle = INVALID_HANDLE_VALUE;
#endif

#define LOG_COMPONENT_LEN	64

#define LOG_LEVEL_DEC_FAIL	-2
#define LOG_LEVEL_DEC_SUCCEED	-1
#define LOG_LEVEL_UNCHANGED	0
#define LOG_LEVEL_INC_SUCCEED	1
#define LOG_LEVEL_INC_FAIL	2

static ZBX_THREAD_LOCAL int	zbx_log_level_change = LOG_LEVEL_UNCHANGED;

static char			log_filename[MAX_STRING_LEN];
static int			log_type = LOG_TYPE_UNDEFINED;
static zbx_mutex_t		log_access = ZBX_MUTEX_NULL;
int				zbx_log_level = LOG_LEVEL_WARNING;

static ZBX_THREAD_LOCAL char	log_component[LOG_COMPONENT_LEN + 1];

static int			config_log_file_size = -1;	/* max log file size in MB */

static int	get_config_log_file_size(void)
{
	if (-1 != config_log_file_size)
		return config_log_file_size;

	THIS_SHOULD_NEVER_HAPPEN;
	exit(EXIT_FAILURE);
}

#ifdef _WINDOWS
#	define LOCK_LOG		zbx_mutex_lock(log_access)
#	define UNLOCK_LOG	zbx_mutex_unlock(log_access)
#else
#	define LOCK_LOG		lock_log()
#	define UNLOCK_LOG	unlock_log()
#endif

#ifdef _WINDOWS
#	define STDIN_FILENO	_fileno(stdin)
#	define STDOUT_FILENO	_fileno(stdout)
#	define STDERR_FILENO	_fileno(stderr)

#	define ZBX_DEV_NULL	"NUL"

#	define dup2(fd1, fd2)	_dup2(fd1, fd2)
#else
#	define ZBX_DEV_NULL	"/dev/null"
#endif

#ifndef _WINDOWS
const char	*zabbix_get_log_level_string(void)
{
	switch (zbx_log_level)
	{
		case LOG_LEVEL_EMPTY:
			return "0 (none)";
		case LOG_LEVEL_CRIT:
			return "1 (critical)";
		case LOG_LEVEL_ERR:
			return "2 (error)";
		case LOG_LEVEL_WARNING:
			return "3 (warning)";
		case LOG_LEVEL_DEBUG:
			return "4 (debug)";
		case LOG_LEVEL_TRACE:
			return "5 (trace)";
	}

	THIS_SHOULD_NEVER_HAPPEN;
	exit(EXIT_FAILURE);
}

int	zabbix_increase_log_level(void)
{
	if (LOG_LEVEL_TRACE == zbx_log_level)
		return FAIL;

	zbx_log_level = zbx_log_level + 1;

	return SUCCEED;
}

int	zabbix_decrease_log_level(void)
{
	if (LOG_LEVEL_EMPTY == zbx_log_level)
		return FAIL;

	zbx_log_level = zbx_log_level - 1;

	return SUCCEED;
}
#endif

int	zbx_redirect_stdio(const char *filename)
{
	const char	default_file[] = ZBX_DEV_NULL;
	int		open_flags = O_WRONLY, fd;

	if (NULL != filename && '\0' != *filename)
		open_flags |= O_CREAT | O_APPEND;
	else
		filename = default_file;

	if (-1 == (fd = open(filename, open_flags, 0666)))
	{
		zbx_error("cannot open \"%s\": %s", filename, zbx_strerror(errno));
		return FAIL;
	}

	fflush(stdout);
	if (-1 == dup2(fd, STDOUT_FILENO))
		zbx_error("cannot redirect stdout to \"%s\": %s", filename, zbx_strerror(errno));

	fflush(stderr);
	if (-1 == dup2(fd, STDERR_FILENO))
		zbx_error("cannot redirect stderr to \"%s\": %s", filename, zbx_strerror(errno));

	close(fd);

	if (-1 == (fd = open(default_file, O_RDONLY)))
	{
		zbx_error("cannot open \"%s\": %s", default_file, zbx_strerror(errno));
		return FAIL;
	}

	if (-1 == dup2(fd, STDIN_FILENO))
		zbx_error("cannot redirect stdin to \"%s\": %s", default_file, zbx_strerror(errno));

	close(fd);

	return SUCCEED;
}

static void	rotate_log(const char *filename)
{
	zbx_stat_t		buf;
	zbx_uint64_t		new_size;
	static zbx_uint64_t	old_size = ZBX_MAX_UINT64;	/* redirect stdout and stderr */
#if !defined(_WINDOWS)
	static zbx_uint64_t	st_ino, st_dev;
#endif

	if (0 != zbx_stat(filename, &buf))
	{
		zbx_redirect_stdio(filename);
		return;
	}

	new_size = buf.st_size;

	if (0 != get_config_log_file_size() && (zbx_uint64_t)get_config_log_file_size() * ZBX_MEBIBYTE < new_size)
	{
		char	filename_old[MAX_STRING_LEN];

		zbx_strscpy(filename_old, filename);
		zbx_strlcat(filename_old, ".old", MAX_STRING_LEN);
		remove(filename_old);
#ifdef _WINDOWS
		zbx_redirect_stdio(NULL);
#endif
		if (0 != rename(filename, filename_old))
		{
			FILE	*log_file = NULL;

			if (NULL != (log_file = fopen(filename, "w")))
			{
				long		milliseconds;
				struct tm	tm;

				zbx_get_time(&tm, &milliseconds, NULL);

				fprintf(log_file, "%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld"
						" cannot rename log file \"%s\" to \"%s\": %s\n",
						zbx_get_thread_id(),
						tm.tm_year + 1900,
						tm.tm_mon + 1,
						tm.tm_mday,
						tm.tm_hour,
						tm.tm_min,
						tm.tm_sec,
						milliseconds,
						filename,
						filename_old,
						zbx_strerror(errno));

				fprintf(log_file, "%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld"
						" Logfile \"%s\" size reached configured limit"
						" LogFileSize but moving it to \"%s\" failed. The logfile"
						" was truncated.\n",
						zbx_get_thread_id(),
						tm.tm_year + 1900,
						tm.tm_mon + 1,
						tm.tm_mday,
						tm.tm_hour,
						tm.tm_min,
						tm.tm_sec,
						milliseconds,
						filename,
						filename_old);

				zbx_fclose(log_file);

				new_size = 0;
			}
		}
		else
			new_size = 0;
	}

	if (old_size > new_size)
		zbx_redirect_stdio(filename);
#if !defined(_WINDOWS)
	else if (st_ino != buf.st_ino || st_dev != buf.st_dev)
	{
		st_ino = buf.st_ino;
		st_dev = buf.st_dev;
		zbx_redirect_stdio(filename);
	}
#endif

	old_size = new_size;
}

#ifndef _WINDOWS
static sigset_t	orig_mask;

static void	lock_log(void)
{
	sigset_t	mask;

	/* block signals to prevent deadlock on log file mutex when signal handler attempts to lock log */
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGHUP);

	if (0 > zbx_sigmask(SIG_BLOCK, &mask, &orig_mask))
		zbx_error("cannot set signal mask to block the user signal");

	zbx_mutex_lock(log_access);
}

static void	unlock_log(void)
{
	zbx_mutex_unlock(log_access);

	if (0 > zbx_sigmask(SIG_SETMASK, &orig_mask, NULL))
		zbx_error("cannot restore signal mask");
}
#else
static void	lock_log(void)
{
#ifdef ZABBIX_AGENT
	if (0 == (ZBX_MUTEX_LOGGING_DENIED & zbx_get_thread_global_mutex_flag()))
#endif
		LOCK_LOG;
}

static void	unlock_log(void)
{
#ifdef ZABBIX_AGENT
	if (0 == (ZBX_MUTEX_LOGGING_DENIED & zbx_get_thread_global_mutex_flag()))
#endif
		UNLOCK_LOG;
}
#endif

void	zbx_handle_log(void)
{
	if (LOG_TYPE_FILE != log_type)
		return;

	LOCK_LOG;

	rotate_log(log_filename);

	UNLOCK_LOG;
}

int	zabbix_open_log(const zbx_config_log_t *log_file_cfg, int level, char **error)
{
	const char	*filename = log_file_cfg->log_file_name;
	int		type = log_file_cfg->log_type;

	log_type = type;
	zbx_log_level = level;
	config_log_file_size = log_file_cfg->log_file_size;

	if (LOG_TYPE_SYSTEM == type)
	{
#ifdef _WINDOWS
		wchar_t	*wevent_source;

		wevent_source = zbx_utf8_to_unicode(ZABBIX_EVENT_SOURCE);
		system_log_handle = RegisterEventSource(NULL, wevent_source);
		zbx_free(wevent_source);
#else
		openlog(syslog_app_name, LOG_PID, LOG_DAEMON);
#endif
	}
	else if (LOG_TYPE_FILE == type)
	{
		FILE	*log_file = NULL;

		if (MAX_STRING_LEN <= strlen(filename))
		{
			*error = zbx_strdup(*error, "too long path for logfile");
			return FAIL;
		}

		if (SUCCEED != zbx_mutex_create(&log_access, ZBX_MUTEX_LOG, error))
			return FAIL;

		if (NULL == (log_file = fopen(filename, "a+")))
		{
			*error = zbx_dsprintf(*error, "unable to open log file [%s]: %s", filename,
					zbx_strerror(errno));
			return FAIL;
		}

		zbx_strscpy(log_filename, filename);
		zbx_fclose(log_file);
	}
	else if (LOG_TYPE_CONSOLE == type || LOG_TYPE_UNDEFINED == type)
	{
		if (SUCCEED != zbx_mutex_create(&log_access, ZBX_MUTEX_LOG, error))
		{
			*error = zbx_strdup(*error, "unable to create mutex for standard output");
			return FAIL;
		}

		fflush(stderr);
		if (-1 == dup2(STDOUT_FILENO, STDERR_FILENO))
			zbx_error("cannot redirect stderr to stdout: %s", zbx_strerror(errno));
	}
	else
	{
		*error = zbx_strdup(*error, "unknown log type");
		return FAIL;
	}

	return SUCCEED;
}

void	zabbix_close_log(void)
{
	if (LOG_TYPE_SYSTEM == log_type)
	{
#ifdef _WINDOWS
		if (NULL != system_log_handle)
			DeregisterEventSource(system_log_handle);
#else
		closelog();
#endif
	}
	else if (LOG_TYPE_FILE == log_type || LOG_TYPE_CONSOLE == log_type || LOG_TYPE_UNDEFINED == log_type)
	{
		zbx_mutex_destroy(&log_access);
	}

	log_type = LOG_TYPE_UNDEFINED;
}

void	__zbx_zabbix_log(int level, const char *fmt, ...)
{
	char		message[MAX_BUFFER_LEN];
	va_list		args;
#ifdef _WINDOWS
	WORD		wType;
	wchar_t		thread_id[20], *strings[2];
#endif

#ifndef ZBX_ZABBIX_LOG_CHECK
	if (SUCCEED != ZBX_CHECK_LOG_LEVEL(level))
		return;
#endif
	if (LOG_TYPE_FILE == log_type)
	{
		FILE	*log_file;

		LOCK_LOG;

		if (0 != get_config_log_file_size())
			rotate_log(log_filename);

		if (NULL != (log_file = fopen(log_filename, "a+")))
		{
			long		milliseconds;
			struct tm	tm;

			zbx_get_time(&tm, &milliseconds, NULL);

			fprintf(log_file,
					"%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld %s",
					zbx_get_thread_id(),
					tm.tm_year + 1900,
					tm.tm_mon + 1,
					tm.tm_mday,
					tm.tm_hour,
					tm.tm_min,
					tm.tm_sec,
					milliseconds,
					log_component
					);

			va_start(args, fmt);
			vfprintf(log_file, fmt, args);
			va_end(args);

			fprintf(log_file, "\n");

			zbx_fclose(log_file);
		}
		else
		{
			zbx_error("failed to open log file: %s", zbx_strerror(errno));

			va_start(args, fmt);
			zbx_vsnprintf(message, sizeof(message), fmt, args);
			va_end(args);

			zbx_error("failed to write [%s] into log file", message);
		}

		UNLOCK_LOG;

		return;
	}

	if (LOG_TYPE_CONSOLE == log_type)
	{
		long		milliseconds;
		struct tm	tm;

		LOCK_LOG;

		zbx_get_time(&tm, &milliseconds, NULL);

		fprintf(stdout,
				"%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld %s",
				zbx_get_thread_id(),
				tm.tm_year + 1900,
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec,
				milliseconds,
				log_component
				);

		va_start(args, fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);

		fprintf(stdout, "\n");

		fflush(stdout);

		UNLOCK_LOG;

		return;
	}

	va_start(args, fmt);
	zbx_vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);

	if (LOG_TYPE_SYSTEM == log_type)
	{
#ifdef _WINDOWS
		switch (level)
		{
			case LOG_LEVEL_CRIT:
			case LOG_LEVEL_ERR:
				wType = EVENTLOG_ERROR_TYPE;
				break;
			case LOG_LEVEL_WARNING:
				wType = EVENTLOG_WARNING_TYPE;
				break;
			default:
				wType = EVENTLOG_INFORMATION_TYPE;
				break;
		}

		StringCchPrintf(thread_id, ARRSIZE(thread_id), TEXT("[%li]: "), zbx_get_thread_id());
		strings[0] = thread_id;
		strings[1] = zbx_utf8_to_unicode(message);

		ReportEvent(
			system_log_handle,
			wType,
			0,
			MSG_ZABBIX_MESSAGE,
			NULL,
			sizeof(strings) / sizeof(*strings),
			0,
			strings,
			NULL);

		zbx_free(strings[1]);

#else	/* not _WINDOWS */

		/* for nice printing into syslog */
		switch (level)
		{
			case LOG_LEVEL_CRIT:
				syslog(LOG_CRIT, "%s%s", log_component, message);
				break;
			case LOG_LEVEL_ERR:
				syslog(LOG_ERR, "%s%s", log_component, message);
				break;
			case LOG_LEVEL_WARNING:
				syslog(LOG_WARNING, "%s%s", log_component, message);
				break;
			case LOG_LEVEL_DEBUG:
			case LOG_LEVEL_TRACE:
				syslog(LOG_DEBUG, "%s%s", log_component, message);
				break;
			case LOG_LEVEL_INFORMATION:
				syslog(LOG_INFO, "%s%s", log_component, message);
				break;
			default:
				/* LOG_LEVEL_EMPTY - print nothing */
				break;
		}

#endif	/* _WINDOWS */
	}	/* LOG_TYPE_SYSLOG */
	else	/* LOG_TYPE_UNDEFINED == log_type */
	{
		LOCK_LOG;

		switch (level)
		{
			case LOG_LEVEL_CRIT:
				zbx_error("ERROR: %s%s", log_component, message);
				break;
			case LOG_LEVEL_ERR:
				zbx_error("Error: %s%s", log_component, message);
				break;
			case LOG_LEVEL_WARNING:
				zbx_error("Warning: %s%s", log_component, message);
				break;
			case LOG_LEVEL_DEBUG:
				zbx_error("DEBUG: %s%s", log_component, message);
				break;
			case LOG_LEVEL_TRACE:
				zbx_error("TRACE: %s%s", log_component, message);
				break;
			default:
				zbx_error("%s%s", log_component, message);
				break;
		}

		UNLOCK_LOG;
	}
}

int	zbx_get_log_type(const char *logtype)
{
	const char	*logtypes[] = {ZBX_OPTION_LOGTYPE_SYSTEM, ZBX_OPTION_LOGTYPE_FILE, ZBX_OPTION_LOGTYPE_CONSOLE};
	int		i;

	for (i = 0; i < (int)ARRSIZE(logtypes); i++)
	{
		if (0 == strcmp(logtype, logtypes[i]))
			return i + 1;
	}

	return LOG_TYPE_UNDEFINED;
}

int	zbx_validate_log_parameters(ZBX_TASK_EX *task, const zbx_config_log_t *log_file_cfg)
{
	if (LOG_TYPE_UNDEFINED == log_file_cfg->log_type)
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid \"LogType\" configuration parameter: '%s'",
				log_file_cfg->log_type_str);
		return FAIL;
	}

	if (LOG_TYPE_CONSOLE == log_file_cfg->log_type && 0 == (task->flags & ZBX_TASK_FLAG_FOREGROUND) &&
			ZBX_TASK_START == task->task)
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"LogType\" \"console\" parameter can only be used with the"
				" -f (--foreground) command line option");
		return FAIL;
	}

	if (LOG_TYPE_FILE == log_file_cfg->log_type && (NULL == log_file_cfg->log_file_name || '\0' ==
			*log_file_cfg->log_file_name))
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"LogType\" \"file\" parameter requires \"LogFile\" parameter to be set");
		return FAIL;
	}

	return SUCCEED;
}

char	*strerror_from_system(unsigned long error)
{
#ifdef _WINDOWS
	size_t		offset = 0;
	wchar_t		wide_string[ZBX_MESSAGE_BUF_SIZE];
	/* !!! Attention: static !!! Not thread-safe for Win32 */
	static char	utf8_string[ZBX_MESSAGE_BUF_SIZE];

	offset += zbx_snprintf(utf8_string, sizeof(utf8_string), "[0x%08lX] ", error);

	/* we don't know the inserts so we pass NULL and enable appropriate flag */
	if (0 == FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), wide_string, ZBX_MESSAGE_BUF_SIZE, NULL))
	{
		zbx_snprintf(utf8_string + offset, sizeof(utf8_string) - offset,
				"unable to find message text [0x%08lX]", GetLastError());

		return utf8_string;
	}

	zbx_unicode_to_utf8_static(wide_string, utf8_string + offset, (int)(sizeof(utf8_string) - offset));

	zbx_rtrim(utf8_string, "\r\n ");

	return utf8_string;
#else	/* not _WINDOWS */
	ZBX_UNUSED(error);

	return zbx_strerror(errno);
#endif	/* _WINDOWS */
}

#ifdef _WINDOWS
char	*strerror_from_module(unsigned long error, const wchar_t *module)
{
	size_t		offset = 0;
	wchar_t		wide_string[ZBX_MESSAGE_BUF_SIZE];
	HMODULE		hmodule;
	/* !!! Attention: static !!! not thread-safe for Win32 */
	static char	utf8_string[ZBX_MESSAGE_BUF_SIZE];

	*utf8_string = '\0';
	hmodule = GetModuleHandle(module);

	offset += zbx_snprintf(utf8_string, sizeof(utf8_string), "[0x%08lX] ", error);

	/* we don't know the inserts so we pass NULL and enable appropriate flag */
	if (0 == FormatMessage(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, hmodule, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), wide_string, sizeof(wide_string), NULL))
	{
		zbx_snprintf(utf8_string + offset, sizeof(utf8_string) - offset,
				"unable to find message text: %s", strerror_from_system(GetLastError()));

		return utf8_string;
	}

	zbx_unicode_to_utf8_static(wide_string, utf8_string + offset, (int)(sizeof(utf8_string) - offset));

	zbx_rtrim(utf8_string, "\r\n ");

	return utf8_string;
}
#endif	/* _WINDOWS */

/******************************************************************************
 *                                                                            *
 * Purpose: log the message optionally appending to a string buffer           *
 *                                                                            *
 * Parameters: level      - [IN] the log level                                *
 *             out        - [OUT] the output buffer (optional)                *
 *             out_alloc  - [OUT] the output buffer size                      *
 *             out_offset - [OUT] the output buffer offset                    *
 *             format     - [IN] the format string                            *
 *                                                                            *
 * Return value: SUCCEED - the socket was successfully opened                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
void	zbx_strlog_alloc(int level, char **out, size_t *out_alloc, size_t *out_offset, const char *format, ...)
{
	va_list	args;
	size_t	len;
	char	*buf;

	if (SUCCEED != ZBX_CHECK_LOG_LEVEL(level) && NULL == out)
		return;

	va_start(args, format);
	len = (size_t)vsnprintf(NULL, 0, format, args) + 2;
	va_end(args);

	buf = (char *)zbx_malloc(NULL, len);

	va_start(args, format);
	len = (size_t)vsnprintf(buf, len, format, args);
	va_end(args);

	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(level))
		zabbix_log(level, "%s", buf);

	if (NULL != out)
	{
		buf[0] = (char)toupper((unsigned char)buf[0]);
		buf[len++] = '\n';
		buf[len] = '\0';

		zbx_strcpy_alloc(out, out_alloc, out_offset, buf);
	}

	zbx_free(buf);
}
void	 zabbix_report_log_level_change(void)
{
	int	change;

	if (0 == zbx_log_level_change)
		return;

	/* reset log level change history to avoid recursion */
	change = zbx_log_level_change;
	zbx_log_level_change = LOG_LEVEL_UNCHANGED;

	switch (change)
	{
		case LOG_LEVEL_DEC_FAIL:
			zabbix_log(LOG_LEVEL_INFORMATION, "cannot decrease log level:"
					" minimum level has been already set");
			break;
		case LOG_LEVEL_DEC_SUCCEED:
			zabbix_log(LOG_LEVEL_INFORMATION, "log level has been decreased to %s",
					zabbix_get_log_level_string());
			break;
		case LOG_LEVEL_INC_SUCCEED:
			zabbix_log(LOG_LEVEL_INFORMATION, "log level has been increased to %s",
					zabbix_get_log_level_string());
			break;
		case LOG_LEVEL_INC_FAIL:
			zabbix_log(LOG_LEVEL_INFORMATION, "cannot increase log level:"
					" maximum level has been already set");
			break;
	}
}
// void	zbx_set_log_component(const char *component)
// {
// 	zbx_snprintf(log_component, sizeof(log_component), "[%s] ", component);
// }
