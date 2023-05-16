#include "glb_api_conf.h"
#include "zbxcommon.h"
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/http.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include "log.h"
#include "openssl_hostname_validation.h"
#include "zbxstr.h"

extern char *CONFIG_API_URL;
extern char *CONFIG_API_TOKEN;
extern int CONFIG_TIMEOUT;

static struct evhttp_uri *http_uri = NULL;
static int port;
char *auth_token = NULL;
enum { HTTP, HTTPS } type = HTTP;

static int ignore_cert = 0;

/* See http://archives.seul.org/libevent/users/Jan-2013/msg00039.html */
static int cert_verify_callback(X509_STORE_CTX *x509_ctx, void *arg)
{
	char cert_str[256];
	const char *host = (const char *) arg;
	const char *res_str = "X509_verify_cert failed";
	HostnameValidationResult res = Error;

	/* This is the function that OpenSSL would call if we hadn't called
	 * SSL_CTX_set_cert_verify_callback().  Therefore, we are "wrapping"
	 * the default functionality, rather than replacing it. */
	int ok_so_far = 0;

	X509 *server_cert = NULL;

	if (ignore_cert) {
		return 1;
	}

	ok_so_far = X509_verify_cert(x509_ctx);

	server_cert = X509_STORE_CTX_get_current_cert(x509_ctx);

	if (ok_so_far) {
		res = validate_hostname(host, server_cert);

		switch (res) {
		case MatchFound:
			res_str = "MatchFound";
			break;
		case MatchNotFound:
			res_str = "MatchNotFound";
			break;
		case NoSANPresent:
			res_str = "NoSANPresent";
			break;
		case MalformedCertificate:
			res_str = "MalformedCertificate";
			break;
		case Error:
			res_str = "Error";
			break;
		default:
			res_str = "WTF!";
			break;
		}
	}

	X509_NAME_oneline(X509_get_subject_name (server_cert),
			  cert_str, sizeof (cert_str));

	if (res == MatchFound) {
		printf("https server '%s' has this certificate, "
		       "which looks good to me:\n%s\n",
		       host, cert_str);
		return 1;
	} else {
		printf("Got '%s' for hostname '%s' and certificate:\n%s\n",
		       res_str, host, cert_str);
		return 0;
	}
}


int glb_api_conf_init(char *api_url, char* auth) {
   
    const char *scheme, *host;

    if (NULL == api_url || NULL == auth_token)
    return FAIL;

    http_uri = evhttp_uri_parse(api_url);
	
    if (http_uri == NULL) {
		LOG_INF("Malformed url %s", api_url);
		return FAIL;
	}
    scheme = evhttp_uri_get_scheme(http_uri);

    if (scheme == NULL || (strcasecmp(scheme, "https") != 0 &&
	                       strcasecmp(scheme, "http") != 0)) {
		LOG_INF("API url must be http or https");
		return FAIL;
	}

   	host = evhttp_uri_get_host(http_uri);
	if (host == NULL) {
		LOG_WRN("API url must have a host");
		return FAIL;
	}

    port = evhttp_uri_get_port(http_uri);
	if (port == -1) {
		port = (strcasecmp(scheme, "http") == 0) ? 80 : 443;
	}

    auth_token = zbx_strdup(NULL, auth);

    return SUCCEED;
}

//reads set of objects from API, uses async http/https iface needs event-based loop either
int glb_api_conf_async_read_objects_from_api(char *request, char *slug, api_cache_mode_t cache_type, api_response_cb_func_t callback) {
    char uri[256];
    const char *path, *query, *host;
    int r;
    SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
    const char *crt = NULL;


    path = evhttp_uri_get_path(http_uri);
	if (strlen(path) == 0) {
		path = "/";
	}

	query = evhttp_uri_get_query(http_uri);
	if (query == NULL) {
		zbx_snprintf(uri, sizeof(uri) - 1, "%s", path);
	} else {
		zbx_snprintf(uri, sizeof(uri) - 1, "%s?%s", path, query);
	}
	uri[sizeof(uri) - 1] = '\0';

#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || \
	(defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x20700000L)
	// Initialize OpenSSL
	SSL_library_init();
	ERR_load_crypto_strings();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
#endif

	/* This isn't strictly necessary... OpenSSL performs RAND_poll
	 * automatically on first use of random number generator. */
	r = RAND_poll();

	if (r == 0) {
		LOG_INF("RAND_poll is 0");
		return FAIL;
	}

	/* Create a new OpenSSL context */
	ssl_ctx = SSL_CTX_new(SSLv23_method());
	if (!ssl_ctx) {
		LOG_INF("SSL_CTX_new error");
		return FAIL;
	}

	if (crt == NULL) {
		X509_STORE *store;
		/* Attempt to use the system's trusted root certificates. */
		store = SSL_CTX_get_cert_store(ssl_ctx);

        if (X509_STORE_set_default_paths(store) != 1) {
			LOG_WRN("X509_STORE_set_default_paths");
			return FAIL;
		}

    } else {
		if (SSL_CTX_load_verify_locations(ssl_ctx, crt, NULL) != 1) {
			LOG_WRN("SSL_CTX_load_verify_locations");
			return FAIL;
		}
	}

    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_cert_verify_callback(ssl_ctx, cert_verify_callback,
					  (void *) host);


/*	#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
	// Set hostname for SNI extension
	SSL_set_tlsext_host_name(ssl, host);
	#endif

	if (strcasecmp(scheme, "http") == 0) {
		bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
	} else {
		type = HTTPS;
		bev = bufferevent_openssl_socket_new(base, -1, ssl,
			BUFFEREVENT_SSL_CONNECTING,
			BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
	}

	if (bev == NULL) {
		fprintf(stderr, "bufferevent_openssl_socket_new() failed\n");
		goto error;
	}

	bufferevent_openssl_set_allow_dirty_shutdown(bev, 1);

	// For simplicity, we let DNS resolution block. Everything else should be
	// asynchronous though.
	evcon = evhttp_connection_base_bufferevent_new(base, NULL, bev,
		host, port);
	if (evcon == NULL) {
		fprintf(stderr, "evhttp_connection_base_bufferevent_new() failed\n");
		goto error;
	}

	if (retries > 0) {
		evhttp_connection_set_retries(evcon, retries);
	}
	if (timeout >= 0) {
		evhttp_connection_set_timeout(evcon, timeout);
	}

	// Fire off the request
	req = evhttp_request_new(http_request_done, bev);
	if (req == NULL) {
		fprintf(stderr, "evhttp_request_new() failed\n");
		goto error;
	}

	output_headers = evhttp_request_get_output_headers(req);
	evhttp_add_header(output_headers, "Host", host);
	evhttp_add_header(output_headers, "Connection", "close");

	if (data_file) {
		//NOTE: In production code, you'd probably want to use
		// evbuffer_add_file() or evbuffer_add_file_segment(), to
		// avoid needless copying. 
		FILE * f = fopen(data_file, "rb");
		char buf[1024];
		size_t s;
		size_t bytes = 0;

		if (!f) {
			syntax();
			goto error;
		}

		output_buffer = evhttp_request_get_output_buffer(req);
		while ((s = fread(buf, 1, sizeof(buf), f)) > 0) {
			evbuffer_add(output_buffer, buf, s);
			bytes += s;
		}
		evutil_snprintf(buf, sizeof(buf)-1, "%lu", (unsigned long)bytes);
		evhttp_add_header(output_headers, "Content-Length", buf);
		fclose(f);
	}

	r = evhttp_make_request(evcon, req, data_file ? EVHTTP_REQ_POST : EVHTTP_REQ_GET, uri);
	if (r != 0) {
		fprintf(stderr, "evhttp_make_request() failed\n");
		goto error;
	}

	event_base_dispatch(base);
	goto cleanup;
*/
    return SUCCEED;
}

static int respond_from_cache(char *slug, char **response, api_cache_mode_t cache_mode) {
    return FAIL;
}


static int save_to_cache(char *slug, char *response, api_cache_mode_t cache_mode) {
    return FAIL;
}

typedef struct
{
	char	*data;
	size_t	alloc;
	size_t	offset;
}
response_buffer_t;

static size_t	curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t	r_size = size * nmemb;

	response_buffer_t *page = userdata;
	zbx_strncpy_alloc(&page->data, &page->alloc, &page->offset, ptr, r_size);

	return r_size;
}

static int curl_post_api_request(char *url, char *postdata, char **responce) {
 	
	static response_buffer_t page_r = {0};
	struct curl_slist	*headers = NULL;
	CURLcode		err;
	CURL	*handle = NULL;
	char  errbuf[CURL_ERROR_SIZE], auth_buf[MAX_STRING_LEN];
	// if (page_r.alloc > MAX_REASONABLE_BUFFER_SIZE) {
	// 	zbx_free(page_r.data);
	// 	bzero(&page_r,sizeof(zbx_httppage_t));	
	// }

	if (NULL == (handle = curl_easy_init()))
	{
		LOG_WRN("cannot initialize cURL session");
		return FAIL;
	} 
	/*  --header 'Authorization: Bearer ${AUTHORIZATION_TOKEN}' \   
	    --header 'Content-Type: application/json-rpc' \*/

	zbx_snprintf(auth_buf, MAX_STRING_LEN, "Authorization: Bearer %s", CONFIG_API_TOKEN);

	headers = curl_slist_append(headers, auth_buf);
  	headers = curl_slist_append(headers, "Content-Type: application/json-rpc");
	LOG_INF("Sending request %s", postdata);


	curl_easy_setopt(handle, CURLOPT_URL, CONFIG_API_URL);
	curl_easy_setopt(handle, CURLOPT_POSTFIELDS, postdata);
	//curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "gzip");
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &page_r);
	curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10L);

	page_r.offset = 0;
	*errbuf = '\0';

	if (CURLE_OK != (err = curl_easy_perform(handle)))
	{

	//	clickhouse_log_error(handle, err, errbuf,&page_r);
        LOG_WRN("Failed url '%s' postdata '%s' ",url, postdata);
	} 
	//LOG_DBG("Recieved from clickhouse: %s", page_r.data);	

	curl_easy_cleanup(handle);
	curl_slist_free_all(headers);
	
	if (CURLE_OK != err) 
		return FAIL;
	
	*responce = page_r.data;
	return SUCCEED;
}


int glb_api_conf_sync_request(char *request, char **response, char *slug, api_cache_mode_t cache_mode) {
    
    if (SUCCEED == respond_from_cache(slug, response, cache_mode))
        return SUCCEED;

	if (SUCCEED != curl_post_api_request(CONFIG_API_URL, request, response)) 
		return FAIL;
	
	save_to_cache(slug, *response, cache_mode);
	return SUCCEED;

    //making the actual request
}
