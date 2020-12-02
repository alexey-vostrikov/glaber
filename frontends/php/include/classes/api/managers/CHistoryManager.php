<?php
/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
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

		return $this->getLastValuesFromServer($items, $limit, $period);
	}

	/*
	* server implemetation of lastvalues - uses valuecache as a source for data
	*/
	private function getLastValuesFromServer($items, $limit, $period) {
	  global $ZBX_SERVER, $ZBX_SERVER_PORT;

	  $result=[];
		
	  $server = new CZabbixServer($ZBX_SERVER, $ZBX_SERVER_PORT, ZBX_SOCKET_TIMEOUT, ZBX_SOCKET_BYTES_LIMIT);
	  $last_values = $server->getLastValues(get_cookie(ZBX_SESSION_NAME),array_column($items,'itemid'),$limit, $period); 
	  	 
	  if (!is_array($last_values)) return [];

	  foreach ($last_values as $value) 
	  {
		 $result[$value['itemid']]=$value;
  	  }
	
	   return $result;
	}

	
	/**
	 * Returns the history data of the item at the given time. If no data exists at the given time, the function will
	 * return the previous data.
	 *
	 * The $item parameter must have the value_type and itemid properties set.
	 *
	 * @param array  $item
	 * @param string $item['itemid']
	 * @param int    $item['value_type']
	 * @param int    $clock
	 * @param int    $ns
	 *
	 * @return array|null  Item data at specified time of first data before specified time. null if data is not found.
	 */
	public function getValueAt(array $item, $clock, $ns) {
		return $this->getValueAtFromServer($item, $clock, $ns);
	}

	/**
	 * Returns history value aggregation for graphs.
	 *
	 * The $item parameter must have the value_type, itemid and source properties set.
	 *
	 * @param array  $items      items to get aggregated values for
	 * @param int    $time_from  minimal timestamp (seconds) to get data from
	 * @param int    $time_to    maximum timestamp (seconds) to get data from
	 * @param string $function   function for data aggregation
	 * @param string $interval   aggregation interval in seconds
	 *
	 * @return array    history value aggregation for graphs
	 */
	public function getGraphAggregationByInterval(array $items, $time_from, $time_to, $function, $interval) {
		
		$results = [];
		//error_log("Called GraphAggregationByInterval,  $time_from, $time_to, $function, $interval 	");
		return $this->getGraphAggregationByIntervalFromServer($items,$time_from, $time_to, $function, $interval);
	}

	/** interval function calculation is implemented via server's getHistory and applying the proper function */
	private function getGraphAggregationByIntervalFromServer(array $items, $time_from, $time_to, $function, $interval) {
		$results = [];
		$items_by_table = [];
		//$width=1;
		
		//so convert it to number of points first
		
		$width = (int)(($time_to -$time_from)/$interval);
		//error_log("Calculated width is $width");

		//first, let's get all the data 
		$results = $this->getGraphAggregationByWidthFromServer($items,$time_from, $time_to, $width,"history_agg");
		$results += $this->getGraphAggregationByWidthFromServer($items,$time_from, $time_to, $width,"trends");

		//let's do a bit of calculation and data adoption
		foreach ($results as $itemid => $itemdata ) {
			//error_log("Convering data $itemid");
			foreach ($itemdata['data'] as $value) {
				//error_log(print_r($value,true));
				$row=[];
				$row['itemid'] = $value['itemid'];
				$row['tick'] = (int)($value['clock'] - (int)$value['clock'] % $interval);
				$row['clock'] = $value['clock'];
			
				switch ($function) {
					case GRAPH_AGGREGATE_MIN:
						$row['value'] = $value['min'];
						break;
					case GRAPH_AGGREGATE_MAX:
						$row['value'] = $value['max'];
						break;
					case GRAPH_AGGREGATE_AVG:
						$row['value'] = $value['avg'];
						break;
					case GRAPH_AGGREGATE_COUNT:
						$row['value'] = $value['count'];
						break;
					case GRAPH_AGGREGATE_SUM:
						$row['value'] = $value['count']*$value['avg'];
						break;
					//oooops, this is something which cannot be done right now
					//TODO figure how to do this or remove first and last from the menu
					case GRAPH_AGGREGATE_FIRST:
						$row['value'] = $value['avg'];
						//$sql_select[] = 'MIN(clock) AS clock';
						break;
					case GRAPH_AGGREGATE_LAST:
						$row['value'] = $value['avg'];
						break;
				}

				$result[$value['itemid']]['source'][] = 'history';
				$result[$value['itemid']]['data'][] = $row;

			}
		}

		return $result;
	}

	/**
	 * SQL specific implementation of getGraphAggregationByWidth.
	 *
	 * @see CHistoryManager::getGraphAggregationByInterval
	 */
	private function getGraphAggregationByIntervalFromSql(array $items, $time_from, $time_to, $function, $interval) {
		$items_by_table = [];
		foreach ($items as $item) {
			$items_by_table[$item['value_type']][$item['source']][] = $item['itemid'];
		}

		$result = [];

		foreach ($items_by_table as $value_type => $items_by_source) {
			foreach ($items_by_source as $source => $itemids) {
				$sql_select = ['itemid'];
				$sql_group_by = ['itemid'];

				$calc_field = zbx_dbcast_2bigint('clock').'-'.zbx_sql_mod(zbx_dbcast_2bigint('clock'), $interval);
				$sql_select[] = $calc_field.' AS tick';
				$sql_group_by[] = $calc_field;

				if ($source === 'history') {
					switch ($function) {
						case GRAPH_AGGREGATE_MIN:
							$sql_select[] = 'MIN(value) AS value, MAX(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_MAX:
							$sql_select[] = 'MAX(value) AS value, MAX(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_AVG:
							$sql_select[] = 'AVG(value) AS value, MAX(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_COUNT:
							$sql_select[] = 'COUNT(*) AS count, MAX(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_SUM:
							$sql_select[] = 'SUM(value) AS value, MAX(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_FIRST:
							$sql_select[] = 'MIN(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_LAST:
							$sql_select[] = 'MAX(clock) AS clock';
							break;
					}
					$sql_from = ($value_type == ITEM_VALUE_TYPE_UINT64) ? 'history_uint' : 'history';
				}
				else {
					switch ($function) {
						case GRAPH_AGGREGATE_MIN:
							$sql_select[] = 'MIN(value_min) AS value, MAX(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_MAX:
							$sql_select[] = 'MAX(value_max) AS value, MAX(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_AVG:
							$sql_select[] = 'AVG(value_avg) AS value, MAX(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_COUNT:
							$sql_select[] = 'SUM(num) AS count, MAX(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_SUM:
							$sql_select[] = '(value_avg * num) AS value, MAX(clock) AS clock';
							$sql_group_by = array_merge($sql_group_by, ['value_avg', 'num']);
							break;
						case GRAPH_AGGREGATE_FIRST:
							$sql_select[] = 'MIN(clock) AS clock';
							break;
						case GRAPH_AGGREGATE_LAST:
							$sql_select[] = 'MAX(clock) AS clock';
							break;
					}
					$sql_from = ($value_type == ITEM_VALUE_TYPE_UINT64) ? 'trends_uint' : 'trends';
				}

				$sql = 'SELECT '.implode(', ', $sql_select).
					' FROM '.$sql_from.
					' WHERE '.dbConditionInt('itemid', $itemids).
					' AND clock >= '.zbx_dbstr($time_from).
					' AND clock <= '.zbx_dbstr($time_to).
					' GROUP BY '.implode(', ', $sql_group_by);

				if ($function == GRAPH_AGGREGATE_FIRST || $function == GRAPH_AGGREGATE_LAST) {
					$sql = 'SELECT DISTINCT h.itemid, h.'.($source === 'history' ? 'value' : 'value_avg').' AS value, h.clock, hi.tick'.
						' FROM '.$sql_from.' h'.
						' JOIN('.$sql.') hi ON h.itemid = hi.itemid AND h.clock = hi.clock';
				}

				$sql_result = DBselect($sql);

				while (($row = DBfetch($sql_result)) !== false) {
					$result[$row['itemid']]['source'] = $source;
					$result[$row['itemid']]['data'][] = $row;
				}
			}
		}

		return $result;
	}

	/**
	 * Returns history value aggregation for graphs.
	 *
	 * The $item parameter must have the value_type, itemid and source properties set.
	 *
	 * @param array $items      items to get aggregated values for
	 * @param int   $time_from  minimal timestamp (seconds) to get data from
	 * @param int   $time_to    maximum timestamp (seconds) to get data from
	 * @param int   $width      graph width in pixels (is not required for pie charts)
	 *
	 * @return array    history value aggregation for graphs
	 */
	public function getGraphAggregationByWidth(array $items, $time_from, $time_to, $width = null) {
		$results = [];
		$agg_results = [];
		$trend_results = [];
		
		//combine history agregation and trends data
		$agg_results += $this->getGraphAggregationByWidthFromServer($items,$time_from, $time_to, $width,"history_agg");
		$trend_results += $this->getGraphAggregationByWidthFromServer($items,$time_from, $time_to, $width,"trends");
		
		foreach ($items as $item) {
		
			$results[$item['itemid']]['data'] = [];
			
			if (isset($trend_results[$item['itemid']]['data'])) 
				$results[$item['itemid']]['data'] += $trend_results[$item['itemid']]['data'];
	
			if (isset($agg_results[$item['itemid']]['data'])) 
				$results[$item['itemid']]['data'] += $agg_results[$item['itemid']]['data'];
		}
	//	error_log(print_r($results,true));
		return $results;
	}
	
	private function getGraphAggregationByWidthFromServer(array $items, $time_from, $time_to, $aggregates, $source) {
		global $ZBX_SERVER, $ZBX_SERVER_PORT;
			
		foreach ($items as $item) {
			//for some strange reason same object dosn't do request for the same time, so init once per itemid here
			$server = new CZabbixServer($ZBX_SERVER, $ZBX_SERVER_PORT, ZBX_SOCKET_TIMEOUT, ZBX_SOCKET_BYTES_LIMIT);  
			$results[$item['itemid']]['data'] = $server->getHistoryData(get_cookie(ZBX_SESSION_NAME), $item['itemid'], $time_from, $time_to, $aggregates, $source); 
			
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
	//TODO implement this based on server's request
	 public function getAggregatedValue(array $item, $aggregation, $time_from) {
		switch (self::getDataSourceType($item['value_type'])) {
			case ZBX_HISTORY_SOURCE_CLICKHOUSE:
				return $this->getAggregatedValueFromClickhouse($item, $aggregation, $time_from);
		
			default:
				return $this->getAggregatedValueFromSql($item, $aggregation, $time_from);
		}
	}

	private function getAggregatedValueFromClickhouse(array $item, $aggregation, $time_from) {

		global $HISTORY;
		$query_text =
			'SELECT '.$aggregation.'(value) AS value'.
			' FROM '. $HISTORY['dbname']. '.history_buffer '.
			' WHERE clock>toDateTime('.$time_from.')'.
			' AND itemid='.$item['itemid'].
			' HAVING COUNT(*)>0';
		

		$value = CClickHouseHelper::query($query_text,0,array());

		return $value;

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
		return ZBX_HISTORY_SOURCE_SERVER;;
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
