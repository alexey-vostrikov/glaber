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
 */

$html_page = (new CHtmlPage())
	->setTitle(_('Queue details'))
	->setTitleSubmenu([
		'main_section' => [
			'items' => [
				(new CUrl('zabbix.php'))
					->setArgument('action', 'queue.overview')
					->getUrl() => _('Queue overview'),
				(new CUrl('zabbix.php'))
					->setArgument('action', 'queue.overview.proxy')
					->getUrl() => _('Queue overview by proxy'),
				(new CUrl('zabbix.php'))
					->setArgument('action', 'queue.details')
					->getUrl() => _('Queue details')
			]
		]
	])
	->setDocUrl(CDocHelper::getUrl(CDocHelper::QUEUE_DETAILS));
if (isset($data['filter_item_type']) && $data['filter_item_type'] >= 0) 
			$html_page->setTitle(_('Queue details for item for item type '.item_type2str($data['filter_item_type'])));
		
$table = (new CDataTable("details"))->setHeader([
	_('Scheduled check'),
	_('Delayed by'),
	_('Host'),
	_('Interfaces'),
	_('Name'),
	_('Type'),
	_('Proxy')
]);

foreach ($data['queue_data'] as $itemid => $item_queue_data) {
	if (!array_key_exists($itemid, $data['items'])) {
		continue;
	}

	$item = $data['items'][$itemid];
	$host = reset($item['hosts']);
	$hostdata = $data['hosts'][$item['hostid']];
	
	$table->addRow([
		zbx_date2str(DATE_TIME_FORMAT_SECONDS, $item_queue_data['nextcheck']),
		$item_queue_data['nextcheck']> 0 ?zbx_date2age($item_queue_data['nextcheck']):"NOT PLANNED",
		(new CHostLink($host['name'], $host['hostid'])),
		getHostAvailabilityTable( $hostdata['interfaces']),
		(new CItemLink($item['name'], $item['itemid'])),
		isset($item['type'])?item_type2str($item['type']):'',
		array_key_exists($data['hosts'][$item['hostid']]['proxy_hostid'], $data['proxies'])
			? $data['proxies'][$data['hosts'][$item['hostid']]['proxy_hostid']]['host']
			: ''
	]);
}

if (CWebUser::getRefresh()) {
	(new CScriptTag('PageRefresh.init('.(CWebUser::getRefresh() * 1000).');'))
		->setOnDocumentReady()
		->show();
}

$html_page
	->addItem($table)
		->show();
