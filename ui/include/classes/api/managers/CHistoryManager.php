<?php
/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

	private $primary_keys_enabled = false;

	/**
	 * Whether to enable optimizations that make use of PRIMARY KEY (itemid, clock, ns) on the history tables.
	 *
	 * @param bool $enabled
	 *
	 * @return CHistoryManager
	 */
	public function setPrimaryKeysEnabled(bool $enabled = true) {
		$this->primary_keys_enabled = $enabled;

		return $this;
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
		return $this->getLastValuesFromServer($items, $limit, $period);
	}

	private function getLastValuesFromServer($items, $limit, $period) {
		$result=[];
						
		$last_values = CZabbixServer::getLastValues(CSessionHelper::getId(),array_column($items,'itemid'),$limit, $period); 
			
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
	public function getValueAt(array $item, $clock, $ns) {
		return $this->getGraphAggregationByWidthFromServer($items, $clock, $clock+1, 1, 'history');
	}

	private function getGraphAggregationByIntervalFromServer(array $items, $time_from, $time_to, $function, $interval) {
		$results = [];
		$items_by_table = [];
			
		$width = (int)(($time_to -$time_from)/$interval);
			
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
					case AGGREGATE_MIN:
						$row['value'] = $value['min'];
						break;
					case AGGREGATE_MAX:
						$row['value'] = $value['max'];
						break;
					case AGGREGATE_AVG:
						$row['value'] = $value['avg'];
						break;
					case AGGREGATE_COUNT:
						$row['count'] = $value['count'];
						break;
					case AGGREGATE_SUM:
						$row['value'] = $value['count']*$value['avg'];
						break;
		
					//oooops, this is something which cannot be done right now
					//TODO figure how to do this or remove first and last from the menu
					case AGGREGATE_FIRST:
						$row['value'] = $value['avg'];
							//$sql_select[] = 'MIN(clock) AS clock';
						break;
					case AGGREGATE_LAST:
						$row['value'] = $value['avg'];
						break;
				}
		 
				$results[$value['itemid']]['source'][] = 'history';
				$results[$value['itemid']]['data'][] = $row;
			}
		}
		return $results;
	}
		 
	
	/**
	 * Returns history value aggregation.
	 *
	 * The $item parameter must have the value_type, itemid and source properties set.
	 *
	 * @param array  $items      Items to get aggregated values for.
	 * @param int    $time_from  Minimal timestamp (seconds) to get data from.
	 * @param int    $time_to    Maximum timestamp (seconds) to get data from.
	 * @param string $function   Function for data aggregation.
	 * @param string $interval   Aggregation interval in seconds.
	 *
	 * @return array  History value aggregation.
	 */
	public function getAggregationByInterval(array $items, $time_from, $time_to, $function, $interval) {
		return $this->getGraphAggregationByIntervalFromServer($items, $time_from, $time_to, $function, $interval);
	}

	public function getItemsHavingValues(array $items, $period ) {
		return $this->getLastValuesFromServer($items, 1, $period);
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
	public function getGraphAggregationByWidth(array $items, $time_from, $time_to, $aggregates) {
		$results = [];
		$agg_results = [];
		
		$agg_results += $this->getGraphAggregationByWidthFromServer($items,$time_from, $time_to, $aggregates, "history" );

		foreach ($items as $item) {
		
			$results[$item['itemid']]['data'] = [];
			$history_start = $time_to;
			
			if (isset($agg_results[$item['itemid']]['data']) && is_array($agg_results[$item['itemid']]['data'])) {
			
				$results[$item['itemid']]['data'] += $agg_results[$item['itemid']]['data'];

				foreach ( $agg_results[$item['itemid']]['data'] as $value) {
					if ( $value['clock'] < $history_start ) 
						$history_start = $value['clock'];
				}
				
			}
						
			if ($history_start - $time_from > 3600 ) {
			
				$trend_results = [];
				$trend_results += $this->getGraphAggregationByWidthFromServer($items,$time_from, $time_to, $aggregates, "trends");		
				
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

		return $results;
	}
	
	private function getGraphAggregationByWidthFromServer(array $items, $time_from, $time_to, $aggregates, $source) {
		global $ZBX_SERVER, $ZBX_SERVER_PORT;

		foreach ($items as $item) {
			$results[$item['itemid']]['data'] = 
				CZabbixServer::getHistoryAggregatedData(CSessionHelper::getId(), $item['itemid'], $time_from, $time_to, $aggregates, $source); 
		
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

	}
	
}
