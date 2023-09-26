/*
** Glaber
** Copyright (C) 2001-2028 Glaber JSC
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



//returns retry if still not ready
//or returns FAIL of SUCCEED it tls connection is ready to go
int glb_tls_process_connection_result() {
#if defined (HAVE_GNUTLS) 
#endif
}



int glb_tls_establish_connection() {
#if defined(HAVE_GNUTLS)
   
    /* TLS handshake */
    while (GNUTLS_E_SUCCESS != (res = gnutls_handshake(s->tls_ctx->ctx)))
	{

		if (SUCCEED == zbx_alarm_timed_out())
		{
			*error = zbx_strdup(*error, "gnutls_handshake() timed out");
			goto out;
		}

		if (GNUTLS_E_INTERRUPTED == res || GNUTLS_E_AGAIN == res)
		{
			continue;
		}
		else if (GNUTLS_E_WARNING_ALERT_RECEIVED == res || GNUTLS_E_FATAL_ALERT_RECEIVED == res)
		{
			const char	*msg;
			int		alert;

			/* server sent an alert to us */
			alert = gnutls_alert_get(s->tls_ctx->ctx);

			if (NULL == (msg = gnutls_alert_get_name(alert)))
				msg = "unknown";

			if (GNUTLS_E_WARNING_ALERT_RECEIVED == res)
			{
				zabbix_log(LOG_LEVEL_WARNING, "%s() gnutls_handshake() received a warning alert: %d %s",
						__func__, alert, msg);
				continue;
			}
			else	/* GNUTLS_E_FATAL_ALERT_RECEIVED */
			{
				*error = zbx_dsprintf(*error, "%s(): gnutls_handshake() failed with fatal alert: %d %s",
						__func__, alert, msg);
				goto out;
			}
		}
		else
		{
			int	level;

			/* log "peer has closed connection" case with debug level */
			level = (GNUTLS_E_PREMATURE_TERMINATION == res ? LOG_LEVEL_DEBUG : LOG_LEVEL_WARNING);

			if (SUCCEED == ZBX_CHECK_LOG_LEVEL(level))
			{
				zabbix_log(level, "%s() gnutls_handshake() returned: %d %s",
						__func__, res, gnutls_strerror(res));
			}

			if (0 != gnutls_error_is_fatal(res))
			{
				*error = zbx_dsprintf(*error, "%s(): gnutls_handshake() failed: %d %s",
						__func__, res, gnutls_strerror(res));
				goto out;
			}
		}
	}

#endif




}


#if defined(HAVE_GNUTLS)
int	glb_tls_start_connect(zbx_tls_context_t *tls_ctx, unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2,
		const char *server_name, char **error)
{
	int	ret = FAIL, res;
#if defined(_WINDOWS)
	double	sec;
#endif

	if (ZBX_TCP_SEC_TLS_CERT == tls_connect)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "In %s(): issuer:\"%s\" subject:\"%s\"", __func__,
				ZBX_NULL2EMPTY_STR(tls_arg1), ZBX_NULL2EMPTY_STR(tls_arg2));
	}
	else if (ZBX_TCP_SEC_TLS_PSK == tls_connect)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "In %s(): psk_identity:\"%s\"", __func__, ZBX_NULL2EMPTY_STR(tls_arg1));
	}
	else
	{
		*error = zbx_strdup(*error, "invalid connection parameters");
		THIS_SHOULD_NEVER_HAPPEN;
		goto out1;
	}

	/* set up TLS context */

	tls_ctx = zbx_malloc(s->tls_ctx, sizeof(zbx_tls_context_t));
	tls_ctx->ctx = NULL;
	tls_ctx->psk_client_creds = NULL;
	tls_ctx->psk_server_creds = NULL;

	if (GNUTLS_E_SUCCESS != (res = gnutls_init(&tls_ctx->ctx, GNUTLS_CLIENT | GNUTLS_NO_EXTENSIONS)))
			/* GNUTLS_NO_EXTENSIONS is used because we do not currently support extensions (e.g. session */
			/* tickets and OCSP) */
	{
		*error = zbx_dsprintf(*error, "gnutls_init() failed: %d %s", res, gnutls_strerror(res));
		goto out;
	}

	if (ZBX_TCP_SEC_TLS_CERT == tls_connect)
	{
		if (NULL == ciphersuites_cert)
		{
			*error = zbx_strdup(*error, "cannot connect with TLS and certificate: no valid certificate"
					" loaded");
			goto out;
		}

		if (GNUTLS_E_SUCCESS != (res = gnutls_priority_set(s->tls_ctx->ctx, ciphersuites_cert)))
		{
			*error = zbx_dsprintf(*error, "gnutls_priority_set() for 'ciphersuites_cert' failed: %d %s",
					res, gnutls_strerror(res));
			goto out;
		}

		if (GNUTLS_E_SUCCESS != (res = gnutls_credentials_set(s->tls_ctx->ctx, GNUTLS_CRD_CERTIFICATE,
				my_cert_creds)))
		{
			*error = zbx_dsprintf(*error, "gnutls_credentials_set() for certificate failed: %d %s", res,
					gnutls_strerror(res));
			goto out;
		}
	}
	else	/* use a pre-shared key */
	{
		if (NULL == ciphersuites_psk)
		{
			*error = zbx_strdup(*error, "cannot connect with TLS and PSK: no valid PSK loaded");
			goto out;
		}

		if (GNUTLS_E_SUCCESS != (res = gnutls_priority_set(s->tls_ctx->ctx, ciphersuites_psk)))
		{
			*error = zbx_dsprintf(*error, "gnutls_priority_set() for 'ciphersuites_psk' failed: %d %s", res,
					gnutls_strerror(res));
			goto out;
		}

		if (NULL == tls_arg2)	/* PSK is not set from DB */
		{
			/* set up the PSK from a configuration file (always in agentd and a case in active proxy */
			/* when it connects to server) */

			if (GNUTLS_E_SUCCESS != (res = gnutls_credentials_set(s->tls_ctx->ctx, GNUTLS_CRD_PSK,
					my_psk_client_creds)))
			{
				*error = zbx_dsprintf(*error, "gnutls_credentials_set() for psk failed: %d %s", res,
						gnutls_strerror(res));
				goto out;
			}
		}
		else
		{
			/* PSK comes from a database (case for a server/proxy when it connects to an agent for */
			/* passive checks, for a server when it connects to a passive proxy) */

			gnutls_datum_t	key;
			int		psk_len;
			unsigned char	psk_buf[HOST_TLS_PSK_LEN / 2];

			if (0 >= (psk_len = zbx_hex2bin((const unsigned char *)tls_arg2, psk_buf, sizeof(psk_buf))))
			{
				*error = zbx_strdup(*error, "invalid PSK");
				goto out;
			}

			if (GNUTLS_E_SUCCESS != (res = gnutls_psk_allocate_client_credentials(
					&s->tls_ctx->psk_client_creds)))
			{
				*error = zbx_dsprintf(*error, "gnutls_psk_allocate_client_credentials() failed: %d %s",
						res, gnutls_strerror(res));
				goto out;
			}

			key.data = psk_buf;
			key.size = (unsigned int)psk_len;

			/* Simplified. 'tls_arg1' (PSK identity) should have been prepared as required by RFC 4518. */
			if (GNUTLS_E_SUCCESS != (res = gnutls_psk_set_client_credentials(tls_ctx->psk_client_creds,
					tls_arg1, &key, GNUTLS_PSK_KEY_RAW)))
			{
				*error = zbx_dsprintf(*error, "gnutls_psk_set_client_credentials() failed: %d %s", res,
						gnutls_strerror(res));
				goto out;
			}

			if (GNUTLS_E_SUCCESS != (res = gnutls_credentials_set(tls_ctx->ctx, GNUTLS_CRD_PSK,
					tls_ctx->psk_client_creds)))
			{
				*error = zbx_dsprintf(*error, "gnutls_credentials_set() for psk failed: %d %s", res,
						gnutls_strerror(res));
				goto out;
			}
		}
	}

	if (NULL != server_name && ZBX_TCP_SEC_UNENCRYPTED != tls_connect && GNUTLS_E_SUCCESS != gnutls_server_name_set(
			s->tls_ctx->ctx, GNUTLS_NAME_DNS, server_name, strlen(server_name)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot set %s tls host name", server_name);
	}

	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
	{
		/* set our own debug callback function */
		gnutls_global_set_log_function(zbx_gnutls_debug_cb);

		/* for Zabbix LOG_LEVEL_TRACE, GnuTLS debug level 4 seems the best */
		/* (the highest GnuTLS debug level is 9) */
		gnutls_global_set_log_level(4);
	}
	else
		gnutls_global_set_log_level(0);		/* restore default log level */

	/* set our own callback function to log issues into Zabbix log */
	gnutls_global_set_audit_log_function(zbx_gnutls_audit_cb);

	gnutls_transport_set_int(s->tls_ctx->ctx, ZBX_SOCKET_TO_INT(s->socket));

	
ASYNC SPLIT GOES HERE: SHOULD CALL INITIAL handshake, but res
has to be processed int the socket handler
    
    /* TLS handshake */
    while (GNUTLS_E_SUCCESS != (res = gnutls_handshake(s->tls_ctx->ctx)))
	{

		if (SUCCEED == zbx_alarm_timed_out())
		{
			*error = zbx_strdup(*error, "gnutls_handshake() timed out");
			goto out;
		}

		if (GNUTLS_E_INTERRUPTED == res || GNUTLS_E_AGAIN == res)
		{
			continue;
		}
		else if (GNUTLS_E_WARNING_ALERT_RECEIVED == res || GNUTLS_E_FATAL_ALERT_RECEIVED == res)
		{
			const char	*msg;
			int		alert;

			/* server sent an alert to us */
			alert = gnutls_alert_get(s->tls_ctx->ctx);

			if (NULL == (msg = gnutls_alert_get_name(alert)))
				msg = "unknown";

			if (GNUTLS_E_WARNING_ALERT_RECEIVED == res)
			{
				zabbix_log(LOG_LEVEL_WARNING, "%s() gnutls_handshake() received a warning alert: %d %s",
						__func__, alert, msg);
				continue;
			}
			else	/* GNUTLS_E_FATAL_ALERT_RECEIVED */
			{
				*error = zbx_dsprintf(*error, "%s(): gnutls_handshake() failed with fatal alert: %d %s",
						__func__, alert, msg);
				goto out;
			}
		}
		else
		{
			int	level;

			/* log "peer has closed connection" case with debug level */
			level = (GNUTLS_E_PREMATURE_TERMINATION == res ? LOG_LEVEL_DEBUG : LOG_LEVEL_WARNING);

			if (SUCCEED == ZBX_CHECK_LOG_LEVEL(level))
			{
				zabbix_log(level, "%s() gnutls_handshake() returned: %d %s",
						__func__, res, gnutls_strerror(res));
			}

			if (0 != gnutls_error_is_fatal(res))
			{
				*error = zbx_dsprintf(*error, "%s(): gnutls_handshake() failed: %d %s",
						__func__, res, gnutls_strerror(res));
				goto out;
			}
		}
	}
###
	if (ZBX_TCP_SEC_TLS_CERT == tls_connect)
	{
		/* log peer certificate information for debugging */
		zbx_log_peer_cert(__func__, s->tls_ctx);

		/* perform basic verification of peer certificate */
		if (SUCCEED != zbx_verify_peer_cert(s->tls_ctx->ctx, error))
		{
			zbx_tls_close(s);
			goto out1;
		}

		/* if required verify peer certificate Issuer and Subject */
		if (SUCCEED != zbx_verify_issuer_subject(s->tls_ctx, tls_arg1, tls_arg2, error))
		{
			zbx_tls_close(s);
			goto out1;
		}
	}

	s->connection_type = tls_connect;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():SUCCEED (established %s %s-%s-%s-" ZBX_FS_SIZE_T ")", __func__,
			gnutls_protocol_get_name(gnutls_protocol_get_version(s->tls_ctx->ctx)),
			gnutls_kx_get_name(gnutls_kx_get(s->tls_ctx->ctx)),
			gnutls_cipher_get_name(gnutls_cipher_get(s->tls_ctx->ctx)),
			gnutls_mac_get_name(gnutls_mac_get(s->tls_ctx->ctx)),
			(zbx_fs_size_t)gnutls_mac_get_key_size(gnutls_mac_get(s->tls_ctx->ctx)));

	return SUCCEED;

out:	/* an error occurred */
	if (NULL != s->tls_ctx->ctx)
	{
		gnutls_credentials_clear(s->tls_ctx->ctx);
		gnutls_deinit(s->tls_ctx->ctx);
	}

	if (NULL != s->tls_ctx->psk_client_creds)
		gnutls_psk_free_client_credentials(s->tls_ctx->psk_client_creds);

	zbx_free(s->tls_ctx);
out1:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __func__, zbx_result_string(ret),
			ZBX_NULL2EMPTY_STR(*error));
	return ret;
}
#elif defined(HAVE_OPENSSL)
static int	zbx_tls_get_error(const SSL *s, int res, const char *func, size_t *error_alloc, size_t *error_offset,
		char **error)
{
	int	result_code;

	result_code = SSL_get_error(s, res);

	switch (result_code)
	{
		case SSL_ERROR_NONE:		/* handshake successful */
			return SUCCEED;
		case SSL_ERROR_ZERO_RETURN:
			zbx_snprintf_alloc(error, error_alloc, error_offset,
					"%s() TLS connection has been closed during read", func);
			return FAIL;
		case SSL_ERROR_SYSCALL:
			if (0 == ERR_peek_error())
			{
				if (0 == res)
				{
					zbx_snprintf_alloc(error, error_alloc, error_offset,
							"%s() connection closed by peer", func);
				}
				else if (-1 == res)
				{
					zbx_snprintf_alloc(error, error_alloc, error_offset, "%s()"
							" I/O error: %s", func,
							strerror_from_system(zbx_socket_last_error()));
				}
				else
				{
					/* "man SSL_get_error" describes only res == 0 and res == -1 for */
					/* SSL_ERROR_SYSCALL case */
					zbx_snprintf_alloc(error, error_alloc, error_offset, "%s()"
							" returned undocumented code %d", func, res);
				}
			}
			else
			{
				zbx_snprintf_alloc(error, error_alloc, error_offset, "%s() set"
						" result code to SSL_ERROR_SYSCALL:", func);
				zbx_tls_error_msg(error, error_alloc, error_offset);
				zbx_snprintf_alloc(error, error_alloc, error_offset, "%s", info_buf);
			}
			return FAIL;
		case SSL_ERROR_SSL:
			zbx_snprintf_alloc(error, error_alloc, error_offset, "%s() set"
					" result code to SSL_ERROR_SSL:", func);
			zbx_tls_error_msg(error, error_alloc, error_offset);
			zbx_snprintf_alloc(error, error_alloc, error_offset, "%s", info_buf);
			return FAIL;
		default:
			zbx_snprintf_alloc(error, error_alloc, error_offset, "%s() set result code"
					" to %d", func, result_code);
			zbx_tls_error_msg(error, error_alloc, error_offset);
			zbx_snprintf_alloc(error, error_alloc, error_offset, "%s", info_buf);
			return FAIL;
	}
}

int	zbx_tls_connect(zbx_socket_t *s, unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2,
		const char *server_name, char **error)
{
	int	ret = FAIL, res;
	size_t	error_alloc = 0, error_offset = 0;

#if defined(HAVE_OPENSSL_WITH_PSK)
	char	psk_buf[HOST_TLS_PSK_LEN / 2];
#endif

	s->tls_ctx = zbx_malloc(s->tls_ctx, sizeof(zbx_tls_context_t));
	s->tls_ctx->ctx = NULL;

	if (ZBX_TCP_SEC_TLS_CERT == tls_connect)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "In %s(): issuer:\"%s\" subject:\"%s\"", __func__,
				ZBX_NULL2EMPTY_STR(tls_arg1), ZBX_NULL2EMPTY_STR(tls_arg2));

		if (NULL == ctx_cert)
		{
			*error = zbx_strdup(*error, "cannot connect with TLS and certificate: no valid certificate"
					" loaded");
			goto out;
		}

		if (NULL == (s->tls_ctx->ctx = SSL_new(ctx_cert)))
		{
			zbx_snprintf_alloc(error, &error_alloc, &error_offset, "cannot create connection context:");
			zbx_tls_error_msg(error, &error_alloc, &error_offset);
			goto out;
		}
	}
	else if (ZBX_TCP_SEC_TLS_PSK == tls_connect)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "In %s(): psk_identity:\"%s\"", __func__, ZBX_NULL2EMPTY_STR(tls_arg1));

#if defined(HAVE_OPENSSL_WITH_PSK)
		if (NULL == ctx_psk)
		{
			*error = zbx_strdup(*error, "cannot connect with TLS and PSK: no valid PSK loaded");
			goto out;
		}

		if (NULL == (s->tls_ctx->ctx = SSL_new(ctx_psk)))
		{
			zbx_snprintf_alloc(error, &error_alloc, &error_offset, "cannot create connection context:");
			zbx_tls_error_msg(error, &error_alloc, &error_offset);
			goto out;
		}

		if (NULL == tls_arg2)	/* PSK is not set from DB */
		{
			/* Set up PSK global variables from a configuration file (always in agentd and a case when */
			/* active proxy connects to server). Here we set it only in case of active proxy */
			/* because for other programs it has already been set in zbx_tls_init_child(). */

			if (0 != (zbx_get_program_type_cb() & ZBX_PROGRAM_TYPE_PROXY_ACTIVE))
			{
				psk_identity_for_cb = my_psk_identity;
				psk_identity_len_for_cb = my_psk_identity_len;
				psk_for_cb = my_psk;
				psk_len_for_cb = my_psk_len;
			}
		}
		else
		{
			/* PSK comes from a database (case for a server/proxy when it connects to an agent for */
			/* passive checks, for a server when it connects to a passive proxy) */

			int	psk_len;

			if (0 >= (psk_len = zbx_hex2bin((const unsigned char *)tls_arg2, (unsigned char *)psk_buf,
					sizeof(psk_buf))))
			{
				*error = zbx_strdup(*error, "invalid PSK");
				goto out;
			}

			/* some data reside in stack but it will be available at the time when a PSK client callback */
			/* function copies the data into buffers provided by OpenSSL within the callback */
			psk_identity_for_cb = tls_arg1;			/* string is on stack */
			/* NULL check to silence analyzer warning */
			psk_identity_len_for_cb = (NULL == tls_arg1 ? 0 : strlen(tls_arg1));
			psk_for_cb = psk_buf;				/* buffer is on stack */
			psk_len_for_cb = (size_t)psk_len;
		}
#else
		*error = zbx_strdup(*error, "cannot connect with TLS and PSK: support for PSK was not compiled in");
		goto out;
#endif
	}
	else
	{
		*error = zbx_strdup(*error, "invalid connection parameters");
		THIS_SHOULD_NEVER_HAPPEN;
		goto out1;
	}

	if (NULL != server_name && ZBX_TCP_SEC_UNENCRYPTED != tls_connect && 1 != SSL_set_tlsext_host_name(
			s->tls_ctx->ctx, server_name))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot set %s tls host name", server_name);
	}

	/* set our connected TCP socket to TLS context */
	if (1 != SSL_set_fd(s->tls_ctx->ctx, s->socket))
	{
		*error = zbx_strdup(*error, "cannot set socket for TLS context");
		goto out;
	}

	/* TLS handshake */

	info_buf[0] = '\0';	/* empty buffer for zbx_openssl_info_cb() messages */

#### here is connection tracking

	if (1 != (res = SSL_connect(s->tls_ctx->ctx)))
	{

		if (SUCCEED == zbx_alarm_timed_out())
		{
			*error = zbx_strdup(*error, "SSL_connect() timed out");
			goto out;
		}

		if (ZBX_TCP_SEC_TLS_CERT == tls_connect)
		{
			long	verify_result;

			/* In case of certificate error SSL_get_verify_result() provides more helpful diagnostics */
			/* than other methods. Include it as first but continue with other diagnostics. */
			if (X509_V_OK != (verify_result = SSL_get_verify_result(s->tls_ctx->ctx)))
			{
				zbx_snprintf_alloc(error, &error_alloc, &error_offset, "%s: ",
						X509_verify_cert_error_string(verify_result));
			}
		}

		if (FAIL == zbx_tls_get_error(s->tls_ctx->ctx, res, "SSL_connect", &error_alloc, &error_offset, error))
			goto out;
	}

	###here is the result processing

	if (ZBX_TCP_SEC_TLS_CERT == tls_connect)
	{
		long	verify_result;

		/* log peer certificate information for debugging */
		zbx_log_peer_cert(__func__, s->tls_ctx);

		/* perform basic verification of peer certificate */
		if (X509_V_OK != (verify_result = SSL_get_verify_result(s->tls_ctx->ctx)))
		{
			zbx_snprintf_alloc(error, &error_alloc, &error_offset, "%s",
					X509_verify_cert_error_string(verify_result));
			zbx_tls_close(s);
			goto out1;
		}

		/* if required verify peer certificate Issuer and Subject */
		if (SUCCEED != zbx_verify_issuer_subject(s->tls_ctx, tls_arg1, tls_arg2, error))
		{
			zbx_tls_close(s);
			goto out1;
		}
	}

	s->connection_type = tls_connect;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():SUCCEED (established %s %s)", __func__,
			SSL_get_version(s->tls_ctx->ctx), SSL_get_cipher(s->tls_ctx->ctx));

	return SUCCEED;

out:	/* an error occurred */
	if (NULL != s->tls_ctx->ctx)
		SSL_free(s->tls_ctx->ctx);

	zbx_free(s->tls_ctx);
out1:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __func__, zbx_result_string(ret),
			ZBX_NULL2EMPTY_STR(*error));
	return ret;
}
#endif
