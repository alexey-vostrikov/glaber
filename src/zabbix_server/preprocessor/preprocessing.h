/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
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

#ifndef ZABBIX_PREPROCESSING_H
#define ZABBIX_PREPROCESSING_H

#include "common.h"
#include "module.h"
#include "dbcache.h"
#include "preproc.h"
#include "zbxalgo.h"

#define ZBX_IPC_SERVICE_PREPROCESSING	"preprocessing"
#define ZBX_IPC_SERVICE_PREPROCESSING_WORKER	"preprocessing_worker"

#define ZBX_IPC_PREPROCESSOR_WORKER		1
#define ZBX_IPC_PREPROCESSOR_REQUEST		2
#define ZBX_IPC_PREPROCESSOR_RESULT		3
#define ZBX_IPC_PREPROCESSOR_QUEUE		4
#define ZBX_IPC_PREPROCESSOR_TEST_REQUEST	5
#define ZBX_IPC_PREPROCESSOR_TEST_RESULT	6
#define ZBX_IPC_PREPROCESSOR_DIAG_STATS		7
#define ZBX_IPC_PREPROCESSOR_DIAG_STATS_RESULT	8
#define ZBX_IPC_PREPROCESSOR_TOP_ITEMS		9
#define ZBX_IPC_PREPROCESSOR_TOP_ITEMS_RESULT	10

typedef struct {
	AGENT_RESULT	*result;
	int		refcount;
}zbx_result_ptr_t;

/* item value data used in preprocessing manager */
typedef struct
{
	zbx_uint64_t		itemid;		 /* item id */
	unsigned char		item_value_type; /* item value type */
	zbx_result_ptr_t	*result_ptr;	 /* item value (if any) to be shared between master and dependent items */
	zbx_timespec_t		*ts;		 /* timestamp of a value */
	char			*error;		 /* error message (if any) */
	unsigned char		item_flags;	 /* item flags */
	unsigned char		state;		 /* item state */
}
zbx_preproc_item_value_t;

typedef enum
{
	REQUEST_STATE_QUEUED		= 0,		/* requires preprocessing */
	REQUEST_STATE_PROCESSING	= 1,		/* is being preprocessed  */
	REQUEST_STATE_DONE		= 2,		/* value is set, waiting for flush */
	REQUEST_STATE_PENDING		= 3		/* value requires preprocessing, */
							/* but is waiting on other request to complete */
}
zbx_preprocessing_states_t;
/* preprocessing request */
typedef struct preprocessing_request
{
	zbx_preprocessing_states_t	state;		/* request state */
	struct preprocessing_request	*pending;	/* the request waiting on this request to complete */
	zbx_preproc_item_value_t	value;		/* unpacked item value */
	zbx_preproc_op_t		*steps;		/* preprocessing steps */
	int				steps_num;	/* number of preprocessing steps */
	unsigned char			value_type;	/* value type from configuration */
							/* at the beginning of preprocessing queue */
}
zbx_preprocessing_request_t;

zbx_uint32_t	zbx_preprocessor_pack_task(unsigned char **data, zbx_uint64_t itemid, unsigned char value_type,
		zbx_timespec_t *ts, zbx_variant_t *value, const zbx_vector_ptr_t *history,
		const zbx_preproc_op_t *steps, int steps_num);
zbx_uint32_t	zbx_preprocessor_pack_result(unsigned char **data, zbx_variant_t *value,
		const zbx_vector_ptr_t *history, char *error);

zbx_uint32_t	zbx_preprocessor_unpack_value(zbx_preproc_item_value_t *value, unsigned char *data);
void	zbx_preprocessor_unpack_task(zbx_uint64_t *itemid, unsigned char *value_type, zbx_timespec_t **ts,
		zbx_variant_t *value, zbx_vector_ptr_t *history, zbx_preproc_op_t **steps,
		int *steps_num, const unsigned char *data);
void	zbx_preprocessor_unpack_result(zbx_variant_t *value, zbx_vector_ptr_t *history, char **error,
		const unsigned char *data);

void	zbx_preprocessor_unpack_test_request(unsigned char *value_type, char **value, zbx_timespec_t *ts,
		zbx_vector_ptr_t *history, zbx_preproc_op_t **steps, int *steps_num, const unsigned char *data);

zbx_uint32_t	zbx_preprocessor_pack_test_result(unsigned char **data, const zbx_preproc_result_t *results,
		int results_num, const zbx_vector_ptr_t *history, const char *error);

void	zbx_preprocessor_unpack_test_result(zbx_vector_ptr_t *results, zbx_vector_ptr_t *history,
		char **error, const unsigned char *data);

zbx_uint32_t	zbx_preprocessor_pack_diag_stats(unsigned char **data, int values_num, int values_preproc_num);

void	zbx_preprocessor_unpack_diag_stats(int *values_num, int *values_preproc_num, const unsigned char *data);

zbx_uint32_t	zbx_preprocessor_pack_top_items_request(unsigned char **data, int limit);

void	zbx_preprocessor_unpack_top_request(int *limit, const unsigned char *data);

zbx_uint32_t	zbx_preprocessor_pack_top_items_result(unsigned char **data, zbx_preproc_item_stats_t **items,
		int items_num);

void	zbx_preprocessor_unpack_top_result(zbx_vector_ptr_t *items, const unsigned char *data);
int	preprocessor_set_variant_result(zbx_preprocessing_request_t *request, zbx_variant_t *value, char *error);
void	preprocessor_flush_value(const zbx_preproc_item_value_t *value);
void	preprocessor_free_request(zbx_preprocessing_request_t *request);
void	request_free_steps(zbx_preprocessing_request_t *request);
void 	preproc_item_value_clear(zbx_preproc_item_value_t *value);
int	worker_item_preproc_execute(unsigned char value_type, zbx_variant_t *value, const zbx_timespec_t *ts,
		zbx_preproc_op_t *steps, int steps_num, zbx_vector_ptr_t *history_in, zbx_vector_ptr_t *history_out,
		zbx_preproc_result_t *results, int *results_num, char **error);
void	worker_format_error(const zbx_variant_t *value, zbx_preproc_result_t *results, int results_num,
		const char *errmsg, char **error);
void	preproc_item_clear(zbx_preproc_item_t *item);
#endif /* ZABBIX_PREPROCESSING_H */
