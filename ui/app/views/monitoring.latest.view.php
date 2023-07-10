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
 * @var CView $this
 * @var array $data
 */

$this->addJsFile('layout.mode.js');
$this->addJsFile('class.tagfilteritem.js');
$this->addJsFile('class.tabfilter.js');
$this->addJsFile('class.tabfilteritem.js');
$this->addJsFile('class.expandable.subfilter.js');

$this->includeJsFile('monitoring.latest.view.js.php');

$this->enableLayoutModes();
$web_layout_mode = $this->getLayoutMode();

if ($data['uncheck']) {
	uncheckTableRows('latest');
}

//show_error_message(json_encode($data));
$html_page = (new CHtmlPage())
	->setTitle(_('Latest data'))
	->setWebLayoutMode($web_layout_mode)
//	->setNavigation(new CHostNav(CHostNav::getData($data['hosts'][0])))
	->setDocUrl(CDocHelper::getUrl(CDocHelper::MONITORING_LATEST_VIEW))
	->setControls(
		(new CTag('nav', true, (new CList())->addItem(get_icon('kioskmode', ['mode' => $web_layout_mode]))))
			->setAttribute('aria-label', _('Content controls'))
	);

//if (1 == count($data['hosts'])) {
//
//	$host = array_values($data['hosts'])[0];
//	$html_page->setNavigation(new CHostNav(CHostNav::getData($host['hostid'])));
//}

if ($web_layout_mode == ZBX_LAYOUT_NORMAL) {
	$filter = (new CTabFilter())
		->setId('monitoring_latest_filter')
		->setOptions($data['tabfilter_options'])
		->addTemplate(new CPartial($data['filter_view'], $data['filter_defaults']));

	foreach ($data['filter_tabs'] as $tab) {
		$tab['tab_view'] = $data['filter_view'];
		$filter->addTemplatedTab($tab['filter_name'], $tab);
	}

	// Set javascript options for tab filter initialization in monitoring.latest.view.js.php file.
	$data['filter_options'] = $filter->options;
	$html_page->addItem($filter);
}
else {
	$data['filter_options'] = null;
}

$html_page
	->addItem(new CPartial('monitoring.latest.view.html', array_intersect_key($data,
		array_flip(['filter', 'view_curl', 'hosts', 'items', 'history', 'config',
			'maintenances', 'items_rw', 'entities', 'tags'
		])
	)))
	->show();

(new CScriptTag('
	view.init('.json_encode([
		'filter_options' => $data['filter_options'],
		'refresh_url' => $data['refresh_url'],
		'refresh_data' => $data['refresh_data'],
		'refresh_interval' => $data['refresh_interval'],
		'checkbox_object' => 'itemids'
	]).');
'))
	->setOnDocumentReady()
	->show();
