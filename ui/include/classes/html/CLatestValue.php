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
    private $itemdata;
    private $history;
    private $triggers;
    
    private $value_formatted = " - ";
    private $value_change;
    private $value_change_raw = 0;
    
    private $last_poll_time;
    private $last_poll_time_raw;

    private $next_poll_time;

    private $worst_severity = -1;

    private $hintbox;

    public function __construct(array $itemdata, $history = [], $triggers = [], $editable = false) {
        $this->itemdata = $itemdata;
        $this->history = $history;
        $this->triggers = $triggers;
        $this->editable = $editable;

        $this->fetchMissingData();
        $this->makeValueFormatted();
        
        $this->makeValueChange();
        $this->calcTimestamps();

       // error_log("Got trigger info:".json_encode($triggerinfo)."\n");

        parent::__construct($this->value_formatted);
        
        if (!$this->isSupportedItem())
            $this->addClass(ZBX_STYLE_RED);
        
        $this->makeHint();

        $this->setHint($this->hintbox, 'hintbox-wrap'); 
        $this->addClass(ZBX_STYLE_LINK_ACTION);
    }

    public function GetWorstSeverity() {
        return $this->worst_severity;
    }
    public function GetValueChangeFormatted() {
        return $this->value_change;
    }
    public function GetValueChangeRaw() {
        return $this->value_change_raw;
    }
    public function GetLastCheck() {
        return $this->last_poll_time;
    }
    public function GetLastCheckRaw() {
        return $this->last_poll_time_raw;
    }


    private function calcTimestamps() {
        if (isset($this->itemdata['nextcheck']) && $this->itemdata['nextcheck'] > 0) {
            $this->next_poll_time = zbx_date2age(2 * time() - $this->itemdata['nextcheck']);
        }

        if (isset($this->itemdata['lastdata']) && $this->itemdata['lastdata'] > 0) {
            $this->last_poll_time_raw = $this->itemdata['lastdata'];
            $this->last_poll_time = zbx_date2age($this->last_poll_time_raw);
        } else if ( isset($this->history) && count($this->history) > 0 ) {
            $this->last_poll_time_raw = $this->history[0]['clock'];
            $this->last_poll_time = zbx_date2age($this->last_poll_time_raw);
        } 
    }

    private function isNumericItem() {
    
        if ( (ITEM_VALUE_TYPE_UINT64 == $this->itemdata['value_type'] ||
              ITEM_VALUE_TYPE_FLOAT == $this->itemdata['value_type'] ))
            return true;
        return false;
    }
    
    private function makeValueChange() {
    
        if (!$this->isNumericItem() || !isset($this->history) || count($this->history) < 2 
                            || $this->itemdata['state'] == ITEM_STATE_NOTSUPPORTED) 
            return;

        $this->value_change_raw = $this->history[0]['value'] - $this->history[1]['value'];
        
        if (0 == $this->value_change_raw)
            return;

        $this->value_change_raw > 0 ? $sign = '+' : $sign = '';
        $this->value_change = $sign. convertUnits(['value' => $this->value_change_raw, 
                                    'units' => ($this->itemdata['units'] === 'unixtime') ? 'uptime' : $this->itemdata['units'] ]);
    }

    private function makeValueFormatted() {

        if (isset( $this->itemdata['error']) && $this->itemdata['state'] == ITEM_STATE_NOTSUPPORTED) {
            $this->value_formatted =  'UNSUPPORTED';
            return;
        }

        if (!isset($this->history) || !is_array($this->history) || 0 == count($this->history) ) 
            return;
        
        $last_history = $this->history[0]['value'];

        if ( $this->isNumericItem() && ITEM_VALUE_TYPE_STR != $this->itemdata['value_type'] && mb_strlen($last_history) > 20 ) {
               $this->value_formatted = substr($last_value, 0, 20). '...';
               return;
        }     
                          
        $this->value_formatted = formatHistoryValue($last_history, $this->itemdata, false);
    }

    private function makeAdminLinks(){
        if (!$this->editable) 
            return null;
        
        return new CLink(_('Edit'),  (new CUrl('items.php'))
            ->setArgument('form', 'update')
            ->setArgument('itemid', $this->itemdata['itemid'])
            ->setArgument('context', 'host'));
    }
    private function isSupportedItem() {
        if (isset($this->itemdata['error']) && strlen($this->itemdata['error']) > 0) 
            return false;
        return true;
    }
    
    private function addHintRow($name, $check_value, $object) {
        if (!isset($check_value))
            return;
        $this->hintbox->addRow([(new CSpan(_($name)))->addStyle(ZBX_STYLE_RIGHT), $object]);
    }
    
    private function makeHint() {
        $value_div = (new CDiv());
    
        $this->hintbox = (new CTableInfo())->setHeader(["",""]);
        $this->hintbox->addRow((new CCol($this->itemdata['name']))->setColSpan(2));
        
        $this->addHintRow('Last check', $this->last_poll_time, $this->last_poll_time);
        $this->addHintRow('Next check', $this->next_poll_time, $this->next_poll_time);

        if ($this->isSupportedItem()) {
            $this->addHintRow('Value', $this->value_formatted, $this->value_formatted);

            if ($this->isNumericItem()) {
                $this->addHintRow('Change', $this->value_change, $this->value_change); //maybe its worth to add down or up arrow
                $this->addHintRow('Graph', $this->history , $this->generateSvgGraph());
            }
        } else {
            $this->addHintRow('Operational status:', 1, (new CSpan('UNSUPPORTED'))->addClass(ZBX_STYLE_RED));
            $this->addHintRow('Error', $this->itemdata['error'], (new CSpan($this->itemdata['error']))->addClass(ZBX_STYLE_RED));
        }

        $this->addHintRow('Triggers', $this->triggers, $this->makeTriggerInfo());
        $this->addHintRow('History', 1, $this->makeHistoryLinks() );
        $this->addHintRow('Manage', $this->editable, $this->makeAdminLinks());

        $this->hintbox->addClass(ZBX_STYLE_HINTBOX_WRAP);
    }

    private function makeHistoryLinks() {
        $ranges=[ ['name' => _('Last hour'), 'range' => 'now-1h'],
                  ['name' => _('Last day'), 'range' => 'now-1d'],
                  ['name' => _('Last week'), 'range' => 'now-7d'],
        ];

        $is_graph = ($this->itemdata['value_type'] == ITEM_VALUE_TYPE_FLOAT || $this->itemdata['value_type'] == ITEM_VALUE_TYPE_UINT64);
       
        $list = (new CList());

        foreach ($ranges as $range) {
            $list->addItem(
                 new CLink($range['name'], (new CUrl('history.php'))
                        ->setArgument('action', $is_graph ? HISTORY_GRAPH : HISTORY_VALUES)
                        ->setArgument('from', $range['range'])
                        ->setArgument('to', 'now')
                        ->setArgument('itemids[]', $this->itemdata['itemid'])
            ));
        }

        return $list;
    }

    private function calcSeverities() {
        $severities = array(0,0,0,0,0,0);
        
        if (!isset($this->triggers)) 
            return $severities;

        foreach ($this->triggers as $trigger) {
            if (TRIGGER_VALUE_TRUE == $trigger['value']) {
                $severities[$trigger['priority']]++;
                $this->worst_severity = MAX($this->worst_severity, $trigger['priority']);
            }
        }
        return $severities;
    }

    private function makeTriggerInfo() {
        
        $severities = $this->calcSeverities();
        
        $problems_link = (new CLink(_(''), (new CUrl('zabbix.php'))
		        ->setArgument('action', 'problem.view')
		        ->setArgument('filter_name', '')
		        //->setArgument('severities', $data['filter']['severities'])
		        ->setArgument('hostids', [$this->itemdata['hostid']])))
            ->addItem(new CTriggersCounters($severities))
            ->addClass(ZBX_STYLE_PROBLEM_ICON_LINK);
        
        return $problems_link;
    }

    private function generateSvgGraph() {
         
        if (isset($this->history) && count($this->history) > 0) 
            return  (new CDiv(new CSVGSmallGraph($this->history, 50, 200)))
                        ->addClass(GLB_STYLE_GRAPH_PREVIEW);

        return '';
    }

    private function fetchMissingData() {
        
    }
}