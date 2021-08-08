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


/**
 * @var CPartial $this
 */

$form = (new CForm('GET', 'history.php'))
	->cleanItems()
	->setName('items')
	->addItem(new CVar('action', HISTORY_BATCH_GRAPH));

$table = (new CTableInfo())->addClass(ZBX_STYLE_OVERFLOW_ELLIPSIS);

// Latest data header.
$col_check_all = new CColHeader(
	(new CCheckBox('all_items'))->onClick("checkAll('".$form->getName()."', 'all_items', 'itemids');")
);

$view_url = $data['view_curl']->getUrl();

$col_host = make_sorting_header(_('Host'), 'host', $data['sort_field'], $data['sort_order'], $view_url);
$col_name = make_sorting_header(_('Name'), 'name', $data['sort_field'], $data['sort_order'], $view_url);

$simple_interval_parser = new CSimpleIntervalParser();
$update_interval_parser = new CUpdateIntervalParser(['usermacros' => true]);

if ($data['filter']['show_details']) {
	$table->setHeader([
		$col_check_all->addStyle('width: 15px;'),
		$col_host->addStyle('width: 13%'),
		$col_name->addStyle('width: 21%'),
		(new CColHeader(_('Interval')))->addStyle('width: 5%'),
		(new CColHeader(_('History')))->addStyle('width: 5%'),
		(new CColHeader(_('Trends')))->addStyle('width: 5%'),
		(new CColHeader(_('Type')))->addStyle('width: 8%'),
		(new CColHeader(_('Last check')))->addStyle('width: 14%'),
		(new CColHeader(_('Last value')))->addStyle('width: 14%'),
		(new CColHeader(_x('Change', 'noun')))->addStyle('width: 10%'),
		(new CColHeader(_('Tags')))->addClass(ZBX_STYLE_COLUMN_TAGS_3),
		(new CColHeader())->addStyle('width: 6%'),
		(new CColHeader(_('Info')))->addStyle('width: 35px')
	]);
}
else {
	$table->setHeader([
		$col_check_all->addStyle('width: 15px'),
		$col_host->addStyle('width: 17%'),
		$col_name->addStyle('width: 40%'),
		(new CColHeader(_('Last check')))->addStyle('width: 14%'),
		(new CColHeader(_('Last value')))->addStyle('width: 14%'),
		(new CColHeader(_x('Change', 'noun')))->addStyle('width: 10%'),
		(new CColHeader(_('Tags')))->addClass(ZBX_STYLE_COLUMN_TAGS_3),
		(new CColHeader())->addStyle('width: 6%')
	]);
}

//Latest data rows.
foreach ($data['items'] as $itemid => $item) {
	$is_graph = ($item['value_type'] == ITEM_VALUE_TYPE_FLOAT || $item['value_type'] == ITEM_VALUE_TYPE_UINT64);

	$checkbox = (new CCheckBox('itemids['.$itemid.']', $itemid))->setEnabled($is_graph);
	$state_css = ($item['state'] == ITEM_STATE_NOTSUPPORTED) ? ZBX_STYLE_GREY : null;

	$item_name = (new CDiv([
		(new CSpan($item['name_expanded']))->addClass('label'),
		($item['description'] !== '') ? makeDescriptionIcon($item['description']) : null
	]))->addClass('action-container');

	$history = $data['history'];
	//show_error_message("Last history:'".print_r($history,true)."'");

	$lastHistory = isset($history[$item['itemid']][0]) ? $history[$item['itemid']][0] : null;
	$prevHistory = [];//isset($history[$item['itemid']][1]) ? $history[$item['itemid']][1] : null;

	if (isset($lastHistory['prevvalue']) && isset($lastHistory['prevclock']) && $lastHistory['prevclock'] > 0 ) {
		$prevHistory['value'] = $lastHistory['prevvalue'];
		$prevHistory['clock'] = $lastHistory['prevclock'];
		//show_error_message("Last history:'".print_r($prevHistory,true)."'");
	}
	
	//show_error_message("Prev history:'".print_r($history,true)."'");
	
	if ($lastHistory) {
			$last_check = zbx_date2str(DATE_TIME_FORMAT_SECONDS, $lastHistory['clock']);
		
		if ( isset($lastHistory['nextcheck']) && $lastHistory['nextcheck'] > 0 )
				$last_check = (new CSpan($last_check))
						->addClass(ZBX_STYLE_LINK_ACTION)
						->setHint("Next: ". zbx_date2str(DATE_TIME_FORMAT_SECONDS,$lastHistory['nextcheck']), 'hintbox-wrap');

		if ($lastHistory['clock'] > 0 ) {
			if (isset($lastHistory['error']) &&  strlen($lastHistory['error'])>0 ) {
				$last_value = (new CSpan('UNSUPPORTED'))
					->addClass(ZBX_STYLE_RED)
					->addClass(ZBX_STYLE_LINK_ACTION)
					->setHint($lastHistory['error'], 'hintbox-wrap');
			} else {
				$last_value = formatHistoryValue($lastHistory['value'], $item, false);
				if ( (ITEM_VALUE_TYPE_TEXT == $item['value_type'] || 
					 ITEM_VALUE_TYPE_LOG == $item['value_type'] ) && 
					 mb_strlen($last_value) > 20 ) {
						$last_value = (new CSpan($last_value))
							->addClass(ZBX_STYLE_LINK_ACTION)
							->setHint($lastHistory['value'], 'hintbox-wrap');
					 }

			}
			
		} else $last_value='';

		$change = '';

		if ( $lastHistory['clock'] > 0 &&
			(!isset($lastHistory['error']) || strlen($lastHistory['error']) <1 ) && 
		    isset($prevHistory) && 
			in_array($item['value_type'], [ITEM_VALUE_TYPE_FLOAT, ITEM_VALUE_TYPE_UINT64]) ) {
			$history_diff = $lastHistory['value'] - $prevHistory['value'];

			if ($history_diff != 0) {
				if ($history_diff > 0) {
					$change = '+';
				}

				// The change must be calculated as uptime for the 'unixtime'.
				$change .= convertUnits([
					'value' => $history_diff,
					'units' => ($item['units'] === 'unixtime') ? 'uptime' : $item['units']
				]);
			}
		}
	}
	else {
		$last_check = '';
		$last_value = '';
		$change = '';
	}

	// Other row data preparation.
	if ($data['config']['hk_history_global']) {
		$keep_history = timeUnitToSeconds($data['config']['hk_history']);
		$item_history = $data['config']['hk_history'];
	}
	elseif ($simple_interval_parser->parse($item['history']) == CParser::PARSE_SUCCESS) {
		$keep_history = timeUnitToSeconds($item['history']);
		$item_history = $item['history'];
	}
	else {
		$keep_history = 0;
		$item_history = (new CSpan($item['history']))->addClass(ZBX_STYLE_RED);
	}

	if ($item['value_type'] == ITEM_VALUE_TYPE_FLOAT || $item['value_type'] == ITEM_VALUE_TYPE_UINT64) {
		if ($data['config']['hk_trends_global']) {
			$keep_trends = timeUnitToSeconds($data['config']['hk_trends']);
			$item_trends = $data['config']['hk_trends'];
		}
		elseif ($simple_interval_parser->parse($item['trends']) == CParser::PARSE_SUCCESS) {
			$keep_trends = timeUnitToSeconds($item['trends']);
			$item_trends = $item['trends'];
		}
		else {
			$keep_trends = 0;
			$item_trends = (new CSpan($item['trends']))->addClass(ZBX_STYLE_RED);
		}
	}
	else {
		$keep_trends = 0;
		$item_trends = '';
	}

	if ($keep_history != 0 || $keep_trends != 0) {
		$actions = new CLink($is_graph ? _('Graph') : _('History'), (new CUrl('history.php'))
			->setArgument('action', $is_graph ? HISTORY_GRAPH : HISTORY_VALUES)
			->setArgument('itemids[]', $item['itemid'])
		);
	}
	else {
		$actions = '';
	}

	$host = $data['hosts'][$item['hostid']];
	$host_name = (new CLinkAction($host['name']))
		->addClass($host['status'] == HOST_STATUS_NOT_MONITORED ? ZBX_STYLE_RED : null)
		->setMenuPopup(CMenuPopupHelper::getHost($item['hostid']));

	if ($data['filter']['show_details']) {

		$item_config_url = (new CUrl('items.php'))
			->setArgument('form', 'update')
			->setArgument('itemid', $itemid)
			->setArgument('context', 'host');

		$item_key = ($item['type'] == ITEM_TYPE_HTTPTEST)
			? (new CSpan($item['key_expanded']))->addClass(ZBX_STYLE_GREEN)
			: (new CLink($item['key_expanded'], $item_config_url))
				->addClass(ZBX_STYLE_LINK_ALT)
				->addClass(ZBX_STYLE_GREEN);

		if (in_array($item['type'], [ITEM_TYPE_SNMPTRAP, ITEM_TYPE_TRAPPER, ITEM_TYPE_DEPENDENT])
				|| ($item['type'] == ITEM_TYPE_ZABBIX_ACTIVE && strncmp($item['key_expanded'], 'mqtt.get', 8) === 0)) {
			$item_delay = '';
		}
		elseif ($update_interval_parser->parse($item['delay']) == CParser::PARSE_SUCCESS) {
			$item_delay = $update_interval_parser->getDelay();

			if ($item_delay[0] === '{') {
				$item_delay = (new CSpan($item_delay))->addClass(ZBX_STYLE_RED);
			}
		}
		else {
			$item_delay = (new CSpan($item['delay']))->addClass(ZBX_STYLE_RED);
		}

		$item_icons = [];
		if ($item['status'] == ITEM_STATUS_ACTIVE && $item['error'] !== '') {
			$item_icons[] = makeErrorIcon($item['error']);
		}

		$table_row = new CRow([
			$checkbox,
			$host_name,
			(new CCol([$item_name, $item_key]))->addClass($state_css),
			(new CCol($item_delay))->addClass($state_css),
			(new CCol($item_history))->addClass($state_css),
			(new CCol($item_trends))->addClass($state_css),
			(new CCol(item_type2str($item['type'])))->addClass($state_css),
			(new CCol($last_check))->addClass($state_css),
			(new CCol($last_value))->addClass($state_css),
			(new CCol($change))->addClass($state_css),
			$data['tags'][$itemid],
			$actions,
			makeInformationList($item_icons)
		]);
	}
	else {
		$table_row = new CRow([
			$checkbox,
			$host_name,
			(new CCol($item_name))->addClass($state_css),
			(new CCol($last_check))->addClass($state_css),
			(new CCol($last_value))->addClass($state_css),
			(new CCol($change))->addClass($state_css),
			$data['tags'][$itemid],
			$actions
		]);
	}

	$table->addRow($table_row);
}

$form->addItem([
	$table,
	$data['paging'],
	new CActionButtonList('graphtype', 'itemids', [
		GRAPH_TYPE_STACKED => ['name' => _('Display stacked graph')],
		GRAPH_TYPE_NORMAL => ['name' => _('Display graph')]
	])
]);

echo $form;
