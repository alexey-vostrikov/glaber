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

/**
 * @var CPartial $this
 */

if (!isset($data['hosts']) || 0 == count( $data['hosts'])) {
    $div = (new CDiv())
        ->addItem(
            (new CTag("h2", true, _("Please, select at least one host"))));
    echo $div;
    return;
}

if ($data['filter']['group_by_discovery'] && $data['entities']) {
    $page_view = ShowGroupedItems($data);
} else {
    $page_view = ShowItemsPlainTable($data);
}

echo $page_view;

function ShowGroupedItems(array &$data) {
    $all_div = new CDiv();
    $items = &$data['items'];
    
    $discovery_out = (new CDiv());

    foreach ($data['entities'] as $discovery_id => $discovery_item_data) {
            $discovery_out->addItem(buildDiscoveryTable($items, $discovery_id, $discovery_item_data, $data));
            $discovery_out->addItem((new CDiv())->addItem(' '));
    }
    
    $all_div->addItem((new CTag("h4", true, "Host-specific items"))
        ->addStyle("font-weight: bold;"))
        ->addItem( ShowItemsPlainTable($data));
    $all_div->addItem($discovery_out);

    return $all_div->toString();
}


function buildDiscoveryTable(array &$items, $discovery_id, array &$discovery_data, &$data) {
    
    $table = (new CDataTable('problems'.$discovery_id, ['compact'=> '1']))->addClass(ZBX_STYLE_OVERFLOW_ELLIPSIS);
    $prototype_items = $discovery_data['items'];
    $table->setHeader( array_merge( [_('Name')], array_column($prototype_items, 'name')));
    
    if (!isset($discovery_data['entities']))
        return;
    
    foreach($discovery_data['entities'] as $entity_name => $entity_items) {
        $row = [$entity_name];
		
        foreach($prototype_items as $prototype_itemid => $prototype_item) {
            if (isset($entity_items[$prototype_itemid])) {
                $itemid = $entity_items[$prototype_itemid];
                
                $value = new CLatestValue( $items[$itemid] , 
                    isset($data['history'][$itemid])? $data['history'][$itemid] : null ,
                    isset($items[$itemid]['triggers'])? $items[$itemid]['triggers'] : null,
                    isset($data['can_create'])
                );    
                
                $col = new CCol($value);
                
                if ($value->GetWorstSeverity() > 0) 
                    $col->addClass(CSeverityHelper::getStatusStyle($value->GetWorstSeverity()));

                array_push($row, $col);
               
                unset($data['items'][$itemid]);
		
            } else {
				array_push($row,"NO item");
			} 
		}
		$table->addRow($row);
	}
    
    if (isset($data['history'][$discovery_id]) && is_array($data['history'][$discovery_id]) 
             &&  isset($data['history'][$discovery_id][0]) 
        ) 
        $last_discovery_data = $data['history'][$discovery_id][0]['value'];
    else 
        $last_discovery_data = '';

    
    $res =  (new CDiv())->addItem((new CTag("h4", true, $discovery_data['name']))
                            ->addStyle("font-weight: bold;")
                            ->addClass(ZBX_STYLE_LINK_ACTION)
                            ->setHint($last_discovery_data, 'hintbox-wrap') )
                        ->addItem($table);
    
    unset($data['items'][$discovery_id]);
    return $res;
}

function ShowItemsPlainTable(array &$data)
{
	$context_host = 0;

	if (1 == count($data['hosts'])) {
		$context_host = 1;
	}
	
    $form = (new CForm('GET', 'history.php'))
        ->cleanItems()
        ->setName('items')
        ->addItem(new CVar('action', HISTORY_BATCH_GRAPH));

    $table = (new CDataTable('problems', ['advanced_search' => true]))->addClass(ZBX_STYLE_OVERFLOW_ELLIPSIS);

    $col_check_all = new CColHeader(
        (new CCheckBox('all_items'))->onClick("checkAll('" . $form->getName() . "', 'all_items', 'itemids');")
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
                ->addStyle('width: 13%')->addClass('search') : null,
            (new CColHeader(_('Name')))->addStyle('width: 33%')->addClass('search'),
            (new CColHeader(_('Interval')))->addStyle('width: 5%')->addClass('search'),
            (new CColHeader(_('History')))->addStyle('width: 5%'),
            (new CColHeader(_('Trends')))->addStyle('width: 5%'),
            (new CColHeader(_('Type')))->addStyle('width: 8%')->addClass('search'),
            (new CColHeader(_('Last response')))->addStyle('width: 14%')->addClass('search'),
            (new CColHeader(_('Last value')))->addStyle('width: 14%')->addClass('search')->addClass('type-num'),
            (new CColHeader(_x('Change', 'noun')))->addStyle('width: 10%')->addClass('search')->addClass('type-num'),
            (new CColHeader(_('Tags')))->addClass(ZBX_STYLE_COLUMN_TAGS_3)->addClass('search'),
 //           isset($data['can_create'])?(new CColHeader(_('Edit')))->addStyle('width: 50px'): null,
        ]);
    } else {
        $table->setHeader([
            $col_check_all->addStyle('width: 15px')->addClass('no-sort'),
            0 == $context_host ? (new CColHeader(_('Host')))->addStyle('width: 23%')->addClass('search') : null,
            (new CColHeader(_('Name')))->addStyle('width: 40%')->addClass('search'),
            (new CColHeader(_('Last response')))->addStyle('width: 20%')->addClass('search'),
            (new CColHeader(_('Last value')))->addStyle('width: 14%')->addClass('search')->addClass('type-num'),
            (new CColHeader(_x('Change', 'noun')))->addStyle('width: 10%')->addClass('search')->addClass('type-num'),
            (new CColHeader(_('Tags')))->addClass(ZBX_STYLE_COLUMN_TAGS_3)->addClass('search'),
 //           isset($data['can_create'])?(new CColHeader(_('Edit')))->addStyle('width: 50px'): null,
        ]);
    }

    foreach ($data['items'] as $itemid => $item) {
        $is_graph = ($item['value_type'] == ITEM_VALUE_TYPE_FLOAT || $item['value_type'] == ITEM_VALUE_TYPE_UINT64);

        $checkbox = (new CCheckBox('itemids[' . $itemid . ']', $itemid))->addClass('no-sort');
        $state_css = ($item['state'] == ITEM_STATE_NOTSUPPORTED) ? ZBX_STYLE_GREY : null;

        $item_name = (new CDiv([
            (new CSpan($item['name']))->addClass('label'),
            ($item['description'] !== '') ? makeDescriptionIcon($item['description']) : null,
        ]))->addClass('action-container');

        $item_history_url = (new CUrl('history.php'))
            ->setArgument('action', $is_graph ? HISTORY_GRAPH : HISTORY_VALUES)
            ->setArgument('itemids[]', $item['itemid']);

        // Other row data preparation.
        if ($simple_interval_parser->parse($item['history']) == \CParser::PARSE_SUCCESS) {
            $keep_history = timeUnitToSeconds($item['history']);
            $item_history = $item['history'];
        } else {
            $keep_history = 0;
            $item_history = (new CSpan($item['history']))->addClass(ZBX_STYLE_RED);
        }

        if ($item['value_type'] == ITEM_VALUE_TYPE_FLOAT || $item['value_type'] == ITEM_VALUE_TYPE_UINT64) {
           if ($simple_interval_parser->parse($item['trends']) == CParser::PARSE_SUCCESS) {
                $keep_trends = timeUnitToSeconds($item['trends']);
                $item_trends = $item['trends'];
            } else {
                $keep_trends = 0;
                $item_trends = (new CSpan($item['trends']))->addClass(ZBX_STYLE_RED);
            }
        } else {
            $keep_trends = 0;
            $item_trends = '';
        }

        $host = $data['hosts'][$item['hostid']];

        $host_name = (new CLinkAction($host['name']))
            ->addClass($host['status'] == HOST_STATUS_NOT_MONITORED ? ZBX_STYLE_RED : null)
            ->setMenuPopup(CMenuPopupHelper::getHost($item['hostid']));

        $value = new CLatestValue( $item , 
            isset($data['history'][$itemid])? $data['history'][$itemid] : null ,
            isset($item['triggers'])? $item['triggers'] : null,
            isset($data['can_create'])
        );    

        if ($data['filter']['show_details']) {

            $item_key = ($item['type'] == ITEM_TYPE_HTTPTEST)
            ? (new CSpan($item['key_expanded']))->addClass(ZBX_STYLE_GREEN)
            : (new CLink($item['key_expanded'], $item_history_url))
                ->addClass(ZBX_STYLE_LINK_ALT)
                ->addClass(ZBX_STYLE_GREEN);

            if (in_array($item['type'], [ITEM_TYPE_SNMPTRAP, ITEM_TYPE_TRAPPER, ITEM_TYPE_DEPENDENT])
                || ($item['type'] == ITEM_TYPE_ZABBIX_ACTIVE && strncmp($item['key_expanded'], 'mqtt.get', 8) === 0)) {
                $item_delay = '';
            } elseif ($update_interval_parser->parse($item['delay']) == CParser::PARSE_SUCCESS) {
                $item_delay = $update_interval_parser->getDelay();

                if ($item_delay[0] === '{') {
                    $item_delay = (new CSpan($item_delay))->addClass(ZBX_STYLE_RED);
                }
            } else {
                $item_delay = (new CSpan($item['delay']))->addClass(ZBX_STYLE_RED);
            }

           
                
        //    if ($value->GetWorstSeverity() > 0) 
          //          $col->addClass(CSeverityHelper::getStatusStyle($value->GetWorstSeverity()));


            $table_row = new CRow([
                $checkbox,
                0 == $context_host ? $host_name : null,
                (new CCol([$item_name, $item_key]))->addClass($state_css),
                (new CCol($item_delay))->addClass($state_css),
                (new CCol($keep_history > 0 ?
                    (new CSpan(""))
                        ->setAttribute("data-indicator", "mark")
                        ->setAttribute("data-indicator-value", 1)
                    : null
                ))->addClass($state_css),
                (new CCol(
                    $keep_trends > 0 ?
                    (new CSpan(""))
                        ->setAttribute("data-indicator", "mark")
                        ->setAttribute("data-indicator-value", 1)
                    : null

                ))->addClass($state_css),
                (new CCol(item_type2str($item['type'])))->addClass($state_css),
                (new CCol($value->GetLastCheck()))->setAttribute('data-order', $value->GetLastCheckRaw())->addClass($state_css),
                (new CCol())
                    ->setAttribute('data-order', $value->GetLastCheckRaw())
                    ->addClass(CSeverityHelper::getStatusStyle($value->GetWorstSeverity()))
                    ->addItem(new CDiv($value)),

                (new CCol($value->GetValueChangeFormatted()))
                    ->setAttribute('data-order', $value->GetValueChangeRaw())
                    ->addClass($state_css),
                $data['tags'][$itemid],
             ]);
        } else {
            $table_row = new CRow([
                $checkbox,
                0 == $context_host ? $host_name : null,
                (new CCol(
                    (new CLink($item_name, $item_history_url))
                        ->addClass(ZBX_STYLE_LINK_ALT)))
                    ->addClass($state_css),
                (new CCol($last_check))->setAttribute('data-order', $last_check_raw)->addClass($state_css),
                (new CCol($value))
                    ->setAttribute('data-order', $last_value_raw)
                    ->addClass(CSeverityHelper::getStatusStyle($value->GetWorstSeverity())),
                (new CCol($value->GetValueChangeFormatted()))
                    ->setAttribute('data-order', $value->GetValueChangeRaw())->addClass($state_css),
                $data['tags'][$itemid],
         //           isset($data['can_create'])? (new CCol())->addItem(new CLink(_('Edit'), $item_config_url)): null,
            ]);
        }

        $table->addRow($table_row);
    }

    $form->addItem([
        $table,
        null,
        new CActionButtonList('graphtype', 'itemids', [
            GRAPH_TYPE_STACKED => ['name' => _('Display stacked graph')],
            GRAPH_TYPE_NORMAL => ['name' => _('Display graph')],
        ]),
    ]);
    return $form;
}