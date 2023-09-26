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


class CControllerQueueDetails extends CController {

	protected function init() {
		$this->disableCsrfValidation();
	}

	protected function checkInput() {
		// VAR	TYPE	OPTIONAL	FLAGS	VALIDATION	EXCEPTION
		$fields = [
			'filter_type' => 'in '.implode(',', [ITEM_TYPE_ZABBIX, ITEM_TYPE_TRAPPER, ITEM_TYPE_SIMPLE, ITEM_TYPE_INTERNAL, 
						ITEM_TYPE_ZABBIX_ACTIVE, ITEM_TYPE_HTTPTEST, ITEM_TYPE_EXTERNAL, ITEM_TYPE_DB_MONITOR, ITEM_TYPE_IPMI, 
						ITEM_TYPE_SSH, ITEM_TYPE_TELNET, ITEM_TYPE_CALCULATED, ITEM_TYPE_JMX, ITEM_TYPE_SNMPTRAP, 
						ITEM_TYPE_DEPENDENT, ITEM_TYPE_HTTPAGENT, ITEM_TYPE_SNMP, ITEM_TYPE_SCRIPT]),
			'action' =>	'in queue.details',
		];
		
		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}
		
	protected function checkPermissions() {
		return $this->checkAccess(CRoleHelper::UI_ADMINISTRATION_QUEUE);
	}

	protected function doAction() {
		global $ZBX_SERVER, $ZBX_SERVER_PORT;

		$zabbix_server = new CZabbixServer($ZBX_SERVER, $ZBX_SERVER_PORT,
			timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::CONNECT_TIMEOUT)),
			timeUnitToSeconds(CSettingsHelper::get(CSettingsHelper::SOCKET_TIMEOUT)), ZBX_SOCKET_BYTES_LIMIT
		);

		$filter_item_type = null;

		if ($this->hasInput('filter_type'))
			$filter_item_type = $this->getInput('filter_type');
	
		//show_error_message("Fetching data for type ". $item_type);

		$queue_data = $zabbix_server->getQueue(CZabbixServer::QUEUE_DETAILS, CSessionHelper::getId(),
			CSettingsHelper::get(CSettingsHelper::SEARCH_LIMIT), $filter_item_type
		);
		
		if ($zabbix_server->getError()) {
			$items = [];
			$hosts = [];
			$queue_data = [];
			$proxies = [];

			error($zabbix_server->getError());
			show_error_message(_('Cannot display item queue.'));
		}
		else {
			$queue_data = array_column($queue_data, null, 'itemid');
			$items = API::Item()->get([
				'output' => ['hostid', 'name', 'type'],
				'selectHosts' => ['name'],
				'itemids' => array_keys($queue_data),
				'webitems' => true,
				'preservekeys' => true
			]);

			if (count($queue_data) != count($items)) {
				$items += API::DiscoveryRule()->get([
					'output' => ['hostid', 'name'],
					'selectHosts' => ['name'],
					'itemids' => array_diff(array_keys($queue_data), array_keys($items)),
					'preservekeys' => true
				]);
			}

			$hosts = API::Host()->get([
				'output' => ['proxy_hostid'],
				'hostids' => array_column($items, 'hostid', 'hostid'),
				'preservekeys' => true,
				'selectInterfaces' => ['type', 'useip', 'ip', 'dns', 'port', 'version', 'details', 'available', 'error', 'include_named'],
			]);

			$proxy_hostids = [];
			foreach ($hosts as $host) {
				if ($host['proxy_hostid']) {
					$proxy_hostids[$host['proxy_hostid']] = true;
				}
			}

			$proxies = [];

			if ($proxy_hostids) {
				$proxies = API::Proxy()->get([
					'proxyids' => array_keys($proxy_hostids),
					'output' => ['proxyid', 'host'],
					'preservekeys' => true
				]);
			}
		}

		$response = new CControllerResponseData([
			'items' => $items,
			'hosts' => $hosts,
			'proxies' => $proxies,
			'queue_data' => $queue_data,
			'total_count' => $zabbix_server->getTotalCount(),
			'filter_item_type' => $filter_item_type
		]);

		$title = _('Queue');
		if (CWebUser::getRefresh()) {
			$title .= ' ['._s('refreshed every %1$s sec.', CWebUser::getRefresh()).']';
		}
		$response->setTitle($title);

		$this->setResponse($response);
	}
}
