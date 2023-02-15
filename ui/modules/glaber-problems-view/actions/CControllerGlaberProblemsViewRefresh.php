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

namespace Modules\GlaberProblemsView\Actions;

class CControllerGlaberProblemsViewRefresh extends CControllerGlaberProblems {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'page' =>						'ge 1',

			// filter inputs
			'groupids' =>			'array_id',
			'hostids' =>				'array_id',
			'filter_select' =>				'string',
			'filter_show_without_data' =>	'in 1',
			'filter_group_by_discovery' =>	'in 1',
			'filter_show_details' =>		'in 1',
			'filter_evaltype' =>			'in '.TAG_EVAL_TYPE_AND_OR.','.TAG_EVAL_TYPE_OR,
			'filter_tags' =>				'array',

			// table sorting inputs
			'sort' =>						'in host,name,lastclock',
			'sortorder' =>					'in '.ZBX_SORT_DOWN.','.ZBX_SORT_UP
		];

		$ret = $this->validateInput($fields);

		if ($ret) {
			// Hosts must have been selected as well if filtering items with data only.
			if (!$this->getInput('hostids', []) && !$this->getInput('filter_show_without_data', 0)) {
				$ret = false;
			}
		}

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return $this->checkAccess(CRoleHelper::UI_MONITORING_PROBLEMS);
	}

	protected function doAction() {
		// filter
		$filter = [
			'groupids' => $this->hasInput('groupids') ? $this->getInput('groupids') : null,
			'hostids' => $this->hasInput('hostids') ? $this->getInput('hostids') : null,
			'select' => $this->getInput('filter_select', ''),
			'show_without_data' =>  $this->hasInput('filter_show_without_data') 
						? $this->getInput('filter_show_without_data') : CProfile::get('filter_show_without_data', 0),
			'show_details' => $this->hasInput('filter_show_details') 
						? $this->getInput('filter_show_details') : CProfile::get('filter_show_details', 0),
			'group_by_discovery' =>  $this->hasInput('filter_group_by_discovery') 
						? $this->getInput('filter_group_by_discovery') : CProfile::get('filter_group_by_discovery', 1),
			'evaltype' => CProfile::get('web.problems.filter.evaltype', TAG_EVAL_TYPE_AND_OR),
			'tags' => []
		];

		// Tags filters.
		foreach (CProfile::getArray('web.problems.filter.tags.tag', []) as $i => $tag) {
			$filter['tags'][] = [
				'tag' => $tag,
				'value' => CProfile::get('web.problems.filter.tags.value', null, $i),
				'operator' => CProfile::get('web.problems.filter.tags.operator', null, $i)
			];
		}

		$sort_field = $this->getInput('sort', 'name');
		$sort_order = $this->getInput('sortorder', ZBX_SORT_UP);

		$view_curl = (new CUrl('zabbix.php'))->setArgument('action', 'problem.view');

		$data = [
			'filter' => $filter,
			'view_curl' => $view_curl,
			'tags' => makeTags($prepared_data['items'], true, 'itemid', ZBX_TAG_COUNT_DEFAULT, $filter['tags'])
		] + $prepared_data;

		$response = new CControllerResponseData($data);
		$this->setResponse($response);
	}
}
