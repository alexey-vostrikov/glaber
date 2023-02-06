<?php declare(strict_types = 0);
/*
** Copyright (C) 2001-2023 Glaber
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

class CLatestValue extends CSpan {
  
    public function __construct(array $item, $history = []) {
//        $this->fetchMissingData($item, $history);
        $formatted_value = $this->makeFormattedValue($item, $history);
        parent::__construct($formatted_value);
        $this->setHint( $this->makeHint($item, $history, $formatted_value), 'hintbox-wrap'); 
        $this->addClass(ZBX_STYLE_LINK_ACTION);
    }

    private function makeFormattedValue(&$item, &$history) {

        if (!isset($history) || !is_array($history) || 0 == count($history) ) 
            return " - ";
        
        $last_history = $history[0]['value'];

        if ( (ITEM_VALUE_TYPE_TEXT == $item['value_type'] ||  ITEM_VALUE_TYPE_LOG == $item['value_type'] ) && 
                mb_strlen($last_history) > 20 ) {
               return substr($last_value, 0, 20). '...';
        }     
        
        if (isset( $item['error']) && $item['state'] == ITEM_STATE_NOTSUPPORTED) 
            return  'UNSUPPORTED';
            
        return formatHistoryValue($last_history, $item, false);
    }

    private function makeHint(&$item, &$history, $formatted_value) {
        $svg = "";

        //generate svg graph preview for digital items and full text view for text ones
        if (ITEM_VALUE_TYPE_UINT64 == $item['value_type'] || ITEM_VALUE_TYPE_FLOAT == $item['value_type']) {
            $svg = $this->generateSvgGraph($item, $history);
        } 
        $name_div = (new CDiv($item['name']))->addClass(ZBX_STYLE_NOWRAP);
        
        $value_graph_div = (new CDiv())
                //->addItem((new CDiv($formatted_value))
                //    ->addStyle('position: absolute; width: 300px; height: 100%; text-align: center; vertical-align: middle; font-size: xxx-large;padding-top: 30px;'))
                ->addItem($svg);
        
        $hint = (new CDiv())
                    ->addItem($name_div)
                    ->addItem($value_graph_div)
                    ->addItem($this->makeHistoryLinks($item))
                    ->addItem($this->makeTriggerInfo($item, null))
                    ->addClass(ZBX_STYLE_HINTBOX_WRAP); //,'', true, '', 0);
                  ;  
        return $hint;
    }

    private function makeHistoryLinks(array $item) {
        $ranges=[  ['name' => _('Last hour'), 'range' => 'now-1h'],
                  ['name' => _('Last day'), 'range' => 'now-1d'],
                  ['name' => _('Last week'), 'range' => 'now-7d'],
        ];

        $is_graph = ($item['value_type'] == ITEM_VALUE_TYPE_FLOAT || $item['value_type'] == ITEM_VALUE_TYPE_UINT64);
       
        $list = (new CList());

        foreach ($ranges as $range) {
            $list->addItem(
                 new CLink($range['name'], (new CUrl('history.php'))
                        ->setArgument('action', $is_graph ? HISTORY_GRAPH : HISTORY_VALUES)
                        ->setArgument('from', $range['range'])
                        ->setArgument('to', 'now')
                        ->setArgument('itemids[]', $item['itemid'])
            ));
        }

        return $list;
    }

    private function makeTriggerInfo(array $item, $triggers) {
        return new CDiv("Trigger info");
    }

    private function generateSvgGraph(&$item,&$history) {
         
        if (isset($history) && count($history) > 0) 
            return  (new CDiv(new CSVGSmallGraph($history, 50, 200)))
                        ->addClass(GLB_STYLE_GRAPH_PREVIEW);

        return '';
    }

    private function fetchMissingData($itemid, &$item, &$history) {
        
    }
}