<?php declare(strict_types = 0);
/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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


namespace Widgets\Web\Actions;

use API,
	CApiTagHelper,
	CArrayHelper,
	CControllerDashboardWidgetView,
	CControllerResponseData,
	CRoleHelper,
	Manager;

class WidgetView extends CControllerDashboardWidgetView {

	protected function doAction(): void {
		$filter_groupids = $this->fields_values['groupids'] ? getSubGroups($this->fields_values['groupids']) : null;
		$filter_hostids = $this->fields_values['hostids'] ?: null;
		$filter_maintenance = ($this->fields_values['maintenance'] == 0) ? 0 : null;

		if ($this->fields_values['exclude_groupids']) {
			$exclude_groupids = getSubGroups($this->fields_values['exclude_groupids']);

			if ($filter_hostids === null) {

				// Get all groups if no selected groups defined.
				if ($filter_groupids === null) {
					$filter_groupids = array_keys(API::HostGroup()->get([
						'output' => [],
						'with_hosts' => true,
						'preservekeys' => true
					]));
				}

				$filter_groupids = array_diff($filter_groupids, $exclude_groupids);

				// Get available hosts.
				$filter_hostids = array_keys(API::Host()->get([
					'output' => [],
					'groupids' => $filter_groupids,
					'preservekeys' => true
				]));
			}

			$exclude_hostids = array_keys(API::Host()->get([
				'output' => [],
				'groupids' => $exclude_groupids,
				'preservekeys' => true
			]));

			$filter_hostids = array_diff($filter_hostids, $exclude_hostids);
		}

		$groups = API::HostGroup()->get([
			'output' => ['groupid', 'name'],
			'groupids' => $filter_groupids,
			'hostids' => $filter_hostids,
			'with_monitored_hosts' => true,
			'with_monitored_httptests' => true,
			'preservekeys' => true
		]);

		CArrayHelper::sort($groups, ['name']);

		$groupids = array_keys($groups);

		$hosts = API::Host()->get([
			'output' => [],
			'groupids' => $groupids,
			'hostids' => $filter_hostids,
			'filter' => ['maintenance_status' => $filter_maintenance],
			'monitored_hosts' => true,
			'preservekeys' => true
		]);

		foreach ($groups as &$group) {
			$group += ['ok' => 0, 'failed' => 0, 'unknown' => 0];
		}
		unset($group);

		// Fetch links between HTTP tests and host groups.
		$where_tags = (array_key_exists('tags', $this->fields_values) && $this->fields_values['tags'])
			? CApiTagHelper::addWhereCondition($this->fields_values['tags'], $this->fields_values['evaltype'], 'ht',
				'httptest_tag', 'httptestid'
			)
			: '';

		$result = DbFetchArray(DBselect(
			'SELECT DISTINCT ht.httptestid,hg.groupid'.
			' FROM httptest ht,hosts_groups hg'.
			' WHERE ht.hostid=hg.hostid'.
				' AND '.dbConditionInt('hg.hostid', array_keys($hosts)).
				' AND '.dbConditionInt('hg.groupid', $groupids).
				' AND ht.status='.HTTPTEST_STATUS_ACTIVE.
				(($where_tags !== '') ? ' AND '.$where_tags : '')
		));

		// Fetch HTTP test execution data.
		$httptest_data = Manager::HttpTest()->getLastData(array_column($result, 'httptestid'));

		foreach ($result as $row) {
			$group = &$groups[$row['groupid']];

			if (array_key_exists($row['httptestid'], $httptest_data)
					&& $httptest_data[$row['httptestid']]['lastfailedstep'] !== null) {
				$group[($httptest_data[$row['httptestid']]['lastfailedstep'] != 0) ? 'failed' : 'ok']++;
			}
			else {
				$group['unknown']++;
			}
			unset($group);
		}

		$this->setResponse(new CControllerResponseData([
			'name' => $this->getInput('name', $this->widget->getDefaultName()),
			'groups' => $groups,
			'user' => [
				'debug_mode' => $this->getDebugMode()
			],
			'allowed_ui_hosts' => $this->checkAccess(CRoleHelper::UI_MONITORING_HOSTS)
		]));
	}
}
