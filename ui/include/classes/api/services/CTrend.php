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
 * Class containing methods for operations with trends.
 */
class CTrend extends CApiService {

	public const ACCESS_RULES = [
		'get' => ['min_user_type' => USER_TYPE_ZABBIX_USER]
	];

	public function __construct() {
		// the parent::__construct() method should not be called.
	}

	/**
	 * Get trend data.
	 *
	 * @param array $options
	 * @param int $options['time_from']
	 * @param int $options['time_till']
	 * @param int $options['limit']
	 * @param string $options['order']
	 *
	 * @return array|int trend data as array or false if error
	 */
	public function get($options = []) {
		$default_options = [
			'itemids'		=> null,
			// filter
			'time_from'		=> null,
			'time_till'		=> null,
			// output fields filter
			'output'		=> API_OUTPUT_EXTEND,
			//count output is set when only counts are requested
			'countOutput'	=> false,
			//data limitation
			'limit'			=> null
		];

		$options = zbx_array_merge($default_options, $options);
		

		$storage_items = [];
		$result = ($options['countOutput']) ? 0 : [];

		if ($options['itemids'] === null || $options['itemids']) {
			// Check if items have read permissions.
			$items = API::Item()->get([
				'output' => ['itemid', 'value_type'],
				'itemids' => $options['itemids'],
				'webitems' => true,
				'filter' => ['value_type' => [ITEM_VALUE_TYPE_FLOAT, ITEM_VALUE_TYPE_UINT64]]
			]);
		
			foreach ($items as $item) {
				array_push($storage_items,$item['itemid']);
			}
		}
		
		$options['itemids'] = $storage_items;
		$data = $this->getFromServer($options);
		
		return is_array($data) ? $data : (string) $data;
	}

	//note: count isn't supported yet by the server server api
	private function getFromServer($options) {
		global $ZBX_SERVER, $ZBX_SERVER_PORT;
		//error_log("Server trend code is invoked");
		//error_log(print_r($options,true));

		$result=[];
		if (!$options['countOutput']) {
			//error_log("no counts");
			//if limit is unset, then assume 5k points is enough
			$limit = ($options['limit'] && zbx_ctype_digit($options['limit'])) ? $options['limit'] : 5000;
			$result = [];
			foreach( $options['itemids'] as $itemid) {
					$server = new CZabbixServer($ZBX_SERVER, $ZBX_SERVER_PORT,
					timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::CONNECT_TIMEOUT)),
					timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::ITEM_TEST_TIMEOUT)),
					ZBX_SOCKET_BYTES_LIMIT);
					$add_result = $server->getHistoryData(CSessionHelper::getId(), $itemid, $options['time_from'], $options['time_till'], $limit, "trends"); 
			}

			$result = $this->unsetExtraFields($add_result, ['itemid'], $options['output']);

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
		} else {
			error_log("WARNING: Unsupported option countOutput is invoked");
			return [];
		}
	}
}
