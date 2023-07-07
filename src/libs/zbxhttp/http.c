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
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "zbxhttp.h"

#include "zbxstr.h"
#include "log.h"
#include "zbxdbhigh.h"
#include "zbxtime.h"

#ifdef HAVE_LIBCURL

extern char	*CONFIG_SOURCE_IP;

extern char	*CONFIG_SSL_CA_LOCATION;
extern char	*CONFIG_SSL_CERT_LOCATION;
extern char	*CONFIG_SSL_KEY_LOCATION;

size_t	zbx_curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t			r_size = size * nmemb;
	zbx_http_response_t	*response;

	response = (zbx_http_response_t*)userdata;

	if (ZBX_MAX_RECV_DATA_SIZE < response->offset + r_size)
		return 0;

	zbx_str_memcpy_alloc(&response->data, &response->allocated, &response->offset, (const char *)ptr, r_size);

	return r_size;
}

size_t	zbx_curl_ignore_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	ZBX_UNUSED(ptr);
	ZBX_UNUSED(userdata);

	return size * nmemb;
}

int	zbx_http_prepare_callbacks(CURL *easyhandle, zbx_http_response_t *header, zbx_http_response_t *body,
		zbx_curl_cb_t header_cb, zbx_curl_cb_t body_cb, char *errbuf, char **error)
{
	CURLcode	err;

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HEADERFUNCTION, header_cb)))
	{
		*error = zbx_dsprintf(*error, "Cannot set header function: %s", curl_easy_strerror(err));
		return FAIL;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HEADERDATA, header)))
	{
		*error = zbx_dsprintf(*error, "Cannot set header callback: %s", curl_easy_strerror(err));
		return FAIL;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_WRITEFUNCTION, body_cb)))
	{
		*error = zbx_dsprintf(*error, "Cannot set write function: %s", curl_easy_strerror(err));
		return FAIL;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_WRITEDATA, body)))
	{
		*error = zbx_dsprintf(*error, "Cannot set write callback: %s", curl_easy_strerror(err));
		return FAIL;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_ERRORBUFFER, errbuf)))
	{
		*error = zbx_dsprintf(*error, "Cannot set error buffer: %s", curl_easy_strerror(err));
		return FAIL;
	}

	return SUCCEED;
}

int	zbx_http_prepare_ssl(CURL *easyhandle, const char *ssl_cert_file, const char *ssl_key_file,
		const char *ssl_key_password, unsigned char verify_peer, unsigned char verify_host,
		char **error)
{
	CURLcode	err;

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYPEER, 0 == verify_peer ? 0L : 1L)))
	{
		*error = zbx_dsprintf(*error, "Cannot set verify the peer's SSL certificate: %s",
				curl_easy_strerror(err));
		return FAIL;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYHOST, 0 == verify_host ? 0L : 2L)))
	{
		*error = zbx_dsprintf(*error, "Cannot set verify the certificate's name against host: %s",
				curl_easy_strerror(err));
		return FAIL;
	}

	if (NULL != CONFIG_SOURCE_IP)
	{
		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_INTERFACE, CONFIG_SOURCE_IP)))
		{
			*error = zbx_dsprintf(*error, "Cannot specify source interface for outgoing traffic: %s",
					curl_easy_strerror(err));
			return FAIL;
		}
	}

	if (0 != verify_peer && NULL != CONFIG_SSL_CA_LOCATION)
	{
		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_CAPATH, CONFIG_SSL_CA_LOCATION)))
		{
			*error = zbx_dsprintf(*error, "Cannot specify directory holding CA certificates: %s",
					curl_easy_strerror(err));
			return FAIL;
		}
	}

	if (NULL != ssl_cert_file && '\0' != *ssl_cert_file)
	{
		char	*file_name;

		file_name = zbx_dsprintf(NULL, "%s/%s", CONFIG_SSL_CERT_LOCATION, ssl_cert_file);
		zabbix_log(LOG_LEVEL_DEBUG, "using SSL certificate file: '%s'", file_name);

		err = curl_easy_setopt(easyhandle, CURLOPT_SSLCERT, file_name);
		zbx_free(file_name);

		if (CURLE_OK != err)
		{
			*error = zbx_dsprintf(*error, "Cannot set SSL client certificate: %s", curl_easy_strerror(err));
			return FAIL;
		}

		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSLCERTTYPE, "PEM")))
		{
			*error = zbx_dsprintf(NULL, "Cannot specify type of the client SSL certificate: %s",
					curl_easy_strerror(err));
			return FAIL;
		}
	}

	if (NULL != ssl_key_file && '\0' != *ssl_key_file)
	{
		char	*file_name;

		file_name = zbx_dsprintf(NULL, "%s/%s", CONFIG_SSL_KEY_LOCATION, ssl_key_file);
		zabbix_log(LOG_LEVEL_DEBUG, "using SSL private key file: '%s'", file_name);

		err = curl_easy_setopt(easyhandle, CURLOPT_SSLKEY, file_name);
		zbx_free(file_name);

		if (CURLE_OK != err)
		{
			*error = zbx_dsprintf(NULL, "Cannot specify private keyfile for TLS and SSL client cert: %s",
					curl_easy_strerror(err));
			return FAIL;
		}

		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSLKEYTYPE, "PEM")))
		{
			*error = zbx_dsprintf(NULL, "Cannot set type of the private key file: %s",
					curl_easy_strerror(err));
			return FAIL;
		}
	}

	if ('\0' != *ssl_key_password)
	{
		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_KEYPASSWD, ssl_key_password)))
		{
			*error = zbx_dsprintf(NULL, "Cannot set passphrase to private key: %s",
					curl_easy_strerror(err));
			return FAIL;
		}
	}

	return SUCCEED;
}

int	zbx_http_prepare_auth(CURL *easyhandle, unsigned char authtype, const char *username, const char *password,
		const char *token, char **error)
{
	CURLcode	err;
	long		curlauth = 0;
	char		auth[MAX_STRING_LEN];

	if (HTTPTEST_AUTH_NONE == authtype)
		return SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "setting HTTPAUTH [%d]", authtype);

	switch (authtype)
	{
		case HTTPTEST_AUTH_BASIC:
			curlauth = CURLAUTH_BASIC;
			break;
		case HTTPTEST_AUTH_NTLM:
			curlauth = CURLAUTH_NTLM;
			break;
		case HTTPTEST_AUTH_NEGOTIATE:
#if LIBCURL_VERSION_NUM >= 0x072600
			curlauth = CURLAUTH_NEGOTIATE;
#else
			curlauth = CURLAUTH_GSSNEGOTIATE;
#endif
			break;
		case HTTPTEST_AUTH_DIGEST:
			curlauth = CURLAUTH_DIGEST;
			break;
		case HTTPTEST_AUTH_BEARER:
#if defined(CURLAUTH_BEARER)
			curlauth = CURLAUTH_BEARER;
#else
			ZBX_UNUSED(token);
			*error = zbx_strdup(*error, "cannot set bearer token: cURL library support >= 7.61.0 is"
					" required");
			return FAIL;
#endif
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			break;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HTTPAUTH, curlauth)))
	{
		*error = zbx_dsprintf(*error, "Cannot set HTTP server authentication method: %s",
				curl_easy_strerror(err));
		return FAIL;
	}

	switch (authtype)
	{
#if defined(CURLAUTH_BEARER)
		case HTTPTEST_AUTH_BEARER:
			if (NULL == token || '\0' == *token)
			{
				*error = zbx_dsprintf(*error, "cannot set empty bearer token");
				return FAIL;
			}

			if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_XOAUTH2_BEARER, token)))
			{
				*error = zbx_dsprintf(*error, "Cannot set bearer: %s", curl_easy_strerror(err));
				return FAIL;
			}
			break;
#endif
		default:
			zbx_snprintf(auth, sizeof(auth), "%s:%s", username, password);
			if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_USERPWD, auth)))
			{
				*error = zbx_dsprintf(*error, "Cannot set user name and password: %s",
						curl_easy_strerror(err));
				return FAIL;
			}
			break;
	}

	return SUCCEED;
}

char	*zbx_http_parse_header(char **headers)
{
	while ('\0' != **headers)
	{
		char	c, *p_end, *line;

		while ('\r' == **headers || '\n' == **headers)
			(*headers)++;

		p_end = *headers;

		while ('\0' != *p_end && '\r' != *p_end && '\n' != *p_end)
			p_end++;

		if (*headers == p_end)
			return NULL;

		if ('\0' != (c = *p_end))
			*p_end = '\0';
		line = zbx_strdup(NULL, *headers);
		if ('\0' != c)
			*p_end = c;

		*headers = p_end;

		zbx_lrtrim(line, " \t");
		if ('\0' == *line)
			zbx_free(line);
		else
			return line;
	}

	return NULL;
}

int	zbx_http_get(const char *url, const char *header, long timeout, const char *ssl_cert_file,
		const char *ssl_key_file, char **out, long *response_code, char **error)
{
	CURL			*easyhandle;
	CURLcode		err;
	char			errbuf[CURL_ERROR_SIZE];
	int			ret = FAIL;
	struct curl_slist	*headers_slist = NULL;
	zbx_http_response_t	body = {0}, response_header = {0};

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() URL '%s'", __func__, url);

	*errbuf = '\0';

	if (NULL == (easyhandle = curl_easy_init()))
	{
		*error = zbx_strdup(NULL, "Cannot initialize cURL library");
		goto clean;
	}

	if (SUCCEED != zbx_http_prepare_callbacks(easyhandle, &response_header, &body, zbx_curl_ignore_cb,
			zbx_curl_write_cb, errbuf, error))
	{
		goto clean;
	}

	if (SUCCEED != zbx_http_prepare_ssl(easyhandle, ssl_cert_file, ssl_key_file, "", 1, 1, error))
		goto clean;

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_USERAGENT, "Zabbix " ZABBIX_VERSION)))
	{
		*error = zbx_dsprintf(NULL, "Cannot set user agent: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_PROXY, "")))
	{
		*error = zbx_dsprintf(NULL, "Cannot set proxy: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_TIMEOUT, timeout)))
	{
		*error = zbx_dsprintf(NULL, "Cannot specify timeout: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HTTPHEADER,
			(headers_slist = curl_slist_append(headers_slist, header)))))
	{
		*error = zbx_dsprintf(NULL, "Cannot specify headers: %s", curl_easy_strerror(err));
		goto clean;
	}

#if LIBCURL_VERSION_NUM >= 0x071304
	/* CURLOPT_PROTOCOLS is supported starting with version 7.19.4 (0x071304) */
	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS)))
	{
		*error = zbx_dsprintf(NULL, "Cannot set allowed protocols: %s", curl_easy_strerror(err));
		goto clean;
	}
#endif

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_URL, url)))
	{
		*error = zbx_dsprintf(NULL, "Cannot specify URL: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, ZBX_CURLOPT_ACCEPT_ENCODING, "")))
	{
		*error = zbx_dsprintf(NULL, "Cannot set cURL encoding option: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_perform(easyhandle)))
	{
		*error = zbx_dsprintf(NULL, "Cannot perform request: %s", '\0' == *errbuf ? curl_easy_strerror(err) :
				errbuf);
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_getinfo(easyhandle, CURLINFO_RESPONSE_CODE, response_code)))
	{
		*error = zbx_dsprintf(NULL, "Cannot get the response code: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (NULL != body.data)
	{
		*out = body.data;
		body.data = NULL;
	}

	else
		*out = zbx_strdup(NULL, "");

	ret = SUCCEED;
clean:
	curl_slist_free_all(headers_slist);	/* must be called after curl_easy_perform() */
	curl_easy_cleanup(easyhandle);
	zbx_free(body.data);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

static const char	*zbx_request_string(int result)
{
	switch (result)
	{
		case HTTP_REQUEST_GET:
			return "GET";
		case HTTP_REQUEST_POST:
			return "POST";
		case HTTP_REQUEST_PUT:
			return "PUT";
		case HTTP_REQUEST_HEAD:
			return "HEAD";
		default:
			return "unknown";
	}
}

static int	http_prepare_request(CURL *easyhandle, const char *posts, unsigned char request_method, char **error)
{
	CURLcode	err;

	switch (request_method)
	{
		case HTTP_REQUEST_POST:
			if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_POSTFIELDS, posts)))
			{
				*error = zbx_dsprintf(*error, "Cannot specify data to POST: %s", curl_easy_strerror(err));
				return FAIL;
			}
			break;
		case HTTP_REQUEST_GET:
			if ('\0' == *posts)
				return SUCCEED;

			if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_POSTFIELDS, posts)))
			{
				*error = zbx_dsprintf(*error, "Cannot specify data to POST: %s", curl_easy_strerror(err));
				return FAIL;
			}

			if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_CUSTOMREQUEST, "GET")))
			{
				*error = zbx_dsprintf(*error, "Cannot specify custom GET request: %s",
						curl_easy_strerror(err));
				return FAIL;
			}
			break;
		case HTTP_REQUEST_HEAD:
			if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_NOBODY, 1L)))
			{
				*error = zbx_dsprintf(*error, "Cannot specify HEAD request: %s", curl_easy_strerror(err));
				return FAIL;
			}
			break;
		case HTTP_REQUEST_PUT:
			if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_POSTFIELDS, posts)))
			{
				*error = zbx_dsprintf(*error, "Cannot specify data to POST: %s", curl_easy_strerror(err));
				return FAIL;
			}

			if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_CUSTOMREQUEST, "PUT")))
			{
				*error = zbx_dsprintf(*error, "Cannot specify custom GET request: %s",
						curl_easy_strerror(err));
				return FAIL;
			}
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			*error = zbx_strdup(*error, "Unsupported request method");
			return FAIL;
	}

	return SUCCEED;
}

static void	http_add_json_header(struct zbx_json *json, char *line)
{
	char	*colon;

	if (NULL != (colon = strchr(line, ':')))
	{
		zbx_ltrim(colon + 1, " \t");

		*colon = '\0';
		zbx_json_addstring(json, line, colon + 1, ZBX_JSON_TYPE_STRING);
		*colon = ':';
	}
	else
		zbx_json_addstring(json, line, "", ZBX_JSON_TYPE_STRING);
}

static void	http_output_json(unsigned char retrieve_mode, char **buffer, zbx_http_response_t *header,
		zbx_http_response_t *body)
{
	struct zbx_json		json;
	struct zbx_json_parse	jp;
	char			*headers, *line;
	unsigned char		json_content = 0;

	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

	headers = header->data;

	if (retrieve_mode != ZBX_RETRIEVE_MODE_CONTENT)
		zbx_json_addobject(&json, "header");

	while (NULL != (line = zbx_http_parse_header(&headers)))
	{
		if (0 == json_content &&
				0 == zbx_strncasecmp(line, "Content-Type:", ZBX_CONST_STRLEN("Content-Type:")) &&
				NULL != strstr(line, "application/json"))
		{
			json_content = 1;
		}

		if (retrieve_mode != ZBX_RETRIEVE_MODE_CONTENT)
			http_add_json_header(&json, line);

		zbx_free(line);
	}

	if (retrieve_mode != ZBX_RETRIEVE_MODE_CONTENT)
		zbx_json_close(&json);

	if (NULL != body->data)
	{
		if (0 == json_content)
		{
			zbx_json_addstring(&json, "body", body->data, ZBX_JSON_TYPE_STRING);
		}
		else if (FAIL == zbx_json_open(body->data, &jp))
		{
			zbx_json_addstring(&json, "body", body->data, ZBX_JSON_TYPE_STRING);
			zabbix_log(LOG_LEVEL_DEBUG, "received invalid JSON object %s", zbx_json_strerror());
		}
		else
		{
			zbx_lrtrim(body->data, ZBX_WHITESPACE);
			zbx_json_addraw(&json, "body", body->data);
		}
	}

	*buffer = zbx_strdup(NULL, json.buffer);
	zbx_json_free(&json);
}

int	zbx_http_request(unsigned char request_method, const char *url, const char *query_fields, char *headers,
		const char *posts, unsigned char retrieve_mode, const char *http_proxy, unsigned char follow_redirects,
		const char *timeout, int max_attempts, const char *ssl_cert_file, const char *ssl_key_file,
		const char *ssl_key_password, unsigned char verify_peer, unsigned char verify_host,
		unsigned char authtype, const char *username, const char *password, const char *token,
		unsigned char post_type, char *status_codes, unsigned char output_format, char **out, char **error)
{
	CURL			*easyhandle;
	CURLcode		err;
	char			url_buffer[ZBX_ITEM_URL_LEN_MAX], errbuf[CURL_ERROR_SIZE], *headers_ptr, *line, *buffer;
	int			ret = NOTSUPPORTED, timeout_seconds, found = FAIL;
	long			response_code;
	struct curl_slist	*headers_slist = NULL;
	struct zbx_json		json;
	zbx_http_response_t	body = {0}, header = {0};
	zbx_curl_cb_t		curl_body_cb;
	char			application_json[] = {"Content-Type: application/json"};
	char			application_ndjson[] = {"Content-Type: application/x-ndjson"};
	char			application_xml[] = {"Content-Type: application/xml"};

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() request method '%s' URL '%s%s' headers '%s'",
			__func__, zbx_request_string(request_method), url, query_fields, headers);

	zabbix_log(LOG_LEVEL_TRACE, "message body '%s'", posts);

	if (NULL == (easyhandle = curl_easy_init()))
	{
		*error = zbx_strdup(NULL, "Cannot initialize cURL library");;
		goto clean;
	}

	switch (retrieve_mode)
	{
		case ZBX_RETRIEVE_MODE_CONTENT:
		case ZBX_RETRIEVE_MODE_BOTH:
			curl_body_cb = zbx_curl_write_cb;
			break;
		case ZBX_RETRIEVE_MODE_HEADERS:
			curl_body_cb = zbx_curl_ignore_cb;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			*error = zbx_strdup(NULL, "Invalid retrieve mode");
			goto clean;
	}

	if (SUCCEED != zbx_http_prepare_callbacks(easyhandle, &header, &body, zbx_curl_write_cb, curl_body_cb, errbuf,
			error))
	{
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_PROXY, http_proxy)))
	{
		*error = zbx_dsprintf(NULL, "Cannot set proxy: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_FOLLOWLOCATION,
			0 == follow_redirects ? 0L : 1L)))
	{
		*error = zbx_dsprintf(NULL, "Cannot set follow redirects: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (0 != follow_redirects &&
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_MAXREDIRS, ZBX_CURLOPT_MAXREDIRS)))
	{
		*error = zbx_dsprintf(NULL, "Cannot set number of redirects allowed: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (FAIL == zbx_is_time_suffix(timeout, &timeout_seconds, (int)strlen(timeout)))
	{
		*error = zbx_dsprintf(NULL, "Invalid timeout: %s", timeout);
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_TIMEOUT, (long)timeout_seconds)))
	{
		*error = zbx_dsprintf(NULL, "Cannot specify timeout: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (SUCCEED != zbx_http_prepare_ssl(easyhandle, ssl_cert_file, ssl_key_file, ssl_key_password,
			verify_peer, verify_host, error))
	{
		goto clean;
	}

	if (SUCCEED != zbx_http_prepare_auth(easyhandle, authtype, username, password, token, error))
		goto clean;

	if (SUCCEED != http_prepare_request(easyhandle, posts, request_method, error))
	{
		goto clean;
	}

	headers_ptr = headers;
	while (NULL != (line = zbx_http_parse_header(&headers_ptr)))
	{
		headers_slist = curl_slist_append(headers_slist, line);

		if (FAIL == found && 0 == strncmp(line, "Content-Type:", ZBX_CONST_STRLEN("Content-Type:")))
			found = SUCCEED;

		zbx_free(line);
	}

	if (FAIL == found)
	{
		if (ZBX_POSTTYPE_JSON == post_type)
			headers_slist = curl_slist_append(headers_slist, application_json);
		else if (ZBX_POSTTYPE_XML == post_type)
			headers_slist = curl_slist_append(headers_slist, application_xml);
		else if (ZBX_POSTTYPE_NDJSON == post_type)
			headers_slist = curl_slist_append(headers_slist, application_ndjson);
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HTTPHEADER, headers_slist)))
	{
		*error = zbx_dsprintf(NULL, "Cannot specify headers: %s", curl_easy_strerror(err));
		goto clean;
	}

#if LIBCURL_VERSION_NUM >= 0x071304
	/* CURLOPT_PROTOCOLS is supported starting with version 7.19.4 (0x071304) */
	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS)))
	{
		*error = zbx_dsprintf(NULL, "Cannot set allowed protocols: %s", curl_easy_strerror(err));
		goto clean;
	}
#endif

	zbx_snprintf(url_buffer, sizeof(url_buffer),"%s%s", url, query_fields);
	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_URL, url_buffer)))
	{
		*error = zbx_dsprintf(NULL, "Cannot specify URL: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, ZBX_CURLOPT_ACCEPT_ENCODING, "")))
	{
		*error = zbx_dsprintf(NULL, "Cannot set cURL encoding option: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_COOKIEFILE, "")))
	{
		*error =  zbx_dsprintf(NULL, "Cannot enable cURL cookie engine: %s", curl_easy_strerror(err));
		goto clean;
	}

	/* try to retrieve page several times depending on number of retries */
	do
	{
		*errbuf = '\0';

		if (CURLE_OK == (err = curl_easy_perform(easyhandle)))
		{
			break;
		}
		else
		{
			if (1 != max_attempts)
			{
				zabbix_log(LOG_LEVEL_INFORMATION, "cannot perform request: %s",
						'\0' == *errbuf ? curl_easy_strerror(err) : errbuf);
			}
		}

		header.offset = 0;
		body.offset = 0;
	}
	while (0 < --max_attempts);

	if (CURLE_OK != err)
	{
		if (CURLE_WRITE_ERROR == err)
		{
			*error = zbx_strdup(NULL, "The requested value is too large");
		}
		else
		{
			*error = zbx_dsprintf(NULL, "Cannot perform request: %s",
					'\0' == *errbuf ? curl_easy_strerror(err) : errbuf);
		}
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_getinfo(easyhandle, CURLINFO_RESPONSE_CODE, &response_code)))
	{
		*error = zbx_dsprintf(NULL, "Cannot get the response code: %s", curl_easy_strerror(err));
		goto clean;
	}

	if (NULL == header.data)
	{
		*error = zbx_dsprintf(NULL, "Server returned empty header");
		goto clean;
	}

	switch (retrieve_mode)
	{
		case ZBX_RETRIEVE_MODE_CONTENT:
			if (NULL != body.data && FAIL == zbx_is_utf8(body.data))
			{
				*error = zbx_dsprintf(NULL, "Server returned invalid UTF-8 sequence");
				goto clean;
			}

			if (HTTP_STORE_JSON == output_format)
			{
				http_output_json(retrieve_mode, &buffer, &header, &body);
				*out = buffer;
			}
			else
			{
				if (NULL != body.data)
				{
					*out = body.data;
					body.data = NULL;
				}
				else
					*out = zbx_strdup(NULL, "");
			}
			break;
		case ZBX_RETRIEVE_MODE_HEADERS:
			if (FAIL == zbx_is_utf8(header.data))
			{
				*error = zbx_dsprintf(NULL, "Server returned invalid UTF-8 sequence");
				goto clean;
			}

			if (HTTP_STORE_JSON == output_format)
			{
				zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
				zbx_json_addobject(&json, "header");
				headers_ptr = header.data;
				while (NULL != (line = zbx_http_parse_header(&headers_ptr)))
				{
					http_add_json_header(&json, line);
					zbx_free(line);
				}
				*out = zbx_strdup(NULL, json.buffer);
				zbx_json_free(&json);
			}
			else
			{
				*out = header.data;
				header.data = NULL;
			}
			break;
		case ZBX_RETRIEVE_MODE_BOTH:
			if (FAIL == zbx_is_utf8(header.data) || (NULL != body.data && FAIL == zbx_is_utf8(body.data)))
			{
				*error = zbx_dsprintf(NULL, "Server returned invalid UTF-8 sequence");
				goto clean;
			}

			if (HTTP_STORE_JSON == output_format)
			{
				http_output_json(retrieve_mode, &buffer, &header, &body);
				*out = buffer;
			}
			else
			{
				if (NULL != body.data)
				{
					zbx_strncpy_alloc(&header.data, &header.allocated, &header.offset, body.data,
							body.offset);
				}

				*out = header.data;
				header.data = NULL;
			}
			break;
	}

	if ('\0' != *status_codes && FAIL == zbx_int_in_list(status_codes, (int)response_code))
	{
		*error = zbx_dsprintf(NULL, "Response code \"%ld\" did not match any of the"
				" required status codes \"%s\"", response_code, status_codes);
		goto clean;
	}

	ret = SUCCEED;
clean:
	curl_slist_free_all(headers_slist);	/* must be called after curl_easy_perform() */
	curl_easy_cleanup(easyhandle);
	zbx_free(body.data);
	zbx_free(header.data);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

#endif
