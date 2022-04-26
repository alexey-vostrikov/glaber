/*
** Copyright Glaber
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

#include "zbxalgo.h"
#include "log.h"
#include "proc_trends.h"
#include "zbxhistory.h"

static zbx_hashset_t trends = {0};
static int last_trends_cleanup_hour = 0; //if trends aren't coming, they must be exported/cleaned by the end of hour

static trend_t * get_trend(metric_t *metric, int now_hour, unsigned char value_type) {
    trend_t *trend, trend_local;
  //  LOG_INF("Searching metric in trends");
    if (NULL != (trend = zbx_hashset_search(&trends, &metric->itemid)))
        return trend;

    bzero(&trend_local, sizeof(trend_t));

    trend_local.itemid = metric->itemid;
    trend_local.value_type = value_type;
    trend_local.account_hour = now_hour;
    
//    LOG_INF("Inserting new trend %ld, total trends: %ld",trend_local.itemid, trends.num_data);
    trend = zbx_hashset_insert(&trends, &trend_local, sizeof(trend_t));
        
    return trend;
}

static void reset_trend(trend_t * trend, metric_t *metric, int now_hour, unsigned int value_type) {
     
    trend->value_type = value_type;

    switch (trend->value_type) {

    case ITEM_VALUE_TYPE_FLOAT: 
        trend->value_avg.dbl = 0.0;
        trend->value_min.dbl = 0.0;
        trend->value_max.dbl = 0.0;
        break;

    case ITEM_VALUE_TYPE_UINT64:
        trend->value_avg.ui64 = 0;
        trend->value_max.ui64 = 0;
        trend->value_min.ui64 = 0;
        break;
    }
    
    trend->num = 0;
    trend->account_hour = now_hour;
}

static void export_trend(trend_t *trend, metric_processing_data_t *proc_data) {
    
    if (trend->num == 0)
        return;

    switch (proc_data->value_type) {
        case ITEM_VALUE_TYPE_UINT64:
            trend->value_avg.ui64 = trend->value_avg.ui64 / trend->num;
        break;
        case ITEM_VALUE_TYPE_FLOAT:
            trend->value_avg.dbl = trend->value_avg.ui64 / trend->num;
        break;
    }

    glb_history_add_trend(trend, proc_data);
}

static void cleanup_old_trends(int now_hour) {
    zbx_hashset_iter_t iter;
    metric_processing_data_t proc_data;
    trend_t *trend;
    static int last_cleanup_hour = 0;

    if (last_cleanup_hour == now_hour ||
        time(NULL) - now_hour < 55 * SEC_PER_HOUR ) 
        return; //only do cleanup in the last 5 minutes of the hour 
    
    last_cleanup_hour = now_hour;

    zbx_hashset_iter_reset(&trends, &iter);
    
    while ( NULL !=(trend = zbx_hashset_iter_next(&iter))) {
        if (trend->account_hour != now_hour ) {
            
            fetch_metric_processing_data(trend->itemid, &proc_data);
            export_trend(trend,&proc_data);
            
            zbx_hashset_iter_remove(&iter);
        }
    }
}

static void account_metric(trend_t *trend, metric_t *metric) {
	switch (trend->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			if (trend->num == 0 || metric->value.data.dbl < trend->value_min.dbl)
				trend->value_min.dbl = metric->value.data.dbl;
			if (trend->num == 0 || metric->value.data.dbl > trend->value_max.dbl)
				trend->value_max.dbl = metric->value.data.dbl;
			trend->value_avg.dbl += metric->value.data.dbl;
			break;
		case ITEM_VALUE_TYPE_UINT64:
			if (trend->num == 0 || metric->value.data.ui64 < trend->value_min.ui64)
				trend->value_min.ui64 = metric->value.data.ui64;
			if (trend->num == 0 || metric->value.data.ui64 > trend->value_max.ui64)
				trend->value_max.ui64 = metric->value.data.ui64;
            trend->value_avg.ui64 += metric->value.data.ui64;
			break;
	}
	trend->num++;
}

int trends_account_metric(metric_t *metric , metric_processing_data_t *proc_data) {
    trend_t *trend;
    int now = time(NULL);
    int now_hour = now - now % 3600;

    trend = get_trend(metric, now_hour, proc_data->value_type);
    if (ITEM_VALUE_TYPE_UINT64 != proc_data->value_type && 
        ITEM_VALUE_TYPE_FLOAT != proc_data->value_type) 
            return SUCCEED;
    
    if (trend->account_hour != now_hour ||
        trend->value_type != proc_data->value_type) 
    {   
        LOG_INF("Exporting trend item %ld, trend value type is %d, proc_value type is %d, trend accout hour is %d, now hour is %d", 
        trend->itemid, trend->value_type, (int) proc_data->value_type,  trend->account_hour, now_hour);
        export_trend(trend, proc_data);
        reset_trend(trend, metric, now_hour, proc_data->value_type);
    }
    account_metric(trend, metric);
    cleanup_old_trends(now_hour);
};

int trends_init_cache() {
    zbx_hashset_create(&trends, 1000, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
};

int trends_destroy_cache() {
    zbx_hashset_destroy(&trends);
};