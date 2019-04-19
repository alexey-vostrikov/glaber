<?php
/*
** Zabbix
** Copyright (C) 2001-2018 Zabbix SIA
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


/**
 * Class to perform low level history related actions.
 */
class CHistoryManager {

	/**
	 * Returns the last $limit history objects for the given items.
	 *
	 * @param array $items     an array of items with the 'itemid' and 'value_type' properties
	 * @param int   $limit     max object count to be returned
	 * @param int   $period    the maximum period to retrieve data for
	 *
	 * @return array    an array with items IDs as keys and arrays of history objects as values
	 */
	public function getLastValues(array $items, $limit = 1, $period = null) {

		$results = [];
		$grouped_items = self::getItemsGroupedByStorage($items);

		if (array_key_exists(ZBX_HISTORY_SOURCE_CLICKHOUSE, $grouped_items)) {
			$results += $this->getLastValuesFromClickHouse($grouped_items[ZBX_HISTORY_SOURCE_CLICKHOUSE], $limit,
					$period
			);
		} else if (array_key_exists(ZBX_HISTORY_SOURCE_ELASTIC, $grouped_items)) {
			$results += $this->getLastValuesFromElasticsearch($grouped_items[ZBX_HISTORY_SOURCE_ELASTIC], $limit,
					$period
			);
		}

		if (array_key_exists(ZBX_HISTORY_SOURCE_SQL, $grouped_items)) {
			$results += $this->getLastValuesFromSql($grouped_items[ZBX_HISTORY_SOURCE_SQL], $limit, $period);
		}

		return $results;
	}

	/**
	 * Elasticsearch specific implementation of getLastValues.
	 *
	 * @see CHistoryManager::getLastValues
	 */

	/**
	 * Clickhouse implementation of getLastValues.
	 *
	 */


private function getLastValuesFromClickhouse($items, $limit, $period) {

	global $HISTORY;
	$results = [];
	$itemslist='';

	
	foreach ($items as $item) {
	    if (strlen($itemslist)>0) {
	        $itemslist.=','.$item['itemid'];
    	    } else {
	        $itemslist.=$item['itemid'];
		}
	} 


	$query_text='
		SELECT 
		    itemid, 
		    max(toInt32(clock)) AS clk, 
		    argMax(ns, toInt32(clock)) AS ns_, 
		    argMax(value, toInt32(clock)) AS val, 
		    argMax(value_dbl, toInt32(clock)) AS val_dbl, 
		    argMax(value_str, toInt32(clock)) AS val_str' .
	' FROM '.$HISTORY['tablename'].' h'.
	' WHERE h.itemid in ( '.$itemslist.')'.
	($period ? ' AND h.clock>'.(time() - $period) : '').
	' GROUP BY itemid';

        $values = CClickHouseHelper::query($query_text,1,array('itemid','clock','ns','value','value_dbl','value_str'));

	foreach ($values as $res) 
	{
		$itemid=$res['itemid'];

		//i know this is shit code, but it works much better then 
		//checking for item type for some reason (see it celow)
		$res['value']=floatval($res['value_dbl'])+intval($res['value']);		
		if (strlen($res['value_str']>0)) $res['value']=$res['value_str'];

		if (empty($results[$itemid])) 
		    {
			$results[$itemid]=[$res];
		    }
	}

//	foreach ($items as $item) {
//	    if ( !empty($results[$item['itemid']])) {
//	    	if ($item['value_type'] ==  ITEM_VALUE_TYPE_FLOAT && !empty($results[$item['itemid']]['value_dbl'])) {
//		    $results[$item['itemid']]['value']=$results[$item['itemid']]['value_dbl'];
//	        }
//		if ($item['value_type'] ==  ITEM_VALUE_TYPE_STR) {
//		    $results[$item['itemid']]['value']=$results[$item['itemid']]['value_str'];
//		}
//	    }
//	    
//	}

	return $results;

//	foreach ($items as $item) {
//	    if ($item['value_type'] ==  ITEM_VALUE_TYPE_FLOAT) {
//    		if (strlen($float_itemslist)>0) {
//		        $float_itemslist.=','.$item['itemid'];
//    		} else {
//		    $float_itemslist.=$item['itemid'];
//		}
//	    } else if ($item['value_type'] ==  ITEM_VALUE_TYPE_UINT64) {
//		if (strlen($uint_itemslist)>0) {
//		        $uint_itemslist.=','.$item['itemid'];
//    		} else {
//		    $uint_itemslist.=$item['itemid'];
//		}
//	    } else {
//		if (strlen($str_itemslist)>0) {
//		        $str_itemslist.=','.$item['itemid'];
//    		} else {
//		    $str_itemslist.=$item['itemid'];
//		}
//	    }
//	}

//	var_dump($itemslist);
	
//	$query_text='SELECT itemid, toInt32(clock) as clk,ns,value,value_dbl,value_str as value'.
//	' FROM '.$HISTORY['tablename'].' h'.
//	' WHERE h.itemid in ( '.$itemslist.')'.
//	($period ? ' AND h.clock>'.(time() - $period) : '').
//	' ORDER BY clk DESC';


//	$query_text.=' UNION ALL SELECT itemid, toInt32(clock) as clk,ns,value as value'.
//	' FROM '.$HISTORY['tablename'].' h'.
//	' WHERE h.itemid in ( '.$uint_itemslist.')'.
//	($period ? ' AND h.clock>'.(time() - $period) : '').
//	' ORDER BY clk DESC';
//
//	$query_text.=' UNION ALL SELECT itemid, toInt32(clock) as clk,ns,value_str as value'.
//	' FROM '.$HISTORY['tablename'].' h'.
//	' WHERE h.itemid in ( '.$str_itemslist.')'.
//	($period ? ' AND h.clock>'.(time() - $period) : '').
//	' ORDER BY clk DESC';

//	var_dump($query_text);
    
//        $values = CClickHouseHelper::query($query_text,1,array('itemid','clock','ns','value','value_dbl','value_str'));

//var_dump($values);

/*
	foreach ($items as $item) {
	    if ($item['value_type'] ==  ITEM_VALUE_TYPE_FLOAT) {
		$query_text=	'SELECT itemid, toInt32(clock),ns,value_dbl'.
		    ' FROM '.$HISTORY['tablename'].' h'.
		    ' WHERE h.itemid= '.$item['itemid'].
		    ($period ? ' AND h.clock>'.(time() - $period) : '').
		    ' ORDER BY h.clock DESC';
	    }

	    if ($item['value_type'] ==  ITEM_VALUE_TYPE_UINT64) {
		$query_text=	'SELECT itemid, toInt32(clock) as clock,ns,value'.
		    ' FROM '.$HISTORY['tablename'].' h'.
		    ' WHERE h.itemid= '.$item['itemid']. 
		    ($period ? ' AND h.clock>'.(time() - $period) : '').
		    ' ORDER BY h.clock DESC';
	    }

	    if ($item['value_type'] ==  ITEM_VALUE_TYPE_STR || $item['value_type'] ==  ITEM_VALUE_TYPE_TEXT ) {
		$query_text=	'SELECT itemid, toInt32(clock) as clock,ns,value_str'.
		    ' FROM '.$HISTORY['tablename'].' h'.
		    ' WHERE h.itemid= '.$item['itemid']. 
		    ($period ? ' AND h.clock>'.(time() - $period) : '').
		    ' ORDER BY h.clock DESC';
	    }

	    if ($limit > 0) $query_text.=" LIMIT $limit";
	    
	    $values = CClickHouseHelper::query($query_text,1,array('itemid','clock','ns','value'));

//	    var_dump($values);
	    if ($values) {
		$results[$item['itemid']] = $values;
	    } else {
//			    error("Got empty array, ommiting the result");
	    }
	}

	return $results;
*/
    }

	private function getLastValuesFromElasticsearch($items, $limit, $period) {
		$terms = [];
		$results = [];
		$filter = [];

		foreach ($items as $item) {
			$terms[$item['value_type']][] = $item['itemid'];
		}

		$query = [
			'aggs' => [
				'group_by_itemid' => [
					'terms' => [
						'field' => 'itemid'
					],
					'aggs' => [
						'group_by_docs' => [
							'top_hits' => [
								'size' => $limit,
								'sort' => [
									'clock' => ZBX_SORT_DOWN
								]
							]
						]
					]
				]
			],
			'size' => 0
		];

		if ($period) {
			$filter[] = [
				'range' => [
					'clock' => [
						'gt' => (time() - $period)
					]
				]
			];
		}

		foreach (self::getElasticsearchEndpoints(array_keys($terms)) as $type => $endpoint) {
			$query['query']['bool']['must'] = array_merge([[
				'terms' => [
					'itemid' => $terms[$type]
				]
			]], $filter);
			// Assure that aggregations for all terms are returned.
			$query['aggs']['group_by_itemid']['terms']['size'] = count($terms[$type]);
			$data = CElasticsearchHelper::query('POST', $endpoint, $query);

			if (!is_array($data) || !array_key_exists('group_by_itemid', $data)
					|| !array_key_exists('buckets', $data['group_by_itemid'])
					|| !is_array($data['group_by_itemid']['buckets'])) {
				continue;
			}

			foreach ($data['group_by_itemid']['buckets'] as $item) {
				if (!is_array($item['group_by_docs']) || !array_key_exists('hits', $item['group_by_docs'])
						|| !is_array($item['group_by_docs']['hits'])
						|| !array_key_exists('hits', $item['group_by_docs']['hits'])
						|| !is_array($item['group_by_docs']['hits']['hits'])) {
					continue;
				}

				foreach ($item['group_by_docs']['hits']['hits'] as $row) {
					if (!array_key_exists('_source', $row) || !is_array($row['_source'])) {
						continue;
					}

					$results[$item['key']][] = $row['_source'];
				}
			}
		}

		return $results;
	}

	/**
	 * SQL specific implementation of getLastValues.
	 *
	 * @see CHistoryManager::getLastValues
	 */

	private function getLastValuesFromSql($items, $limit, $period) {
		$results = [];

		foreach ($items as $item) {
			$values = DBfetchArray(DBselect(
				'SELECT *'.
				' FROM '.self::getTableName($item['value_type']).' h'.
				' WHERE h.itemid='.zbx_dbstr($item['itemid']).
					($period ? ' AND h.clock>'.(time() - $period) : '').
				' ORDER BY h.clock DESC',
				$limit
			));

			if ($values) {
				$results[$item['itemid']] = $values;
			}
		}

		return $results;
	}

	/**
	 * Returns history value aggregation for graphs.
	 *
	 * The $item parameter must have the value_type, itemid and source properties set.
	 *
	 * @param array  $items        items to get aggregated values for
	 * @param int    $time_from    minimal timestamp (seconds) to get data from
	 * @param int    $time_to      maximum timestamp (seconds) to get data from
	 * @param int    $width        graph width in pixels (is not required for pie charts)
	 *
	 * @return array    history value aggregation for graphs
	 */
	public function getGraphAggregation(array $items, $time_from, $time_to, $width = null) {
//		error("Hello wold");
		if ($width !== null) {
			$size = $time_to - $time_from;
			$delta = $size - $time_from % $size;
		}
		else {
			$size = null;
			$delta = null;
		}

		$grouped_items = self::getItemsGroupedByStorage($items);

		$results = [];


		if (array_key_exists(ZBX_HISTORY_SOURCE_CLICKHOUSE, $grouped_items)) {
			$results += $this->getGraphAggregationFromClickhouse($grouped_items[ZBX_HISTORY_SOURCE_CLICKHOUSE],
					$time_from, $time_to, $width, $size, $delta
			);
		} else if (array_key_exists(ZBX_HISTORY_SOURCE_ELASTIC, $grouped_items)) {
			$results += $this->getGraphAggregationFromElasticsearch($grouped_items[ZBX_HISTORY_SOURCE_ELASTIC],
					$time_from, $time_to, $width, $size, $delta
			);
		}

		if (array_key_exists(ZBX_HISTORY_SOURCE_SQL, $grouped_items)) {
			$results += $this->getGraphAggregationFromSql($grouped_items[ZBX_HISTORY_SOURCE_SQL], $time_from, $time_to,
					$width, $size, $delta
			);
		}

		return $results;
	}

	/**
	 * Elasticsearch specific implementation of getGraphAggregation.
	 *
	 * @see CHistoryManager::getGraphAggregation
	 */
	private function getGraphAggregationFromClickhouse(array $items, $time_from, $time_to, $width, $size, $delta) {

		global $HISTORY;
		$group_by = 'itemid';
		$sql_select_extra = '';

		if ($width !== null && $size !== null && $delta !== null) {
			// Required for 'group by' support of Oracle.
			$calc_field = 'round('.$width.'*'.'modulo(toUInt32(clock)'.'+'.$delta.",$size)".'/('.$size.'),0)';

			$sql_select_extra = ','.$calc_field.' AS i';
			$group_by .= ','.$calc_field;
		}

		$results = [];

		foreach ($items as $item) {
			if ($item['value_type'] == ITEM_VALUE_TYPE_UINT64) {
				$sql_select = 'COUNT(*) AS count,AVG(value) AS avg,MIN(value) AS min,MAX(value) AS max';
			} else
			{
				$sql_select = 'COUNT(*) AS count,AVG(value_dbl) AS avg,MIN(value_dbl) AS min,MAX(value_dbl) AS max';
			}
			$daystart=date('Y-m-d',$time_from);
			$dayend=date('Y-m-d',$time_to);
			
			$query_text = 
				'SELECT itemid,'.$sql_select.$sql_select_extra.',MAX(toUInt32(clock)) AS clock1'.
				' FROM '. $HISTORY['tablename'] .
				' WHERE itemid='.$item['itemid'].
					' AND day >= \''.$daystart. '\''.
					' AND day <= \''. $dayend. '\''.	 
					' AND toUInt32(clock)>='.$time_from.
					' AND toUInt32(clock)<='.$time_to.
				' GROUP BY '.$group_by ;

//			file_put_contents('/var/log/nginx/chartlog.log', "Will do query '$new_query_text' \n",FILE_APPEND);


			$values = CClickHouseHelper::query($query_text,1,array('itemid','count','avg','min','max','i','clock'));

			$results[$item['itemid']]['source'] = 'history';
			$results[$item['itemid']]['data'] = $values;
		}

//	    ob_start();
//	    var_dump($results);
//	    $dresult = ob_get_clean();
//	    error("Dump of the result is '$dresult'");

//	    file_put_contents('/var/log/nginx/chartlog.log', "Clickhouse Results structure is $dresult' \n",FILE_APPEND);

		return $results;

	}

	private function getGraphAggregationFromElasticsearch(array $items, $time_from, $time_to, $width, $size, $delta) {
		$terms = [];

		foreach ($items as $item) {
			$terms[$item['value_type']][] = $item['itemid'];
		}

		$aggs = [
			'max_value' => [
				'max' => [
					'field' => 'value'
				]
			],
			'avg_value' => [
				'avg' => [
					'field' => 'value'
				]
			],
			'min_value' => [
				'min' => [
					'field' => 'value'
				]
			],
			'max_clock' => [
				'max' => [
					'field' => 'clock'
				]
			]
		];

		$query = [
			'aggs' => [
				'group_by_itemid' => [
					'terms' => [
						// Assure that aggregations for all terms are returned.
						'size' => count($items),
						'field' => 'itemid'
					]
				]
			],
			'query' => [
				'bool' => [
					'must' => [
						[
							'terms' => [
								'itemid' => $terms
							]
						],
						[
							'range' => [
								'clock' => [
									'gte' => $time_from,
									'lte' => $time_to
								]
							]
						]
					]
				]
			],
			'size' => 0
		];

		if ($width !== null && $size !== null && $delta !== null) {
			// Additional grouping for line graphs.
			$aggs['max_clock'] = [
				'max' => [
					'field' => 'clock'
				]
			];

			// Clock value is divided by 1000 as it is stored as milliseconds.
			$formula = 'Math.floor((params.width*((doc[\'clock\'].date.getMillis()/1000+params.delta)%params.size))'.
					'/params.size)';

			$script = [
				'inline' => $formula,
				'params' => [
					'width' => (int)$width,
					'delta' => $delta,
					'size' => $size
				]
			];
			$aggs = [
				'group_by_script' => [
					'terms' => [
						'size' => $width,
						'script' => $script
					],
					'aggs' => $aggs
				]
			];
		}

		$query['aggs']['group_by_itemid']['aggs'] = $aggs;

		$results = [];

		foreach (self::getElasticsearchEndpoints(array_keys($terms)) as $type => $endpoint) {
			$query['query']['bool']['must'] = [
				[
					'terms' => [
						'itemid' => $terms[$type]
					]
				],
				[
					'range' => [
						'clock' => [
							'gte' => $time_from,
							'lte' => $time_to
						]
					]
				]
			];

			$data = CElasticsearchHelper::query('POST', $endpoint, $query);

			if ($width !== null && $size !== null && $delta !== null) {
				foreach ($data['group_by_itemid']['buckets'] as $item) {
					if (!is_array($item['group_by_script']) || !array_key_exists('buckets', $item['group_by_script'])
							|| !is_array($item['group_by_script']['buckets'])) {
						continue;
					}

					$results[$item['key']]['source'] = 'history';
					foreach ($item['group_by_script']['buckets'] as $point) {
						$results[$item['key']]['data'][] = [
							'itemid' => $item['key'],
							'i' => $point['key'],
							'count' => $point['doc_count'],
							'min' => $point['min_value']['value'],
							'avg' => $point['avg_value']['value'],
							'max' => $point['max_value']['value'],
							// Field value_as_string is used to get value as seconds instead of milliseconds.
							'clock' => $point['max_clock']['value_as_string']
						];
					}
				}
			}
			else {
				foreach ($data['group_by_itemid']['buckets'] as $item) {
					$results[$item['key']]['source'] = 'history';
					$results[$item['key']]['data'][] = [
						'itemid' => $item['key'],
						'min' => $item['min_value']['value'],
						'avg' => $item['avg_value']['value'],
						'max' => $item['max_value']['value'],
						// Field value_as_string is used to get value as seconds instead of milliseconds.
						'clock' => $item['max_clock']['value_as_string']
					];
				}
			}
		}

		return $results;
	}

	/**
	 * SQL specific implementation of getGraphAggregation.
	 *
	 * @see CHistoryManager::getGraphAggregation
	 */
	private function getGraphAggregationFromSql(array $items, $time_from, $time_to, $width, $size, $delta) {
		$group_by = 'itemid';
		$sql_select_extra = '';

		if ($width !== null && $size !== null && $delta !== null) {
			// Required for 'group by' support of Oracle.
			$calc_field = 'round('.$width.'*'.zbx_sql_mod(zbx_dbcast_2bigint('clock').'+'.$delta, $size)
					.'/('.$size.'),0)';

			$sql_select_extra = ','.$calc_field.' AS i';
			$group_by .= ','.$calc_field;
		}

		$results = [];

		foreach ($items as $item) {
			if ($item['source'] === 'history') {
				$sql_select = 'COUNT(*) AS count,AVG(value) AS avg,MIN(value) AS min,MAX(value) AS max';
				$sql_from = ($item['value_type'] == ITEM_VALUE_TYPE_UINT64) ? 'history_uint' : 'history';
			}
			else {
				$sql_select = 'SUM(num) AS count,AVG(value_avg) AS avg,MIN(value_min) AS min,MAX(value_max) AS max';
				$sql_from = ($item['value_type'] == ITEM_VALUE_TYPE_UINT64) ? 'trends_uint' : 'trends';
			}

			$result = DBselect(
				'SELECT itemid,'.$sql_select.$sql_select_extra.',MAX(clock) AS clock'.
				' FROM '.$sql_from.
				' WHERE itemid='.zbx_dbstr($item['itemid']).
					' AND clock>='.zbx_dbstr($time_from).
					' AND clock<='.zbx_dbstr($time_to).
				' GROUP BY '.$group_by
			);

			$data = [];
			while (($row = DBfetch($result)) !== false) {
				$data[] = $row;
			}

			$results[$item['itemid']]['source'] = $item['source'];
			$results[$item['itemid']]['data'] = $data;
		}

		return $results;
	}

	/**
	 * Returns aggregated history value.
	 *
	 * The $item parameter must have the value_type and itemid properties set.
	 *
	 * @param array  $item         item to get aggregated value for
	 * @param string $aggregation  aggregation to be applied (min / max / avg)
	 * @param int    $time_from    timestamp (seconds)
	 *
	 * @return string    aggregated history value
	 */
	public function getAggregatedValue(array $item, $aggregation, $time_from) {
		switch (self::getDataSourceType($item['value_type'])) {
			case ZBX_HISTORY_SOURCE_CLICKHOUSE:
				return $this->getAggregatedValueFromClickhouse($item, $aggregation, $time_from);
			case ZBX_HISTORY_SOURCE_ELASTIC:
				return $this->getAggregatedValueFromElasticsearch($item, $aggregation, $time_from);

			default:
				return $this->getAggregatedValueFromSql($item, $aggregation, $time_from);
		}
	}

	private function getAggregatedValueFromClickhouse(array $item, $aggregation, $time_from) {

		global $HISTORY;
		$query_text =
			'SELECT '.$aggregation.'(value) AS value'.
			' FROM '. $HISTORY['tablename'].
			' WHERE clock>toDateTime('.$time_from.')'.
			' AND itemid='.$item['itemid'].
			' HAVING COUNT(*)>0';
		

		$value = CClickHouseHelper::query($query_text,0,array());

		return $value;

	}


	/**
	 * Elasticsearch specific implementation of getAggregatedValue.
	 *
	 * @see CHistoryManager::getAggregatedValue
	 */
	private function getAggregatedValueFromElasticsearch(array $item, $aggregation, $time_from) {
		$query = [
			'aggs' => [
				$aggregation.'_value' => [
					$aggregation => [
						'field' => 'value'
					]
				]
			],
			'query' => [
				'bool' => [
					'must' => [
						[
							'term' => [
								'itemid' => $item['itemid']
							]
						],
						[
							'range' => [
								'clock' => [
									'gte' => $time_from
								]
							]
						]
					]
				]
			],
			'size' => 0
		];

		$endpoints = self::getElasticsearchEndpoints($item['value_type']);

		if ($endpoints) {
			$data = CElasticsearchHelper::query('POST', reset($endpoints), $query);

			if (array_key_exists($aggregation.'_value', $data)
					&& array_key_exists('value', $data[$aggregation.'_value'])) {
				return $data[$aggregation.'_value']['value'];
			}
		}

		return null;
	}

	/**
	 * SQL specific implementation of getAggregatedValue.
	 *
	 * @see CHistoryManager::getAggregatedValue
	 */
	private function getAggregatedValueFromSql(array $item, $aggregation, $time_from) {
		$result = DBselect(
			'SELECT '.$aggregation.'(value) AS value'.
			' FROM '.self::getTableName($item['value_type']).
			' WHERE clock>'.$time_from.
			' AND itemid='.zbx_dbstr($item['itemid']).
			' HAVING COUNT(*)>0' // Necessary because DBselect() return 0 if empty data set, for graph templates.
		);

		if (($row = DBfetch($result)) !== false) {
			return $row['value'];
		}

		return null;
	}

	/**
	 * Clear item history and trends by provided item IDs. History is deleted from both SQL and Elasticsearch.
	 *
	 * @param array $itemids    item ids to delete history for
	 *
	 * @return bool
	 */
	public function deleteHistory(array $itemids) {
		return $this->deleteHistoryFromSql($itemids) && $this->deleteHistoryFromElasticsearch($itemids);
	}

	/**
	 * Elasticsearch specific implementation of deleteHistory.
	 *
	 * @see CHistoryManager::deleteHistory
	 */
	private function deleteHistoryFromElasticsearch(array $itemids) {
		global $HISTORY;

		if (is_array($HISTORY) && array_key_exists('types', $HISTORY) && is_array($HISTORY['types'])
				&& count($HISTORY['types'] > 0)) {

			$query = [
				'query' => [
					'terms' => [
						'itemid' => array_values($itemids)
					]
				]
			];

			$types = [];
			foreach ($HISTORY['types'] as $type) {
				$types[] = self::getTypeIdByTypeName($type);
			}

			foreach (self::getElasticsearchEndpoints($types, '_delete_by_query') as $endpoint) {
				if (!CElasticsearchHelper::query('POST', $endpoint, $query)) {
					return false;
				}
			}
		}

		return true;
	}

	/**
	 * SQL specific implementation of deleteHistory.
	 *
	 * @see CHistoryManager::deleteHistory
	 */
	private function deleteHistoryFromSql(array $itemids) {
		return DBexecute('DELETE FROM trends WHERE '.dbConditionInt('itemid', $itemids))
				&& DBexecute('DELETE FROM trends_uint WHERE '.dbConditionInt('itemid', $itemids))
				&& DBexecute('DELETE FROM history_text WHERE '.dbConditionInt('itemid', $itemids))
				&& DBexecute('DELETE FROM history_log WHERE '.dbConditionInt('itemid', $itemids))
				&& DBexecute('DELETE FROM history_uint WHERE '.dbConditionInt('itemid', $itemids))
				&& DBexecute('DELETE FROM history_str WHERE '.dbConditionInt('itemid', $itemids))
				&& DBexecute('DELETE FROM history WHERE '.dbConditionInt('itemid', $itemids));
	}

	/**
	 * Get type name by value type id.
	 *
	 * @param int $value_type    value type id
	 *
	 * @return string    value type name
	 */
	public static function getTypeNameByTypeId($value_type) {
		$mapping = [
			ITEM_VALUE_TYPE_FLOAT => 'dbl',
			ITEM_VALUE_TYPE_STR => 'str',
			ITEM_VALUE_TYPE_LOG => 'log',
			ITEM_VALUE_TYPE_UINT64 => 'uint',
			ITEM_VALUE_TYPE_TEXT => 'text'
		];

		if (array_key_exists($value_type, $mapping)) {
			return $mapping[$value_type];
		}

		// Fallback to float.
		return $mapping[ITEM_VALUE_TYPE_FLOAT];
	}

	/**
	 * Get type id by value type name.
	 *
	 * @param int $type_name    value type name
	 *
	 * @return int    value type id
	 */
	public static function getTypeIdByTypeName($type_name) {
		$mapping = [
			'dbl' => ITEM_VALUE_TYPE_FLOAT,
			'str' => ITEM_VALUE_TYPE_STR,
			'log' => ITEM_VALUE_TYPE_LOG,
			'uint' => ITEM_VALUE_TYPE_UINT64,
			'text' => ITEM_VALUE_TYPE_TEXT
		];

		if (array_key_exists($type_name, $mapping)) {
			return $mapping[$type_name];
		}

		// Fallback to float.
		return ITEM_VALUE_TYPE_FLOAT;
	}

	/**
	 * Get data source (SQL or Elasticsearch) type based on value type id.
	 *
	 * @param int $value_type    value type id
	 *
	 * @return string    data source type
	 */
	public static function getDataSourceType($value_type) {
		static $cache = [];

		if (!array_key_exists($value_type, $cache)) {
			global $HISTORY;

			if (is_array($HISTORY) && array_key_exists('types', $HISTORY) && is_array($HISTORY['types'])) {
					if ($HISTORY['storagetype']=='clickhouse') 
							$cache[$value_type] = in_array(self::getTypeNameByTypeId($value_type), $HISTORY['types'])
								? ZBX_HISTORY_SOURCE_CLICKHOUSE : ZBX_HISTORY_SOURCE_SQL;
					else 
							$cache[$value_type] = in_array(self::getTypeNameByTypeId($value_type), $HISTORY['types'])
								? ZBX_HISTORY_SOURCE_ELASTIC : ZBX_HISTORY_SOURCE_SQL;
			}
			else {
				// SQL is a fallback data source.
				$cache[$value_type] = ZBX_HISTORY_SOURCE_SQL;
			}
		}

		return $cache[$value_type];
	}

	private static function getElasticsearchUrl($value_name) {
		static $urls = [];
		static $invalid = [];

		// Additional check to limit error count produced by invalid configuration.
		if (array_key_exists($value_name, $invalid)) {
			return null;
		}

		if (!array_key_exists($value_name, $urls)) {
			global $HISTORY;

			if (!is_array($HISTORY) || !array_key_exists('url', $HISTORY)) {
				$invalid[$value_name] = true;
				error(_s('Elasticsearch url is not set for type: %1$s.', $value_name));

				return null;
			}

			$url = $HISTORY['url'];
			if (is_array($url)) {
				if (!array_key_exists($value_name, $url)) {
					$invalid[$value_name] = true;
					error(_s('Elasticsearch url is not set for type: %1$s.', $value_name));

					return null;
				}

				$url = $url[$value_name];
			}

			if (substr($url, -1) !== '/') {
				$url .= '/';
			}

			$urls[$value_name] = $url;
		}

		return $urls[$value_name];
	}

	/**
	 * Get endpoints for Elasticsearch requests.
	 *
	 * @param mixed $value_types    value type(s)
	 *
	 * @return array    Elasticsearch query endpoints
	 */
	public static function getElasticsearchEndpoints($value_types, $action = '_search') {
		if (!is_array($value_types)) {
			$value_types = [$value_types];
		}

		$indices = [];
		$endponts = [];

		foreach (array_unique($value_types) as $type) {
			if (self::getDataSourceType($type) === ZBX_HISTORY_SOURCE_ELASTIC) {
				$indices[$type] = self::getTypeNameByTypeId($type);
			}
		}

		foreach ($indices as $type => $index) {
			if (($url = self::getElasticsearchUrl($index)) !== null) {
				$endponts[$type] = $url.$index.'*/values/'.$action;
			}
		}

		return $endponts;
	}

	/**
	 * Return the name of the table where the data for the given value type is stored.
	 *
	 * @param int $value_type    value type
	 *
	 * @return string    table name
	 */
	public static function getTableName($value_type) {
		$tables = [
			ITEM_VALUE_TYPE_LOG => 'history_log',
			ITEM_VALUE_TYPE_TEXT => 'history_text',
			ITEM_VALUE_TYPE_STR => 'history_str',
			ITEM_VALUE_TYPE_FLOAT => 'history',
			ITEM_VALUE_TYPE_UINT64 => 'history_uint'
		];

		return $tables[$value_type];
	}

	/**
	 * Returns the items grouped by the storage type.
	 *
	 * @param array $items     an array of items with the 'value_type' property
	 *
	 * @return array    an array with storage type as a keys and item arrays as a values
	 */
	private function getItemsGroupedByStorage(array $items) {
		$grouped_items = [];

		foreach ($items as $item) {
			$source = self::getDataSourceType($item['value_type']);
			$grouped_items[$source][] = $item;
		}

		return $grouped_items;
	}
}
