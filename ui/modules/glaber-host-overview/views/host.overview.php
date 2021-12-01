<?php declare(strict_types = 1);
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
 * @var CView $this
 */

//$this->addCssFile('assets/styles/'.CHtml::encode($default_theme).'.css');
$this->includeJsFile('tables.js', $data);
$this->includeJsFile('jquery.dataTables.min.js',$data);
$this->includeJsFile('jquery.dataTables.min.css',$data);
//$this->addJsFile((new CUrl('js/jquery.dataTables.min.js'))->getUrl());

$widget = new CWidget();
$widget->setTitle(_('Host overview: '.$data['hostinfo'][0]['host']));

$problems_counts_div = (new CDiv())->addClass(ZBX_STYLE_PROBLEM_ICON_LIST);
$total_problem_count = 0;

krsort($data['problems_counts'],0);

foreach ($data['problems_counts'] as $severity => $count) {
	$total_problem_count += $count;
	if ( $count > 0 ) {			
		$problems_counts_div->addItem((new CSpan($count))
				->addClass(ZBX_STYLE_PROBLEM_ICON_LIST_ITEM)
				->addClass(getSeverityStatusStyle($severity))
				->setAttribute('title', getSeverityName($severity))
				);
	}
}

if ( 0 == $total_problem_count ) {
	$problems_counts_div = (new CDiv('No problems found'));
}

$widget->addItem((new CSpan('Problems:'))
					->addItem('Total count: '.$total_problem_count)
					->addItem($problems_counts_div));


if (isset($data['items']) && count($data['items']) > 0) {
	$specific_table = (new CTable())
		->addClass('values-table row-border')
		->setHeader(array_merge( [_('Item name'), _('value')]));

	foreach ($data['items'] as $key => $item ) {
		$specific_table->addRow( [$item['name'], show_compact_latest_data($data,$item) ]);	
	}

	$widget->addItem((new CDiv())
				->addClass("values-table-block")
			->addItem(((new CDiv ("Host-specific items"))
			->addClass('block-header')))
			->addItem($specific_table));
}

foreach ($data['entities'] as $entity) {
	if (isset($entity['entities']) && is_array($entity['entities'])) {
		$entity_tabble = (new CTable())
				->addClass('values-table row-border')
				->setHeader( array_merge( [_('Name')],array_column($entity['items'], 'name')));

		foreach ($entity['entities'] as $entity_name => $entity_items) {
			$row = [$entity_name];
		
			foreach($entity['items'] as $prototype_item) {
				if (isset($entity_items[$prototype_item['itemid']])) {
					array_push($row,show_compact_latest_data($data, $entity_items[$prototype_item['itemid']]));
				} else {
					array_push($row,"NO item");
				}
			}
			$entity_tabble->addRow($row);
		}
		
		$widget->addItem((new CDiv())
				->addClass('values-table-block')
				->addItem(((new CDiv ( (new CTag('h3', true, $entity['name']))->addClass("block-title") ))
					->addClass('block-header')))
				->addItem($entity_tabble));
	} 
}

foreach ($data['templates'] as $template) {
	
	if (isset($template) && isset($template['items']) && count ($template['items'])>0 ) {
	
		$template_table = (new CTable())
				->addClass('values-table display row-border')
				->setHeader( array_merge( [_('Item name'), _('value')]));
	
		foreach ($template['items'] as $key => $item ) {
				$row = [$item['name'], show_compact_latest_data($data,$item)];	
				$template_table->addRow($row);
		}

		$widget->addItem((new CDiv())
					->addClass("values-table-block")
				->addItem(((new CDiv ( $template['name']))
					->addClass('block-header')))
				->addItem($template_table));
	}
}


$widget
//	->addItem(new CPre(print_r($data,true)))
	->show();


function show_compact_latest_data(array &$data, &$item) {

//	$contents = new CSpan();

	if (!isset($item) || !isset($item['itemid']) )
		return '';

	$itemid = $item['itemid'];
	
	$last_history = array_key_exists($itemid, $data['latestdata'])
		? ((count($data['latestdata'][$itemid]) > 0) ? $data['latestdata'][$itemid][0] : null)
		: null;

//	$is_graph = ($item['value_type'] == ITEM_VALUE_TYPE_FLOAT || $item['value_type'] == ITEM_VALUE_TYPE_UINT64);	

	if ($last_history) {
		$last_value = formatHistoryValue($last_history['value'], $item, false);
		if ( (ITEM_VALUE_TYPE_TEXT == $item['value_type'] ||  ITEM_VALUE_TYPE_LOG == $item['value_type'] ) && 
			mb_strlen($last_value) > 20 ) {
				$last_value = (new CSpan($last_value))
						->addClass(ZBX_STYLE_LINK_ACTION)
						->setHint($last_history['value'], 'hintbox-wrap');
	 	}
	
//		return $last_value;
	
	} else {
		$last_value = 'no data';
	}

	$contents = new CCol();
	
	if ( isset($item['triggers']) && count($item['triggers']) > 0 ) {
		$trigger = $item['triggers'][0];
	
		if ( $trigger['status'] == TRIGGER_STATUS_ENABLED) {
			//if ($trigger['state'] == TRIGGER_VALUE_TRUE )
			if (isset($item['problems'])) {
				$severity= 0;
				$hint = '';
				
				foreach ($item['problems'] as $problem_id) {
					$severity = max( $data['all_problems'][$problem_id]['severity'], $severity);
					$hint.=$data['all_problems'][$problem_id]['name']."  ";
				}
				
				$contents->addClass(getSeverityStyle($severity,1));

				$last_value = (new CSpan($last_value))
						->addClass(ZBX_STYLE_LINK_ACTION) 
						->setHint($hint, 'hintbox-wrap');

			} else {
				$contents->addClass(getSeverityStyle($trigger['priority'],0));
			}
		//	else 
		//	$contents->addClass(getSeverityStyle($trigger['priority']));
				//->addClass(ZBX_STYLE_LINK_ACTION)
				//->setHint("Hello world", 'hintbox-wrap');
				//->setHint($trigger['triggerid'].$trigger['description'], 'hintbox-wrap');
//			$contents->addItem((new CSpan($last_value))
//					->addClass(ZBX_STYLE_LINK_ACTION) 
//					->setHint($trigger['triggerid'].$trigger['description'], 'hintbox-wrap')
			//);
		}// else {
			$contents->addItem($last_value);
		//}
		//addTriggerValueStyle($contents, $trigger['state'], 0, 0);
	} else {
		$contents->addItem($last_value);
	}

	//	if ($is_graph && count($item_hist)>0 ) 
//		return (new CSVGSmallGraph($item_hist,50,200));

	return $contents;
}

	