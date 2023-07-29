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
    
    private $value_raw;
    private $value_formatted; 
    private $value_short;
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
        
        $this->value_formatted = " - ";
        $this->value_short = new CIcon("fa-solid fa-eye-slash fa-lg");
        
        $this->fetchMissingData();
        $this->makeValueFormatted();
        
        $this->makeValueChange();
        $this->calcTimestamps();
       
        
        if ($this->isDisabledItem()) {
          //  $this->addClass(ZBX_STYLE_GREY);
            $this->value_short = (new CIcon("fa-solid fa-triangle-exclamation fa-lg"))
                            ->addClass(ZBX_STYLE_YELLOW);;
        }
        else if (!$this->isSupportedItem()) {
//            $this->addClass(ZBX_STYLE_RED);
            $this->value_short = (new CIcon("fa-solid fa-triangle-exclamation fa-lg"))
                            ->addClass(ZBX_STYLE_RED);
        }
        parent::__construct($this->value_short);
        
        if ($this->isNumericItem())
            $this->addClass(ZBX_STYLE_NOWRAP);

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
        if ( !$this->isDisabledItem()) 
            return $this->last_poll_time;
        return null;
    }
    public function GetLastCheckRaw() {
        return $this->last_poll_time_raw;
    }
    public function GetValueRaw() {
        return $this->value_raw;
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

        if ( $this->isDisabledItem() ) {
            $this->value_formatted =  'DISABLED';
            return;
        }

        if (isset( $this->itemdata['error']) && $this->itemdata['state'] == ITEM_STATE_NOTSUPPORTED) {
            $this->value_formatted =  'UNSUPPORTED';
            $this->value_raw = 'UNSUPPORTED';
            return;
        }

        if (!isset($this->history) || !is_array($this->history) || 0 == count($this->history) ) 
            return;
        
        $last_history = $this->history[0]['value'];

        $this->value_raw = $last_history;
        $this->value_formatted = formatHistoryValue($last_history, $this->itemdata, false);

        if ( !$this->isNumericItem() &&  mb_strlen($last_history) > 20 ) {
                $this->value_short = substr($last_history, 0, 20). '...';
                return;
        }     
        
        $this->value_short = $this->value_formatted;
    }

    private function makeAdminLinks(){

        return new CLink(_('Edit'),  (new CUrl('items.php'))
            ->setArgument('form', 'update')
            ->setArgument('itemid', $this->itemdata['itemid'])
            ->setArgument('context', 'host'));
    }
    
    private function isDisabledItem() {
        return (ITEM_STATUS_DISABLED == $this->itemdata['status']);
    }
    
    private function isSupportedItem() {
        if (isset($this->itemdata['error']) && strlen($this->itemdata['error']) > 0) 
            return false;
        return true;
    }
    
    private function addHintRow(&$element, $name, $check_value, $object) {
        if (!isset($check_value))
            return;
        $element->addRow([(new CSpan($name))->addStyle(ZBX_STYLE_RIGHT)->addStyle(ZBX_STYLE_WORDWRAP), $object]);
    }
    
    private function makeHint() {
        //$value_div = (new CDiv());
    
        $this->hintbox = (new CTableInfo())->setHeader(["",""]);
        $this->hintbox->addRow((new CCol(new CTag('strong', true, $this->itemdata['name'])))->setColSpan(2));
        
        if ( ! $this->isDisabledItem()) {
            $this->addHintRow($this->hintbox, _('Last check'), $this->last_poll_time, $this->last_poll_time);
            $this->addHintRow($this->hintbox, _('Next check'), $this->next_poll_time, $this->next_poll_time);
        }

        if ($this->isSupportedItem()) {
            $this->addHintRow($this->hintbox, _('Value'), $this->value_formatted, $this->value_formatted);

            if ($this->isNumericItem()) {
                $this->addHintRow($this->hintbox, _('Change'), $this->value_change, $this->value_change); //maybe its worth to add down or up arrow
                $this->addHintRow($this->hintbox, _('Graph'), $this->history , $this->generateSvgGraph());
            }
        } else {
            $this->addHintRow($this->hintbox, _('Operational status'), 1, (new CSpan('UNSUPPORTED'))->addClass(ZBX_STYLE_RED));
            $this->addHintRow($this->hintbox, _('Error'), $this->itemdata['error'], (new CSpan($this->itemdata['error']))->addClass(ZBX_STYLE_RED));
        }

        $this->addHintRow($this->hintbox, _('Triggers'), $this->triggers, $this->makeTriggerInfo());
        $this->addHintRow($this->hintbox, _('History'), 1, $this->makeHistoryLinks() );
        $this->addHintRow($this->hintbox, _('Manage'), $this->editable, $this->makeAdminLinks());

        $this->hintbox->addClass(ZBX_STYLE_HINTBOX_WRAP);
    }
    
    public function makeStateInfo() {
        $info = new CDiv();
        
        $state_info = (new CTableInfo());//->setHeader(["",""]);
        //$this->hintbox->addRow((new CCol(new CTag('strong', true, $this->itemdata['name'])))->setColSpan(2));
        
        if ( ! $this->isDisabledItem()) {
            $this->addHintRow($state_info, _('Last check'), $this->last_poll_time, $this->last_poll_time);
            $this->addHintRow($state_info, _('Next check'), $this->next_poll_time, $this->next_poll_time);
        

            if ($this->isSupportedItem()) {
                $this->addHintRow($state_info, _('Operational status'), 1, (new CSpan('SUPPORTED'))->addClass(ZBX_STYLE_GREEN));
                $this->addHintRow($state_info, _('LastValue'), $this->value_formatted, $this->value_formatted);

                if ($this->isNumericItem()) {
                    $this->addHintRow($state_info, _('Change'), $this->value_change, $this->value_change); //maybe its worth to add down or up arrow
                    $this->addHintRow($state_info, _('Graph'), $this->history , $this->generateSvgGraph());
                }
            } else {
                $this->addHintRow($state_info, _('Operational status'), 1, (new CSpan('UNSUPPORTED'))->addClass(ZBX_STYLE_RED));
                $this->addHintRow($state_info, _('Error'), $this->itemdata['error'], (new CSpan($this->itemdata['error']))->addClass(ZBX_STYLE_RED));
            }
        } else {
           $this->addHintRow($state_info, _('Operational state'), 1, (new CSpan('DISABLED'))->addClass(ZBX_STYLE_YELLOW));
        }

        $this->addHintRow($state_info, _('Triggers'), $this->triggers, $this->makeTriggerInfo());
        $this->addHintRow($state_info, _('History'), 1, $this->makeHistoryLinks() );
        

        $vc_table = (new CTableInfo())->setHeader(["Time","Time rel", "Value"]);
        
        foreach ($this->history as $idx => $hist) {
            $vc_table->addRow([
                            zbx_date2str(DATE_TIME_FORMAT_SECONDS, $hist['clock']), 
                            zbx_date2age($hist['clock']),
                            formatHistoryValue($hist['value'], $this->itemdata, false)]);
        }

        $info->addItem((new CDiv())
                ->addItem(new CTag('h3',true, _('Current state data')))
                ->addItem($state_info) )
             ->addItem((new CDiv())
                ->addItem(new CTag('h3',true, _('Value Cache data')))
                ->addItem($vc_table));

        return $info;

    }
   
    private function makeHistoryLinks() {
        $ranges=[ ['name' => _('Last hour'), 'range' => 'now-1h'],
                  ['name' => _('Last day'), 'range' => 'now-1d'],
                  ['name' => _('Last week'), 'range' => 'now-7d'],
        ];

        $is_graph = ($this->itemdata['value_type'] == ITEM_VALUE_TYPE_FLOAT || $this->itemdata['value_type'] == ITEM_VALUE_TYPE_UINT64);
       
        $list = (new CDiv())->addClass(ZBX_STYLE_NOWRAP);;

        foreach ($ranges as $range) {
            $list->addItem(
                 new CLink($range['name'], (new CUrl('history.php'))
                        ->setArgument('action', $is_graph ? HISTORY_GRAPH : HISTORY_VALUES)
                        ->setArgument('from', $range['range'])
                        ->setArgument('to', 'now')
                        ->setArgument('itemids[]', $this->itemdata['itemid'])
            ))
            ->addItem(NBSP())->addItem(NBSP()) ;
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
        $problems = "";
        
        if (array_sum($severities) == 0)
            $problems = _('Problems');

        $problems_link = (new CLink($problems, (new CUrl('zabbix.php'))
		        ->setArgument('action', 'problem.view')
		        ->setArgument('filter_name', '')
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
        if (!isset($this->history) || 0 == count($this->history)) {
           // error_log("Fetching the history for item ". $this->itemdata['itemid']. "\n");

            $this->history = \Manager::History()->getLastValues([$this->itemdata], 100,
			    timeUnitToSeconds(\CSettingsHelper::get(\CSettingsHelper::HISTORY_PERIOD))
		    );

            if (isset( $this->history[$this->itemdata['itemid']]))
                $this->history = $this->history[$this->itemdata['itemid']];
           // error_log("Result is ".json_encode($this->history));
        }


    }
}