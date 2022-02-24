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
$context_host = 0;
 if (1 == count( $data['hosts']) ) {
	$context_host = 1;
//	error_log("Host context: ".print_r($data['hosts'],true)); 
} 

$form = (new CForm('GET', 'history.php'))
	->cleanItems()
	->setName('items')
	->addItem(new CVar('action', HISTORY_BATCH_GRAPH));

$table = (new CDataTable('latest',['advanced_search' => true]))->addClass(ZBX_STYLE_OVERFLOW_ELLIPSIS);

// Latest data header.
$col_check_all = new CColHeader(
	(new CCheckBox('all_items'))->onClick("checkAll('".$form->getName()."', 'all_items', 'itemids');")
);

$view_url = $data['view_curl']->getUrl();

$simple_interval_parser = new CSimpleIntervalParser();
$update_interval_parser = new CUpdateIntervalParser(['usermacros' => true]);

if ($data['filter']['show_details']) {
	$table->setHeader([
		$col_check_all
			->addStyle('width: 15px;')
			->addClass('no-sort'),
		0 == $context_host ? (new CColHeader(_('Host')))
			->addStyle('width: 13%')->addClass('search') : NULL,
		(new CColHeader(_('Name')))->addStyle('width: 21%')->addClass('search'),
		(new CColHeader(_('Interval')))->addStyle('width: 5%')->addClass('search'),
		(new CColHeader(_('History')))->addStyle('width: 5%'),
		(new CColHeader(_('Trends')))->addStyle('width: 5%'),
		(new CColHeader(_('Type')))->addStyle('width: 8%')->addClass('search'),
		(new CColHeader(_('Last response')))->addStyle('width: 14%')->addClass('search'),
		(new CColHeader(_('Last value')))->addStyle('width: 14%')->addClass('search')->addClass('type-num'),
		(new CColHeader(_x('Change', 'noun')))->addStyle('width: 10%')->addClass('search')->addClass('type-num'),
		(new CColHeader(_('Tags')))->addClass(ZBX_STYLE_COLUMN_TAGS_3)->addClass('search'),
		(new CColHeader())->addStyle('width: 100px'),
	]);
}
else {
	$table->setHeader([
		$col_check_all->addStyle('width: 15px')->addClass('no-sort'),
		0 == $context_host ? (new CColHeader(_('Host')))->addStyle('width: 13%')->addClass('search') : NULL,
		(new CColHeader(_('Name')))->addStyle('width: 40%')->addClass('search'),
		(new CColHeader(_('Last response')))->addStyle('width: 20%')->addClass('search'),
		(new CColHeader(_('Last value')))->addStyle('width: 14%')->addClass('search')->addClass('type-num'),
		(new CColHeader(_x('Change', 'noun')))->addStyle('width: 10%')->addClass('search')->addClass('type-num'),
		(new CColHeader(_('Tags')))->addClass(ZBX_STYLE_COLUMN_TAGS_3)->addClass('search'),
		(new CColHeader())->addStyle('width: 100px')
	]);
}

//Latest data rows.
foreach ($data['items'] as $itemid => $item) {
	$change_raw = '';
	$is_graph = ($item['value_type'] == ITEM_VALUE_TYPE_FLOAT || $item['value_type'] == ITEM_VALUE_TYPE_UINT64);

	$checkbox = (new CCheckBox('itemids['.$itemid.']', $itemid))->addClass('no-sort');//->setEnabled($is_graph);
	$state_css = ($item['state'] == ITEM_STATE_NOTSUPPORTED) ? ZBX_STYLE_GREY : null;

	$item_name = (new CDiv([
		(new CSpan($item['name_expanded']))->addClass('label'),
		($item['description'] !== '') ? makeDescriptionIcon($item['description']) : null
	]))->addClass('action-container');
	
	$item_config_url = (new CUrl('items.php'))
		->setArgument('form', 'update')
		->setArgument('itemid', $itemid)
		->setArgument('context', 'host');
	
	$item_hist = [];

	$last_history = array_key_exists($itemid, $data['history'])
		? ((count($data['history'][$itemid]) > 0) ? $data['history'][$itemid][0] : null)
		: null;

	
	if ($last_history) {
			$item_hist = $data['history'][$itemid];
			$prev_history = (count($data['history'][$itemid]) > 1) ? $data['history'][$itemid][1] : null;
			$last_check = zbx_date2age($last_history['clock']);
			$last_check_raw = $last_history['clock'];
			
			$last_value = formatHistoryValue($last_history['value'], $item, false);
			if ( (ITEM_VALUE_TYPE_TEXT == $item['value_type'] || 
					 ITEM_VALUE_TYPE_LOG == $item['value_type'] ) && 
					 mb_strlen($last_value) > 20 ) {
						$last_value = (new CSpan($last_value))
							->addClass(ZBX_STYLE_LINK_ACTION)
							->setHint($last_history['value'], 'hintbox-wrap');
					 }
			$change = '';
			$last_value_raw = $last_history['value'];
	
			if ($prev_history && in_array($item['value_type'], [ITEM_VALUE_TYPE_FLOAT, ITEM_VALUE_TYPE_UINT64])) {
				$history_diff = $last_history['value'] - $prev_history['value'];
	
				if ($history_diff != 0) {
					if ($history_diff > 0) {
						$change = '+';
					}
					$change_raw = $history_diff;
					// The change must be calculated as uptime for the 'unixtime'.
					$change .= convertUnits([
						'value' => $history_diff,
						'units' => ($item['units'] === 'unixtime') ? 'uptime' : $item['units']
					]);
				}
			}
	} else {
		$last_check = '';
		$last_value = '';
		$change = '';

		$last_check_raw = '';
		$last_value_raw = '';
	}

		
	if (isset($item['error']) && $item['state'] == ITEM_STATE_NOTSUPPORTED ) {
			if (strlen($last_value) < 1) 
				$last_value = 'UNSUPPORTED';	
			
			if ($item['lastdata'] > 0 ) {
				$last_check = zbx_date2age($item['lastdata']);
				$last_check_raw = $item['lastdata'];
			}
			else {
				$last_check = '<UNKNOWN>';
				$last_check_raw = '<UNKNOWN>';
			}

			$last_check = (new CSpan($last_check))
					->addClass(ZBX_STYLE_RED)
					->addClass(ZBX_STYLE_LINK_ACTION)
					->setHint($item['error'], 'hintbox-wrap');
			
			$last_value = (new CSpan($item['error']))
					->addClass(ZBX_STYLE_RED)
					->addClass(ZBX_STYLE_LINK_ACTION)
					->setHint($item['error'], 'hintbox-wrap');

			$last_value_raw = $item['error'];
			$change = '';
			$change_raw = '';
	} 	
	if ( isset($item['nextcheck']) && $item['nextcheck'] > 0 )
		$last_check = (new CSpan($last_check))
				->addClass(ZBX_STYLE_LINK_ACTION)
				->setHint("Next request in: ". zbx_date2age( 2 * time() - $item['nextcheck'] ), 'hintbox-wrap');


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

	$host = $data['hosts'][$item['hostid']];
	$host_name = (new CLinkAction($host['name']))
		->addClass($host['status'] == HOST_STATUS_NOT_MONITORED ? ZBX_STYLE_RED : null)
		->setMenuPopup(CMenuPopupHelper::getHost($item['hostid']));


		
	if ($data['filter']['show_details']) {

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


		$table_row = new CRow([
			$checkbox,
			0 == $context_host ? $host_name : NULL,
			(new CCol([$item_name, $item_key]))->addClass($state_css),
			(new CCol($item_delay))->addClass($state_css),
			(new CCol( $keep_history > 0 ?
				 (new CSpan(""))
					->setAttribute("data-indicator","mark")
					->setAttribute("data-indicator-value",1)
				: null
				))->addClass($state_css),
			(new CCol(
				$keep_trends > 0 ?
				(new CSpan(""))
				   ->setAttribute("data-indicator","mark")
				   ->setAttribute("data-indicator-value",1)
			   : null
			   
			))->addClass($state_css),
			(new CCol(item_type2str($item['type'])))->addClass($state_css),
			(new CCol($last_check))->setAttribute('data-order', $last_check_raw)->addClass($state_css),
			(new CCol())
				->setAttribute('data-order', $last_value_raw)
				->addClass($state_css)
				->addItem(new CDiv($last_value)),

			(new CCol($change))
				->setAttribute('data-order', $change_raw)
				->addClass($state_css),
			$data['tags'][$itemid],
			(new CCol())
				->addClass(ZBX_STYLE_CENTER)
				->addItem( new CLink(($is_graph && count($item_hist) > 0) ? (new CSVGSmallGraph($item_hist,50,200)) : _('History'),
					(new CUrl('history.php'))
			  			->setArgument('action', $is_graph ? HISTORY_GRAPH : HISTORY_VALUES)
			  			->setArgument('itemids[]', $item['itemid'])))
		]);
	}
	else {
		$table_row = new CRow([
			$checkbox,
			0 == $context_host ? $host_name : NULL,
			(new CCol(
				(new CLink($item_name, $item_config_url))
					->addClass(ZBX_STYLE_LINK_ALT)))
					->addClass($state_css),
			(new CCol($last_check))->setAttribute('data-order', $last_check_raw)->addClass($state_css),
			(new CCol($last_value))->setAttribute('data-order', $last_value_raw)->addClass($state_css),
			(new CCol($change))->setAttribute('data-order', $change_raw)->addClass($state_css),
			$data['tags'][$itemid],
			(new CCol())
				->addClass(ZBX_STYLE_CENTER)
				->addItem( new CLink(($is_graph && count($item_hist) > 0) ? (new CSVGSmallGraph($item_hist,50,200)) : _('History'),
					(new CUrl('history.php'))
						->setArgument('action', $is_graph ? HISTORY_GRAPH : HISTORY_VALUES)
			  			->setArgument('itemids[]', $item['itemid'])) )
			
			
		]);
	}

	$table->addRow($table_row);
}

$form->addItem([
	$table,
	NULL,
	new CActionButtonList('graphtype', 'itemids', [
		GRAPH_TYPE_STACKED => ['name' => _('Display stacked graph')],
		GRAPH_TYPE_NORMAL => ['name' => _('Display graph')]
	])
]);

echo $form;
