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
 * Base controller for the "Latest data" page and the "Latest data" asynchronous refresh page.
 */
abstract class CControllerLatest extends CController {

	/**
	 * Prepare the latest data based on the given filter and sorting options.
	 *
	 * @param array  $filter                       Item filter options.
	 * @param array  $filter['groupids']           Filter items by host groups.
	 * @param array  $filter['hostids']            Filter items by hosts.
	 * @param string $filter['select']             Filter items by name.
	 * @param int    $filter['evaltype']           Filter evaltype.
	 * @param array  $filter['tags']               Filter tags.
	 * @param string $filter['tags'][]['tag']
	 * @param string $filter['tags'][]['value']
	 * @param int    $filter['tags'][]['operator']
	 * @param int    $filter['show_without_data']  Include items with empty history.
	 * @param string $sort_field                   Sorting field.
	 * @param string $sort_order                   Sorting order.
	 *
	 * @return array
	 */
	protected function prepareData(array $filter, $sort_field, $sort_order) {
		// Select groups for subsequent selection of hosts and items.
		$multiselect_hostgroup_data = [];
		$groupids = $filter['groupids'] ? getSubGroups($filter['groupids'], $multiselect_hostgroup_data) : null;
		$group_by_entity = $filter['group_by_discovery'] ? $filter['group_by_discovery'] : null;
		
		// Select hosts for subsequent selection of items.
		$hosts = API::Host()->get([
			'output' => ['hostid', 'name', 'status'],
			'groupids' => $groupids,
			'hostids' => $filter['hostids'] ? $filter['hostids'] : null,
			'monitored_hosts' => true,
			'preservekeys' => true
		]);

		$search_limit = CSettingsHelper::get(CSettingsHelper::SEARCH_LIMIT);
		$history_period = timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::HISTORY_PERIOD));
		$select_items_cnt = 0;
		$select_items = [];

		$fetch_options = [
			'output' => ['itemid', 'type', 'hostid', 'name', 'key_', 'delay', 'history', 'trends', 'status',
					 'value_type', 'units', 'description', 'state', 'error'],
			'selectTags' => ['tag', 'value'],
			'selectValueMap' => ['mappings'],
			'hostids' => array_column($hosts,'hostid'),
			'webitems' => true,
			'evaltype' => $filter['evaltype'],
			'tags' => $filter['tags'] ? $filter['tags'] : null,
			'filter' => [ 'status' => [ITEM_STATUS_ACTIVE]	],
			'search' => ($filter['select'] === '') ? null : ['name' => $filter['select']],
			'preservekeys' => true,
			'discovery_items' => true,
		];

		if (isset($group_by_entity)) {
			$fetch_options += [
				'selectTemplates' => ['templateid','name'],
				'selectDiscoveryRule' => ['itemid', 'name', 'templateid', 'key_'],
				'selectItemDiscovery' => ['parent_itemid','itemdiscoveryid','itemid'],
			];
		}
		
		$items = API::Item()->get($fetch_options);

		$multiselect_host_data = $filter['hostids']
			? API::Host()->get([
				'output' => ['hostid', 'name'],
				'hostids' => $filter['hostids']
			])
			: [];

		$discovery_entitites = API::DiscoveryEntity()->get ([
				'hostids' => array_column($hosts, 'hostid'),
				'items' => $items
			]
		);

		return [
			'hosts' => $hosts,
			'items' => $items,
			'entities' => $discovery_entitites,
			'multiselect_hostgroup_data' => $multiselect_hostgroup_data,
			'multiselect_host_data' => CArrayHelper::renameObjectsKeys($multiselect_host_data, ['hostid' => 'id'])
		];
	}

	/**
	 * Extend previously prepared data.
	 *
	 * @param array $prepared_data      Data returned by prepareData method.
	 */
	protected function extendData(array &$prepared_data) {
		$items = CMacrosResolverHelper::resolveItemKeys($prepared_data['items']);
		$items = CMacrosResolverHelper::resolveItemNames($items);
		$items = CMacrosResolverHelper::resolveItemDescriptions($items);
		$items = CMacrosResolverHelper::resolveTimeUnitMacros($items, ['delay', 'history', 'trends']);

		//error_log("Gettind items".print_r($items,true));
		
		$history = Manager::History()->getLastValues($items, 10,
			timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::HISTORY_PERIOD))
		);

		$prepared_data['items'] = $items;
		$prepared_data['history'] = $history;
	}
}
