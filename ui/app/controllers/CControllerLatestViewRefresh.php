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


/**
 * Controller for the "Latest data" asynchronous refresh page.
 */
class CControllerLatestViewRefresh extends CControllerLatestView {

	protected function doAction(): void {
		if ($this->getInput('filter_counters', 0) != 0) {
			$profile = (new CTabFilterProfile(static::FILTER_IDX, static::FILTER_FIELDS_DEFAULT))->read();
			$filters = $this->hasInput('counter_index')
				? [$profile->getTabFilter($this->getInput('counter_index'))]
				: $profile->getTabsWithDefaults();

			$filter_counters = [];

			foreach ($filters as $index => $tabfilter) {
				if (!$tabfilter['filter_show_counter']) {
					$filter_counters[$index] = 0;

					continue;
				}

				$prepared_data = $this->prepareData($tabfilter, $tabfilter['sort'], $tabfilter['sortorder']);
			}

			$this->setResponse(
				(new CControllerResponseData([
					'main_block' => json_encode(['filter_counters' => $filter_counters])
				]))->disableView()
			);
		}
		else {
			$filter = static::FILTER_FIELDS_DEFAULT;
			$this->getInputs($filter, array_keys($filter));
			$filter = $this->cleanInput($filter);

			$prepared_data = $this->prepareData($filter, null, null);

			$view_url = (new CUrl('zabbix.php'))->setArgument('action', 'latest.view');
			$paging_arguments = array_filter(array_intersect_key($filter, self::FILTER_FIELDS_DEFAULT));
			array_map([$view_url, 'setArgument'], array_keys($paging_arguments), $paging_arguments);

			$this->extendData($prepared_data);

			$data = [
				'results' => [
					'filter' => $filter,
					'view_curl' => $view_url,
					'config' => [
					],
					'tags' => makeTags($prepared_data['items'], true, 'itemid', (int) $filter['show_tags'],
						$filter['tags'], [],
						(int) $filter['tag_name_format'], $filter['tag_priority']
					)
				] + $prepared_data,
			];

			$response = new CControllerResponseData($data);
			$this->setResponse($response);
		}
	}
}
