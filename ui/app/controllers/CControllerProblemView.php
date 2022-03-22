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


class CControllerProblemView extends CControllerProblem {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'show' =>					'in '.TRIGGERS_OPTION_RECENT_PROBLEM.','.TRIGGERS_OPTION_IN_PROBLEM.','.TRIGGERS_OPTION_ALL,
			'groupids' =>				'array_id',
			'hostids' =>				'array_id',
			'triggerids' =>				'array_id',
			'name' =>					'string',
			'severities' =>				'array',
			'age_state' =>				'in 0,1',
			'age' =>					'int32',
			'inventory' =>				'array',
			'evaltype' =>				'in '.TAG_EVAL_TYPE_AND_OR.','.TAG_EVAL_TYPE_OR,
			'tags' =>					'array',
			'show_tags' =>				'in '.PROBLEMS_SHOW_TAGS_NONE.','.PROBLEMS_SHOW_TAGS_1.','.PROBLEMS_SHOW_TAGS_2.','.PROBLEMS_SHOW_TAGS_3,
			'show_suppressed' =>		'in 0,1',
			'unacknowledged' =>			'in 0,1',
			'compact_view' =>			'in 0,1',
			'show_timeline' =>			'in 0,1',
			'details' =>				'in 0,1',
			'highlight_row' =>			'in 0,1',
			'show_opdata' =>			'in '.OPERATIONAL_DATA_SHOW_NONE.','.OPERATIONAL_DATA_SHOW_SEPARATELY.','.OPERATIONAL_DATA_SHOW_WITH_PROBLEM,
			'tag_name_format' =>		'in '.PROBLEMS_TAG_NAME_FULL.','.PROBLEMS_TAG_NAME_SHORTENED.','.PROBLEMS_TAG_NAME_NONE,
			'tag_priority' =>			'string',
			'from' =>					'range_time',
			'to' =>						'range_time',
			'sort' =>					'in clock,host,severity,name',
			'sortorder' =>				'in '.ZBX_SORT_DOWN.','.ZBX_SORT_UP,
			'page' =>					'ge 1',
			'uncheck' =>				'in 1',
			'filter_name' =>			'string',
			'filter_custom_time' =>		'in 1,0',
			'filter_show_counter' =>	'in 1,0',
			'filter_counters' =>		'in 1',
			'filter_reset' =>			'in 1',
			'counter_index' =>			'ge 0'
		];

		$ret = $this->validateInput($fields) && $this->validateTimeSelectorPeriod();

		if ($ret && $this->hasInput('inventory')) {
			foreach ($this->getInput('inventory') as $filter_inventory) {
				if (count($filter_inventory) != 2
						|| !array_key_exists('field', $filter_inventory) || !is_string($filter_inventory['field'])
						|| !array_key_exists('value', $filter_inventory) || !is_string($filter_inventory['value'])) {
					$ret = false;
					break;
				}
			}
		}

		if ($ret && $this->hasInput('tags')) {
			foreach ($this->getInput('tags') as $filter_tag) {
				if (count($filter_tag) != 3
						|| !array_key_exists('tag', $filter_tag) || !is_string($filter_tag['tag'])
						|| !array_key_exists('value', $filter_tag) || !is_string($filter_tag['value'])
						|| !array_key_exists('operator', $filter_tag) || !is_string($filter_tag['operator'])) {
					$ret = false;
					break;
				}
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
		$filter_tabs = [];
		$profile = (new CTabFilterProfile(static::FILTER_IDX, static::FILTER_FIELDS_DEFAULT))
			->read()
			->setInput($this->cleanInput($this->getInputAll()));

		foreach ($profile->getTabsWithDefaults() as $index => $filter_tab) {
			if ($filter_tab['filter_custom_time']) {
				$filter_tab['show'] = TRIGGERS_OPTION_ALL;
				$filter_tab['filter_src']['show'] = TRIGGERS_OPTION_ALL;
			}

			if ($index == $profile->selected) {
				// Initialize multiselect data for filter_scr to allow tabfilter correctly handle unsaved state.
				$filter_tab['filter_src']['filter_view_data'] = $this->getAdditionalData($filter_tab['filter_src']);
			}

			$filter_tabs[] = $filter_tab + ['filter_view_data' => $this->getAdditionalData($filter_tab)];
		}

		$filter = $filter_tabs[$profile->selected];

		$data = [
			'action' => $this->getAction(),
			'tabfilter_idx' => static::FILTER_IDX,
			'filter' => $filter,
			'filter_view' => 'monitoring.problem.filter',
			'filter_defaults' => $profile->filter_defaults,
			'tabfilter_options' => [
				'idx' => static::FILTER_IDX,
				'selected' => $profile->selected,
				'support_custom_time' => 1,
				'expanded' => $profile->expanded,
				'page' => $filter['page'],
				'timeselector' => [
					'from' => $profile->from,
					'to' => $profile->to,
					'disabled' => ($filter['show'] != TRIGGERS_OPTION_ALL || $filter['filter_custom_time'])
				] + getTimeselectorActions($profile->from, $profile->to)
			],
			'filter_tabs' => $filter_tabs,
			'refresh_interval' => CWebUser::getRefresh() * 1000,
			'inventories' => array_column(getHostInventories(), 'title', 'db_field'),
			'sort' => $filter['sort'],
			'sortorder' => $filter['sortorder'],
			'uncheck' => $this->hasInput('filter_reset'),
			'page' => $this->getInput('page', 1)
		];

		$response = new CControllerResponseData($data);
		$response->setTitle(_('Problems'));

		if ($data['action'] === 'problem.view.csv') {
			$response->setFileName('zbx_problems_export.csv');
		}

		$this->setResponse($response);
	}
}
