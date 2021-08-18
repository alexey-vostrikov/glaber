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


class CControllerDashboardWidgetEdit extends CController {

	private $context;

	protected function checkInput() {
		$fields = [
			'templateid' => 'db dashboard.templateid',
			'type' => 'string',
			'name' => 'string',
			'view_mode' => 'in '.implode(',', [ZBX_WIDGET_VIEW_MODE_NORMAL, ZBX_WIDGET_VIEW_MODE_HIDDEN_HEADER]),
			'prev_type' => 'string',
			'fields' => 'json',
			'unique_id' => 'string',
			'dashboard_page_unique_id' => 'string'
		];

		$ret = $this->validateInput($fields);

		if ($ret) {
			$this->context = $this->hasInput('templateid')
				? CWidgetConfig::CONTEXT_TEMPLATE_DASHBOARD
				: CWidgetConfig::CONTEXT_DASHBOARD;

			if ($this->hasInput('type')) {
				if (!CWidgetConfig::isWidgetTypeSupportedInContext($this->getInput('type'), $this->context)) {
					$ret = false;
				}
			}
		}

		if (!$ret) {
			$this->setResponse((new CControllerResponseData([]))->disableView());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return ($this->context === CWidgetConfig::CONTEXT_TEMPLATE_DASHBOARD)
			? ($this->getUserType() >= USER_TYPE_ZABBIX_ADMIN)
			: ($this->getUserType() >= USER_TYPE_ZABBIX_USER);
	}

	protected function doAction() {
		$known_widget_types = CWidgetConfig::getKnownWidgetTypes($this->context);

		natsort($known_widget_types);

		if ($this->hasInput('type')) {
			$type = $this->getInput('type');

			if (!array_key_exists($type, $known_widget_types) || $this->getInput('prev_type', $type) !== $type) {
				CProfile::update('web.dashboard.last_widget_type', $type, PROFILE_TYPE_STR);
			}
		}
		else {
			$type = CProfile::get('web.dashboard.last_widget_type');
			if (!array_key_exists($type, $known_widget_types)) {
				$type = array_keys($known_widget_types)[0];
			}
		}

		$templateid = ($this->context === CWidgetConfig::CONTEXT_TEMPLATE_DASHBOARD)
			? $this->getInput('templateid')
			: null;

		$form = CWidgetConfig::getForm($type, $this->getInput('fields', '{}'), $templateid);

		// Transforms corrupted data to default values.
		$form->validate();

		$this->setResponse(new CControllerResponseData([
			'user' => [
				'debug_mode' => $this->getDebugMode()
			],
			'dialogue' => [
				'type' => $type,
				'name' => $this->getInput('name', ''),
				'view_mode' => $this->getInput('view_mode', ZBX_WIDGET_VIEW_MODE_NORMAL),
				'fields' => $form->getFields()
			],
			'templateid' => $templateid,
			'unique_id' => $this->hasInput('unique_id') ? $this->getInput('unique_id') : null,
			'dashboard_page_unique_id' => $this->hasInput('dashboard_page_unique_id')
				? $this->getInput('dashboard_page_unique_id')
				: null,
			'known_widget_types' => $known_widget_types,
			'captions' => $this->getCaptions($form)
		]));
	}

	/**
	 * Prepares mapped list of names for all required resources.
	 *
	 * @param CWidgetForm $form
	 *
	 * @return array
	 */
	private function getCaptions($form) {
		$captions = ['simple' => [], 'ms' => []];

		foreach ($form->getFields() as $field) {
			if ($field instanceof CWidgetFieldSelectResource) {
				$resource_type = $field->getResourceType();
				$id = $field->getValue();

				if (!array_key_exists($resource_type, $captions['simple'])) {
					$captions['simple'][$resource_type] = [];
				}

				if ($id != 0) {
					switch ($resource_type) {
						case WIDGET_FIELD_SELECT_RES_SYSMAP:
							$captions['simple'][$resource_type][$id] = _('Inaccessible map');
							break;
					}
				}
			}
		}

		foreach ($captions['simple'] as $resource_type => &$list) {
			if (!$list) {
				continue;
			}

			switch ($resource_type) {
				case WIDGET_FIELD_SELECT_RES_SYSMAP:
					$maps = API::Map()->get([
						'sysmapids' => array_keys($list),
						'output' => ['sysmapid', 'name']
					]);

					if ($maps) {
						foreach ($maps as $key => $map) {
							$list[$map['sysmapid']] = $map['name'];
						}
					}
					break;
			}
		}
		unset($list);

		// Prepare data for CMultiSelect controls.
		$groupids = [];
		$hostids = [];
		$itemids = [];
		$graphids = [];
		$prototype_itemids = [];
		$prototype_graphids = [];

		foreach ($form->getFields() as $field) {
			if ($field instanceof CWidgetFieldMsGroup) {
				$key = 'groups';
				$var = 'groupids';
			}
			elseif ($field instanceof CWidgetFieldMsHost) {
				$key = 'hosts';
				$var = 'hostids';
			}
			elseif ($field instanceof CWidgetFieldMsItem) {
				$key = 'items';
				$var = 'itemids';
			}
			elseif ($field instanceof CWidgetFieldMsGraph) {
				$key = 'graphs';
				$var = 'graphids';
			}
			elseif ($field instanceof CWidgetFieldMsItemPrototype) {
				$key = 'item_prototypes';
				$var = 'prototype_itemids';
			}
			elseif ($field instanceof CWidgetFieldMsGraphPrototype) {
				$key = 'graph_prototypes';
				$var = 'prototype_graphids';
			}
			else {
				continue;
			}

			$field_name = $field->getName();
			$captions['ms'][$key][$field_name] = [];

			foreach ($field->getValue() as $id) {
				$captions['ms'][$key][$field_name][$id] = ['id' => $id];
				$tmp = &$$var;
				$tmp[$id][] = $field_name;
			}
		}

		if ($groupids) {
			$groups = API::HostGroup()->get([
				'output' => ['name'],
				'groupids' => array_keys($groupids),
				'preservekeys' => true
			]);

			foreach ($groups as $groupid => $group) {
				foreach ($groupids[$groupid] as $field_name) {
					$captions['ms']['groups'][$field_name][$groupid]['name'] = $group['name'];
				}
			}
		}

		if ($hostids) {
			$hosts = API::Host()->get([
				'output' => ['name'],
				'hostids' => array_keys($hostids),
				'preservekeys' => true
			]);

			foreach ($hosts as $hostid => $host) {
				foreach ($hostids[$hostid] as $field_name) {
					$captions['ms']['hosts'][$field_name][$hostid]['name'] = $host['name'];
				}
			}
		}

		if ($itemids) {
			$items = API::Item()->get([
				'output' => ['itemid', 'hostid', 'name', 'key_'],
				'selectHosts' => ['name'],
				'itemids' => array_keys($itemids),
				'webitems' => true,
				'preservekeys' => true
			]);

			$items = CMacrosResolverHelper::resolveItemNames($items);

			foreach ($items as $itemid => $item) {
				foreach ($itemids[$itemid] as $field_name) {
					$captions['ms']['items'][$field_name][$itemid] += [
						'name' => $item['name_expanded'],
						'prefix' => $item['hosts'][0]['name'].NAME_DELIMITER
					];
				}
			}
		}

		if ($graphids) {
			$graphs = API::Graph()->get([
				'output' => ['graphid', 'name'],
				'selectHosts' => ['name'],
				'graphids' => array_keys($graphids),
				'preservekeys' => true
			]);

			foreach ($graphs as $graphid => $graph) {
				foreach ($graphids[$graphid] as $field_name) {
					$captions['ms']['graphs'][$field_name][$graphid] += [
						'name' => $graph['name'],
						'prefix' => $graph['hosts'][0]['name'].NAME_DELIMITER
					];
				}
			}
		}

		if ($prototype_itemids) {
			$item_prototypes = API::ItemPrototype()->get([
				'output' => ['itemid', 'hostid', 'name', 'key_'],
				'selectHosts' => ['name'],
				'itemids' => array_keys($prototype_itemids),
				'preservekeys' => true
			]);

			$item_prototypes = CMacrosResolverHelper::resolveItemNames($item_prototypes);

			foreach ($item_prototypes as $itemid => $item) {
				foreach ($prototype_itemids[$itemid] as $field_name) {
					$captions['ms']['item_prototypes'][$field_name][$itemid] += [
						'name' => $item['name_expanded'],
						'prefix' => $item['hosts'][0]['name'].NAME_DELIMITER
					];
				}
			}
		}

		if ($prototype_graphids) {
			$graph_prototypes = API::GraphPrototype()->get([
				'output' => ['graphid', 'name'],
				'selectHosts' => ['name'],
				'graphids' => array_keys($prototype_graphids),
				'preservekeys' => true
			]);

			foreach ($graph_prototypes as $graphid => $graph) {
				foreach ($prototype_graphids[$graphid] as $field_name) {
					$captions['ms']['graph_prototypes'][$field_name][$graphid] += [
						'name' => $graph['name'],
						'prefix' => $graph['hosts'][0]['name'].NAME_DELIMITER
					];
				}
			}
		}

		$inaccessible_resources = [
			'groups' => _('Inaccessible group'),
			'hosts' => _('Inaccessible host'),
			'items' => _('Inaccessible item'),
			'graphs' => _('Inaccessible graph'),
			'item_prototypes' => _('Inaccessible item prototype'),
			'graph_prototypes' => _('Inaccessible graph prototype')
		];

		foreach ($captions['ms'] as $resource_type => &$fields_captions) {
			foreach ($fields_captions as &$field_captions) {
				$n = 0;

				foreach ($field_captions as &$caption) {
					if (!array_key_exists('name', $caption)) {
						$postfix = (++$n > 1) ? ' ('.$n.')' : '';
						$caption['name'] = $inaccessible_resources[$resource_type].$postfix;
						$caption['inaccessible'] = true;
					}
				}
				unset($caption);
			}
			unset($field_captions);
		}
		unset($fields_captions);

		return $captions;
	}
}
