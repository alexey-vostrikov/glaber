<?php
/*
** Copyright (C) 2001-2038 Glaber
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

namespace Modules\GlaberLatestView\Actions;
require_once 'CControllerGlaberLatest.php';

/**
 * Controller for the "Latest data" page.
 */
class CControllerGlaberLatestView extends CControllerGlaberLatest {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'page' =>						'ge 1',

			// filter inputs
			'groupids' =>			'array_id',
			'hostids' =>				'array_id',
			//'filter_select' =>				'string',
			'filter_show_without_data' =>	'in 0,1',
			'filter_group_by_discovery' =>	'in 0,1',
			'filter_show_details' =>		'in 1',
			'filter_set' =>					'in 1',
			'filter_rst' =>					'in 1',
			'filter_evaltype' =>			'in '.TAG_EVAL_TYPE_AND_OR.','.TAG_EVAL_TYPE_OR,
			'filter_tags' =>				'array',

			// table sorting inputs
			'sort' =>						'in host,name,lastclock',
			'sortorder' =>					'in '.ZBX_SORT_DOWN.','.ZBX_SORT_UP
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new \CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return $this->checkAccess(\CRoleHelper::UI_MONITORING_LATEST_DATA);
	}

	protected function doAction() {

		
		if ($this->hasInput('filter_set')) {
			//user has set the filter fields, remembering all of them
			
			\CProfile::update('web.latest.filter.show_without_data', $this->getInput('filter_show_without_data', 0), PROFILE_TYPE_INT);
			\CProfile::update('web.latest.filter.group_by_discovery', $this->getInput('filter_group_by_discovery', 0), PROFILE_TYPE_INT );
			\CProfile::update('web.latest.filter.show_details', $this->getInput('filter_show_details', 0), PROFILE_TYPE_INT);

		} 
		
		if ($this->hasInput('groupids') || $this->hasInput('hostids')) {
			//the page has been also opened via external link having host/group fields
			//setting host and group filters

			\CProfile::updateArray('web.latest.filter.groupids', $this->getInput('groupids', []),PROFILE_TYPE_ID	);
			\CProfile::updateArray('web.latest.filter.hostids', $this->getInput('hostids', []), PROFILE_TYPE_ID);
		}
		if ($this->hasInput('filter_rst')) {
			\CProfile::deleteIdx('web.latest.filter.groupids');
			\CProfile::deleteIdx('web.latest.filter.hostids');
			\CProfile::delete('web.latest.filter.select');
			\CProfile::delete('web.latest.filter.show_without_data');
			\CProfile::delete('web.latest.filter.show_details');
		}

		// Force-check "Show items without data" if there are no hosts selected.
		$hostids = \CProfile::getArray('web.latest.filter.hostids');
		$filter_show_without_data = $hostids ? \CProfile::get('web.latest.filter.show_without_data', 1) : 1;

		$filter = [
			'groupids' => \CProfile::getArray('web.latest.filter.groupids'),
			'hostids' => $hostids,
			'select' => \CProfile::get('web.latest.filter.select', ''),
			'show_without_data' => $filter_show_without_data,
			'show_details' => \CProfile::get('web.latest.filter.show_details', 0),
			'group_by_discovery' => \CProfile::get('web.latest.filter.group_by_discovery', 1),
			'tags' => []
		];

	
		$view_curl = (new \CUrl('zabbix.php'))->setArgument('action', 'latest.view');

		$refresh_curl = (new \CUrl('zabbix.php'))->setArgument('action', 'latest.view.refresh');
		$refresh_data = array_filter([
			'groupids' => $filter['groupids'],
			'hostids' => $filter['hostids'],
			'filter_select' => $filter['select'],
			'filter_show_without_data' => $filter['show_without_data'] ? 1 : null,
			'filter_group_by_discovery' => $filter['group_by_discovery'] ? 1 : null,
			'filter_show_details' => $filter['show_details'] ? 1 : null,
		]);

		// data sort and pager
		$prepared_data = $this->prepareData($filter, "", "");
		
		if (!isset($prepared_data['error']))
			$this->extendData($prepared_data);

		

		$data = [
			'filter' => $filter,
			'view_curl' => $view_curl,
			'refresh_url' => $refresh_curl->getUrl(),
			'refresh_data' => $refresh_data,
			'refresh_interval' => \CWebUser::getRefresh() * 1000,
			'active_tab' => \CProfile::get('web.latest.filter.active', 1),
			'tags' => isset($prepared_data['items']) ? makeTags($prepared_data['items'], true, 'itemid', ZBX_TAG_COUNT_DEFAULT, $filter['tags']): null,
		] + $prepared_data;


		$response = new \CControllerResponseData($data);
		$response->setTitle(_('Latest data'));
		$this->setResponse($response);
	}
}
