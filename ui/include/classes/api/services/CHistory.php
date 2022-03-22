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
 * Class containing methods for operations with histories.
 */
class CHistory extends CApiService {

	public const ACCESS_RULES = [
		'get' => ['min_user_type' => USER_TYPE_ZABBIX_USER]
	];

	protected $tableName;
	protected $tableAlias = 'h';
	protected $sortColumns = ['itemid', 'clock'];

	public function __construct() {
		// considering the quirky nature of the history API,
		// the parent::__construct() method should not be called.
	}

	/**
	 * Get history data.
	 *
	 * @param array  $options
	 * @param int    $options['history']                       History object type to return.
	 * @param array  $options['hostids']                       Return only history from the given hosts.
	 * @param array  $options['itemids']                       Return only history from the given items.
	 * @param int    $options['time_from']                     Return only values that have been received after or at
	 *                                                         the given time.
	 * @param int    $options['time_till']                     Return only values that have been received before or at
	 *                                                         the given time.
	 * @param array  $options['filter']                        Return only those results that exactly match the given
	 *                                                         filter.
	 * @param int    $options['filter']['itemid']
	 * @param int    $options['filter']['clock']
	 * @param mixed  $options['filter']['value']
	 * @param int    $options['filter']['ns']
	 * @param array  $options['search']                        Return results that match the given wildcard search
	 *                                                         (case-insensitive).
	 * @param string $options['search']['value']
	 * @param bool   $options['searchByAny']                   If set to true return results that match any of the
	 *                                                         criteria given in the filter or search parameter instead
	 *                                                         of all of them.
	 * @param bool   $options['startSearch']                   Return results that match the given wildcard search
	 *                                                         (case-insensitive).
	 * @param bool   $options['excludeSearch']                 Return results that do not match the criteria given in
	 *                                                         the search parameter.
	 * @param bool   $options['searchWildcardsEnabled']        If set to true enables the use of "*" as a wildcard
	 *                                                         character in the search parameter.
	 * @param array  $options['output']                        Object properties to be returned.
	 * @param bool   $options['countOutput']                   Return the number of records in the result instead of the
	 *                                                         actual data.
	 * @param array  $options['sortfield']                     Sort the result by the given properties. Refer to a
	 *                                                         specific API get method description for a list of
	 *                                                         properties that can be used for sorting. Macros are not
	 *                                                         expanded before sorting.
	 * @param array  $options['sortorder']                     Order of sorting. If an array is passed, each value will
	 *                                                         be matched to the corresponding property given in the
	 *                                                         sortfield parameter.
	 * @param int    $options['limit']                         Limit the number of records returned.
	 * @param bool   $options['editable']                      If set to true return only objects that the user has
	 *                                                         write permissions to.
	 *
	 * @throws Exception
	 * @return array|int    Data array or number of rows.
	 */
	public function get($options = []) {
		$value_types = [ITEM_VALUE_TYPE_FLOAT, ITEM_VALUE_TYPE_STR, ITEM_VALUE_TYPE_LOG, ITEM_VALUE_TYPE_UINT64,
			ITEM_VALUE_TYPE_TEXT
		];
		$common_value_types = [ITEM_VALUE_TYPE_FLOAT, ITEM_VALUE_TYPE_STR, ITEM_VALUE_TYPE_UINT64,
			ITEM_VALUE_TYPE_TEXT
		];

		$api_input_rules = ['type' => API_OBJECT, 'fields' => [
			// filter
			'history' =>				['type' => API_INT32, 'in' => implode(',', $value_types), 'default' => ITEM_VALUE_TYPE_UINT64],
			'hostids' =>				['type' => API_IDS, 'flags' => API_ALLOW_NULL | API_NORMALIZE, 'default' => null],
			'itemids' =>				['type' => API_IDS, 'flags' => API_ALLOW_NULL | API_NORMALIZE, 'default' => null],
			'time_from' =>				['type' => API_INT32, 'flags' => API_ALLOW_NULL, 'default' => null],
			'time_till' =>				['type' => API_INT32, 'flags' => API_ALLOW_NULL, 'default' => null],
			'filter' =>					['type' => API_MULTIPLE, 'default' => null, 'rules' => [
											['if' => ['field' => 'history', 'in' => implode(',', [ITEM_VALUE_TYPE_LOG])], 'type' => API_OBJECT, 'flags' => API_ALLOW_NULL, 'fields' => [
					'itemid' =>					['type' => API_IDS, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'clock' =>					['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'timestamp' =>				['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'source' =>					['type' => API_STRINGS_UTF8, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'severity' =>				['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'logeventid' =>				['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'ns' =>						['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE]
				]],
											['if' => ['field' => 'history', 'in' => implode(',', [ITEM_VALUE_TYPE_STR, ITEM_VALUE_TYPE_TEXT])], 'type' => API_OBJECT, 'flags' => API_ALLOW_NULL, 'fields' => [
					'itemid' =>					['type' => API_IDS, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'clock' =>					['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'ns' =>						['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE]
				]],
											['if' => ['field' => 'history', 'in' => implode(',', [ITEM_VALUE_TYPE_UINT64])], 'type' => API_OBJECT, 'flags' => API_ALLOW_NULL, 'fields' => [
					'itemid' =>					['type' => API_IDS, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'clock' =>					['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'ns' =>						['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'value' =>					['type' => API_UINTS64, 'flags' => API_ALLOW_NULL | API_NORMALIZE]
				]],
											['if' => ['field' => 'history', 'in' => implode(',', [ITEM_VALUE_TYPE_FLOAT])], 'type' => API_OBJECT, 'flags' => API_ALLOW_NULL, 'fields' => [
					'itemid' =>					['type' => API_IDS, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'clock' =>					['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'ns' =>						['type' => API_INTS32, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'value' =>					['type' => API_FLOATS, 'flags' => API_ALLOW_NULL | API_NORMALIZE]
				]]
			]],
			'search' =>					['type' => API_MULTIPLE, 'default' => null, 'rules' => [
											['if' => ['field' => 'history', 'in' => implode(',', [ITEM_VALUE_TYPE_LOG])], 'type' => API_OBJECT, 'flags' => API_ALLOW_NULL, 'fields' => [
					'source' =>					['type' => API_STRINGS_UTF8, 'flags' => API_ALLOW_NULL | API_NORMALIZE],
					'value' =>					['type' => API_STRINGS_UTF8, 'flags' => API_ALLOW_NULL | API_NORMALIZE]
				]],
											['if' => ['field' => 'history', 'in' => implode(',', [ITEM_VALUE_TYPE_STR, ITEM_VALUE_TYPE_TEXT])], 'type' => API_OBJECT, 'flags' => API_ALLOW_NULL, 'fields' => [
					'value' =>					['type' => API_STRINGS_UTF8, 'flags' => API_ALLOW_NULL | API_NORMALIZE]
				]],
											['if' => ['field' => 'history', 'in' => implode(',', [ITEM_VALUE_TYPE_FLOAT, ITEM_VALUE_TYPE_UINT64])], 'type' => API_OBJECT, 'flags' => API_ALLOW_NULL, 'fields' => []]
			]],
			'searchByAny' =>			['type' => API_BOOLEAN, 'default' => false],
			'startSearch' =>			['type' => API_FLAG, 'default' => false],
			'excludeSearch' =>			['type' => API_FLAG, 'default' => false],
			'searchWildcardsEnabled' =>	['type' => API_BOOLEAN, 'default' => false],
			// output
			'output' =>					['type' => API_MULTIPLE, 'default' => API_OUTPUT_EXTEND, 'rules' => [
											['if' => ['field' => 'history', 'in' => implode(',', [ITEM_VALUE_TYPE_LOG])], 'type' => API_OUTPUT, 'in' => implode(',', ['itemid', 'clock', 'timestamp', 'source', 'severity', 'value', 'logeventid', 'ns'])],
											['if' => ['field' => 'history', 'in' => implode(',', $common_value_types)], 'type' => API_OUTPUT, 'in' => implode(',', ['itemid', 'clock', 'value', 'ns'])]
			]],
			'countOutput' =>			['type' => API_FLAG, 'default' => false],
			// sort and limit
			'sortfield' =>				['type' => API_STRINGS_UTF8, 'flags' => API_NORMALIZE, 'in' => implode(',', $this->sortColumns), 'uniq' => true, 'default' => []],
			'sortorder' =>				['type' => API_SORTORDER, 'default' => []],
			'limit' =>					['type' => API_INT32, 'flags' => API_ALLOW_NULL, 'in' => '1:'.ZBX_MAX_INT32, 'default' => null],
			// flags
			'editable' =>				['type' => API_BOOLEAN, 'default' => false]
		]];

		if (!CApiInputValidator::validate($api_input_rules, $options, '/', $error)) {
			self::exception(ZBX_API_ERROR_PARAMETERS, $error);
		}

		if (self::$userData['type'] != USER_TYPE_SUPER_ADMIN || $options['hostids'] !== null) {
			$items = API::Item()->get([
				'output' => ['itemid'],
				'itemids' => $options['itemids'],
				'hostids' => $options['hostids'],
				'editable' => $options['editable'],
				'webitems' => true,
				'preservekeys' => true
			]);
			$options['itemids'] = array_keys($items);
		}

		$this->tableName = CHistoryManager::getTableName($options['history']);


		return $this->getFromServer($options);
	
	}

/**
	 * server specific implementation of get.
	 *
	 * @see CHistory::get
	 */
	private function getFromServer($options) {
		global $HISTORY;
		
		//error_log(print_r($options,true));
		
		$result = [];
		
		if ($options['itemids'] !== null) {
			if(!is_array($options['itemids'])) $options['itemids'] = [ $options['itemids'] ];
		}
		$values=[];

		foreach( $options['itemids'] as $itemid) {
			
			global $ZBX_SERVER, $ZBX_SERVER_PORT;
			$server =  new CZabbixServer($ZBX_SERVER, $ZBX_SERVER_PORT,
			    timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::CONNECT_TIMEOUT)),
			    timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::SOCKET_TIMEOUT)), 0
			);
	    
			$data = $server->getHistoryData(CSessionHelper::getId(), $itemid, $options['time_from'], $options['time_till'], $options['limit'], "history");
			
			//error_log("$itemid :". print_r($result,true));
			//error_log(print_r($data,true));
			
			if (is_array($data)) {
				$result = array_merge($result, $data);
			}
	
		}
		
		//maybe it's better to add a sortfield here, but it's unlikely 
		//that it could be anything but clock
		if (isset($options['sortorder'])) {
			switch ($options['sortorder']) {
				case 'ASC': 
					usort($result, function($a,$b) { return $a['clock'] - $b['clock']; });
				break;
	
				case 'DESC': 
					usort($result, function($a,$b) { return $b['clock'] - $a['clock']; });
				break;					 
			}

		}

		return $result;
	}
		
}