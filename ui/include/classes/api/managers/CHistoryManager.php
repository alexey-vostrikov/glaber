<?php
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


/**
 * Class to perform low level history related actions.
 */
class CHistoryManager {

	/**
	 * Returns a subset of $items having history data within the $period of time.
	 *
	 * @param array $items   An array of items with the 'itemid' and 'value_type' properties.
	 * @param int   $period  The maximum period of time to search for history values within.
	 *
	 * @return array  An array with items IDs as keys and the original item data as values.
	 */
	public function getItemsHavingValues(array $items, $period = null) {
		$items = zbx_toHash($items, 'itemid');

		$results = [];
		//$grouped_items = $this->getItemsGroupedByStorage($items);

		$results +=  $this->getLastValuesFromServer($items, 2, $period);
		
		return array_intersect_key($items, $results);
	}

	/**
	 * Returns the last $limit history objects for the given items.
	 *
	 * @param array $items   An array of items with the 'itemid' and 'value_type' properties.
	 * @param int   $limit   Max object count to be returned.
	 * @param int   $period  The maximum period to retrieve data for.
	 *
	 * @return array  An array with items IDs as keys and arrays of history objects as values.
	 */
	public function getLastValues(array $items, $limit = 1, $period = null) {
		//there is two possibilites for getlastvalues
		//most recent 2 values are kept int the server memory, so we requesting them
		//if we need less then 2 values

		return $this->getLastValuesFromServer($items, $limit, $period);
	}
	
	private function getLastValuesFromServer($items, $limit, $period) {
	  global $ZBX_SERVER, $ZBX_SERVER_PORT;
		
	  $result=[];
				
	  $server = new CZabbixServer($ZBX_SERVER, $ZBX_SERVER_PORT,
	  	timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::CONNECT_TIMEOUT)),
	    timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::SOCKET_TIMEOUT)),  ZBX_SOCKET_BYTES_LIMIT);
	  $last_values = $server->getLastValues(CSessionHelper::getId(),array_column($items,'itemid'),$limit, $period); 
	
	  if (!is_array($last_values)) {
			return [];
	  }

	  foreach ($last_values as $value) 
	  {
		
		if (!isset($result[$value['itemid']])) 
			$result[$value['itemid']]=[];
			
		$result[$value['itemid']]  +=  $value['values'];
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
	public function getValueAt(array $items, $clock, $ns) {
		return $this->getGraphAggregationByWidthFromServer($items, $clock, $clock+1, 1, 'history');
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
		foreach ($itemdata['data'] as $value) {
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
					$row['count'] = $value['count'];
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
	 * Returns history value aggregation for graphs.
	 *
	 * The $item parameter must have the value_type, itemid and source properties set.
	 *
	 * @param array  $items      Items to get aggregated values for.
	 * @param int    $time_from  Minimal timestamp (seconds) to get data from.
	 * @param int    $time_to    Maximum timestamp (seconds) to get data from.
	 * @param string $function   Function for data aggregation.
	 * @param string $interval   Aggregation interval in seconds.
	 *
	 * @return array  History value aggregation for graphs.
	 */
	public function getGraphAggregationByInterval(array $items, $time_from, $time_to, $function, $interval) {
		return $this->getGraphAggregationByIntervalFromServer($items,$time_from, $time_to, $function, $interval);
	}
	
	/**
	 * Returns history value aggregation for graphs.
	 *
	 * The $item parameter must have the value_type, itemid and source properties set.
	 *
	 * @param array $items      Items to get aggregated values for.
	 * @param int   $time_from  Minimal timestamp (seconds) to get data from.
	 * @param int   $time_to    Maximum timestamp (seconds) to get data from.
	 * @param int   $width      Graph width in pixels (is not required for pie charts).
	 *
	 * @return array  History value aggregation for graphs.
	 */
	public function getGraphAggregationByWidth(array $items, $time_from, $time_to, $width = null) {
		$results = [];
		$agg_results = [];
		
		$agg_results += $this->getGraphAggregationByWidthFromServer($items,$time_from, $time_to, $width,"history_agg");

		foreach ($items as $item) {
		
			$results[$item['itemid']]['data'] = [];
			$history_start = $time_to;
			//error_log(print_r($agg_results,1));
			//error_log(print_r($trend_results,1));
			//error_log(print_r($results,1));
			
			if (isset($agg_results[$item['itemid']]['data']) && is_array($agg_results[$item['itemid']]['data'])) {
			
				$results[$item['itemid']]['data'] += $agg_results[$item['itemid']]['data'];

				foreach ( $agg_results[$item['itemid']]['data'] as $value) {
					if ( $value['clock'] < $history_start ) 
						$history_start = $value['clock'];
				}
				

				//fix: remove after testing - this test's 
				//$history_start += 3 * 86000;
				//foreach ($results[$item['itemid']]['data'] as $idx => $value) {
				//	if ($value["clock"] < $history_start) {
				//		unset($results[$item['itemid']]['data'][$idx]);
				//	}
				//}
				
			}
						
			if ($history_start - $time_from > 3600 ) {
			
				$trend_results = [];
				$trend_results += $this->getGraphAggregationByWidthFromServer($items,$time_from, $time_to, $width,"trends");		
				
				//now using only points that has timestamps prior to the history start
				if (isset($trend_results[$item['itemid']]['data']) && is_array($trend_results[$item['itemid']]['data'])) 
				{
					foreach($trend_results[$item['itemid']]['data'] as $idx=>$key) {
						if ($key['clock'] < $history_start)
						 array_push($results[$item['itemid']]['data'],$trend_results[$item['itemid']]['data'][$idx]);
					}
				}
			} 
		}
	//	error_log("getGraphAggregationByWidth:". print_r($results,true));
		return $results;
	}
	
	private function getGraphAggregationByWidthFromServer(array $items, $time_from, $time_to, $aggregates, $source) {
		global $ZBX_SERVER, $ZBX_SERVER_PORT;
			
		foreach ($items as $item) {
			//for some strange reason same object dosn't do request for the same time, so init once per itemid here
			$server = new CZabbixServer($ZBX_SERVER, $ZBX_SERVER_PORT, 
					timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::CONNECT_TIMEOUT)),
					timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::SOCKET_TIMEOUT)), ZBX_SOCKET_BYTES_LIMIT);  
			$results[$item['itemid']]['data'] = $server->getHistoryData(CSessionHelper::getId(), $item['itemid'], $time_from, $time_to, $aggregates, $source); 
		
		}
		return $results;
	}

	/**
	 * Returns aggregated history value.
	 *
	 * The $item parameter must have the value_type and itemid properties set.
	 *
	 * @param array  $item         Item to get aggregated value for.
	 * @param string $aggregation  Aggregation to be applied (min / max / avg).
	 * @param int    $time_from    Timestamp (seconds).
	 *
	 * @return string  Aggregated history value.
	 */
	//TODO implement this based on server's request
	 public function getAggregatedValue(array $item, $aggregation, $time_from) {
//		switch (self::getDataSourceType($item['value_type'])) {
			   return $this->getAggregatedValueFromServer($item, $aggregation, $time_from);
	}
	private function getAggregatedValueFromServer(array $item, $aggregation, $time_from) {
		error_log("Requested aggregated value for items $item, agg: $aggregation, from: $time_from");

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
	 * @param array $items  Key - itemid, value - value_type.
	 *
	 * @return bool
	 */
	public function deleteHistory(array $items) {
		return;
		//return $this->deleteHistoryFromSql($items) && $this->deleteHistoryFromElasticsearch(array_keys($items));
	}

	/**
	 * Get type name by value type id.
	 *
	 * @param int $value_type  Value type id.
	 *
	 * @return string  Value type name.
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
	 * @param int $type_name  Value type name.
	 *
	 * @return int  Value type id.
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
	 * @param int $value_type  Value type id.
	 *
	 * @return string  Data source type.
	 */
	public static function getDataSourceType($value_type) {
		static $cache = [];

		if (!array_key_exists($value_type, $cache)) {
			global $HISTORY;

			if (is_array($HISTORY) && array_key_exists('types', $HISTORY) && is_array($HISTORY['types'])) {
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



	/**
	 * Return the name of the table where the data for the given value type is stored.
	 *
	 * @param int $value_type  Value type.
	 *
	 * @return string|array  Table name | all tables.
	 */
	public static function getTableName($value_type = null) {
		$tables = [
			ITEM_VALUE_TYPE_LOG => 'history_log',
			ITEM_VALUE_TYPE_TEXT => 'history_text',
			ITEM_VALUE_TYPE_STR => 'history_str',
			ITEM_VALUE_TYPE_FLOAT => 'history',
			ITEM_VALUE_TYPE_UINT64 => 'history_uint'
		];

		return ($value_type === null) ? $tables : $tables[$value_type];
	}

	/**
	 * Returns the items grouped by the storage type.
	 *
	 * @param array $items  An array of items with the 'value_type' property.
	 *
	 * @return array  An array with storage type as a keys and item arrays as a values.
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
