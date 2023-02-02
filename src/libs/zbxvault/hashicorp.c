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

#include "hashicorp.h"

#include "zbxkvs.h"
#include "zbxjson.h"
#include "zbxhttp.h"
#include "zbxstr.h"

int	zbx_hashicorp_kvs_get(const char *vault_url, const char *token, const char *ssl_cert_file,
		const char *ssl_key_file, const char *path, long timeout, zbx_kvs_t *kvs, char **error)
{
#ifndef HAVE_LIBCURL
	ZBX_UNUSED(vault_url);
	ZBX_UNUSED(token);
	ZBX_UNUSED(ssl_cert_file);
	ZBX_UNUSED(ssl_key_file);
	ZBX_UNUSED(path);
	ZBX_UNUSED(timeout);
	ZBX_UNUSED(kvs);
	*error = zbx_dsprintf(*error, "missing cURL library");
	return FAIL;
#else
	char			*out = NULL, *url, header[MAX_STRING_LEN], *left, *right;
	struct zbx_json_parse	jp, jp_data, jp_data_data;
	int			ret = FAIL;
	long			response_code;

	if (NULL == token)
	{
		*error = zbx_dsprintf(*error, "\"VaultToken\" configuration parameter or \"VAULT_TOKEN\" environment"
				" variable should be defined");
		return FAIL;
	}

	zbx_strsplit_first(path, '/', &left, &right);
	if (NULL == right)
	{
		*error = zbx_dsprintf(*error, "cannot find separator \"\\\" in path");
		free(left);
		return FAIL;
	}
	url = zbx_dsprintf(NULL, "%s/v1/%s/data/%s", vault_url, left, right);
	zbx_free(right);
	zbx_free(left);

	zbx_snprintf(header, sizeof(header), "X-Vault-Token: %s", token);

	if (SUCCEED != zbx_http_get(url, header, timeout, ssl_cert_file, ssl_key_file, &out, &response_code, error))
		goto fail;

	if (200 != response_code && 204 != response_code)
	{
		*error = zbx_dsprintf(*error, "unsuccessful response code \"%ld\"", response_code);
		goto fail;
	}

	if (SUCCEED != zbx_json_open(out, &jp))
	{
		*error = zbx_dsprintf(*error, "cannot parse secrets from vault: %s", zbx_json_strerror());
		goto fail;
	}

	if (SUCCEED != zbx_json_brackets_by_name(&jp, "data", &jp_data))
	{
		*error = zbx_dsprintf(*error, "cannot find the \"%s\" object in the received JSON object.",
				ZBX_PROTO_TAG_DATA);
		goto fail;
	}

	if (SUCCEED != zbx_json_brackets_by_name(&jp_data, "data", &jp_data_data))
	{
		*error = zbx_dsprintf(*error, "cannot find the \"%s\" object in the received \"%s\" JSON object.",
				ZBX_PROTO_TAG_DATA, ZBX_PROTO_TAG_DATA);
		goto fail;
	}

	zbx_kvs_from_json_get(&jp_data_data, kvs);

	ret = SUCCEED;
fail:
	zbx_free(url);
	zbx_free(out);

	return ret;
#endif
}
