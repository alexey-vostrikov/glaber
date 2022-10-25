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

namespace Modules\HostOverview\Actions;

use CControllerResponseData;
use CControllerResponseFatal;
use CController as CAction;
use API;
use Manager;
 

/**
 * Example module action.
 */
class CHostOverview extends CAction {
 
	/**
	 * Initialize action. Method called by Zabbix core.
	 *
	 * @return void
	 */
	public function init(): void {
		/**
		 * Disable SID (Sessoin ID) validation. Session ID validation should only be used for actions which involde data
		 * modification, such as update or delete actions. In such case Session ID must be presented in the URL, so that
		 * the URL would expire as soon as the session expired.
		 */
		$this->disableSIDvalidation();
	}
 
	/**
	 * Check and sanitize user input parameters. Method called by Zabbix core. Execution stops if false is returned.
	 *
	 * @return bool true on success, false on error.
	 */
	protected function checkInput(): bool {
		$fields = [
			'hostid'  => 'required|id',
		];
 
		// Only validated data will further be available using $this->hasInput() and $this->getInput().
		$ret = $this->validateInput($fields);
 
		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}
 
		return $ret;
	}
 
	/**
	 * Check if the user has permission to execute this action. Method called by Zabbix core.
	 * Execution stops if false is returned.
	 *
	 * @return bool
	 */
	protected function checkPermissions(): bool {
		$permit_user_types = [USER_TYPE_ZABBIX_ADMIN, USER_TYPE_SUPER_ADMIN];
 
		return in_array($this->getUserType(), $permit_user_types);
	}
 
	/**
	 * Prepare the response object for the view. Method called by Zabbix core.
	 *
	 * @return void
	 */
	protected function doAction(): void {
		$data = [];
 
		$hostid = $this->getInput('hostid');
			
		$data['hostinfo'] = API::Host()->get(['hostids'=> [$hostid], 	] );
		$data['entities'] = $this->getEntitiesStructure($hostid);		
		
		$items = $this->getItems($hostid);
		
		$data['all_triggers'] = API::Trigger()->get([
			'output' => ['itemid','description', 'status'],
			'selectHosts' => ['hostid'],
			'selectItems' => ['itemid'],
			'hostids' => [$hostid],
			'skipDependent' => true,
			'monitored' => true,
			'preservekeys' => true
		]);

		$data['all_problems'] = $this->getTriggerProblems($data['all_triggers']);
		$data['problems_counts'] = $this->calcProblemsCounts($data['all_problems']);
		$this->linkItemsToProblems($data['all_problems'], $data['all_triggers'], $items);

		$data['latestdata'] = $this->getLastValues($items); 

		$this->assignItemsToEntities($data, $items);
		$this->assignItemsToTemplates($data, $items);
		
		
		//$data['items_problems'] = $this->makeItemsToProblems($data['problems']);		

		if (isset($items))
			$data['items'] = $items;
		else 
			$data['items'] = [];

		$response = new CControllerResponseData($data);
 
		$this->setResponse($response);
	}
	
	protected function calcProblemsCounts(array &$problems) {
		$count = [0,0,0,0,0,0];
		
		foreach ($problems as $problem) {
			$severity = $problem['severity'];
			$count[$severity]++;
		}
		return $count;
	}
	
	protected function linkItemsToProblems(array &$problems, array &$triggers, array &$items) {
		foreach ($problems as $problem) {
			$trigger_id = $problem['objectid'];
			
			foreach($triggers[$trigger_id]['items'] as $t_item) {
				$items[$t_item['itemid']]['problems'][] = $problem['eventid'];
			}

		}

	}

	protected function getTriggerProblems(array &$triggers) {
	
		$problems = API::Problem()->get([
			'output' =>  API_OUTPUT_EXTEND,
			'objectids' => array_keys($triggers),
			'source' => EVENT_SOURCE_TRIGGERS,
			'object' => EVENT_OBJECT_TRIGGER,
		]);

		$problems = zbx_toHash($problems, 'eventid');
		return $problems;
	}

	protected function getLastValues(array &$items) {
		$hash_items = zbx_toHash($items,'itemid');
		return Manager::History()->getLastValues($hash_items); 
	}
	protected function getItems(string $hostid) {
		return zbx_toHash( API::Item()->get([
			'hostids'=> [$hostid],
			'output' => ['name', 'type', 'key_',
				'params', 'value_type', 'units', 'history',
				'trends', 'description', 'status',
				'templateid', 'templatename', 'flags', 'master_itemid' 
			],
			'selectValueMap' => ['mappings'],
			'selectTemplates' => ['templateid','name'],
			'selectDiscoveryRule' => ['itemid', 'name', 'templateid', 'key_'],
			'selectItemDiscovery' => ['parent_itemid','itemdiscoveryid','itemid'],
			'selectTags' => ['tag', 'value'],
			'selectTriggers' => ['triggerid', 'priority', 'description', 'status']
		]),'itemid');
	}
	
	protected function assignItemsToTemplates(array &$data, array &$items) {
	
		$templates = getItemParentTemplates($items, ZBX_FLAG_DISCOVERY_NORMAL);
		
		foreach ($items as $key => $item) {
			if (isset ($item['templateid'])) {		
	
				//templates will hold links hash
				if (isset($templates['links'][$item['itemid']])) {

					$template_id = $templates['links'][$item['itemid']]['hostid'];

					if (!isset($templates['templates'][$template_id]['items']))
						$templates['templates'][$template_id]['items'] = [];

					array_push($templates['templates'][$template_id]['items'], $item);
				
					unset($items[$key]);
				} 
			} 
		}
		
		$data['templates'] = $templates['templates'];
	}

	protected function getEntitiesStructure(string $hostid) {
		$entities = API::DiscoveryRule()->get(['hostids'=> [$hostid]]);
				
		//this will fetch all the prototypes (templates) for the entity ID
		$entity_items = API::ItemPrototype()->get([
			'discoveryids' => array_column($entities,'itemid'),
			'output' => ['itemid', 'name', 'description', 'key_'],
			'selectDiscoveryRule' => ['itemid'],
		]);
		
		
		$result = [];

		foreach( $entities as $entity ) {
			$result[$entity['itemid']] = $entity;
			$result[$entity['itemid']]['items'] = [];
		}
		
		foreach ($entity_items as $item ) {
			$entity_id = $item['discoveryRule']['itemid'];
			$result[$entity_id]['items'][$item['itemid']]= $item;
			//removing macroses from the names
			$name = preg_replace('/{#[A-Z0-9_]*}/',"",$item['name']);
			$name = preg_replace('/\$[0-9]/',"",$name);
			$result[$entity_id]['items'][$item['itemid']]['name'] = $name;
		}

		return $result;
	}
	
	protected function assignItemsToEntities(array &$data, array &$items) {
	
		foreach ($items as $key => $item) {
			//fetching an entity name
			if ( isset($item['discoveryRule']) && 
				 is_array($item['discoveryRule']) &&
				 isset($item['discoveryRule']['itemid']) ) {
			
				
				$entity_discovery_id = $item['discoveryRule']['itemid'];
				$entity_discovery_item_id = $item['itemDiscovery']['parent_itemid'];


				[$entity_macro,$entity_name] = $this->str_diff(
						$data['entities'][$entity_discovery_id]['items'][$entity_discovery_item_id]['key_'],$item['key_']);
				
				$entities=&$data['entities'][$entity_discovery_id]['entities'];
				
				if (!isset($entities)) 
					$entities=[];
				
				if (strlen($entity_name) < 1) 
					continue;
				
				$entities[$entity_name][$entity_discovery_item_id] = $item;
	
				unset($items[$key]);
			} 
		}
	}



	private function str_diff(string $old, string $new){
		$from_start = strspn($old ^ $new, "\0");        
		$from_end = strspn(strrev($old) ^ strrev($new), "\0");
	
		$old_end = strlen($old) - $from_end;
		$new_end = strlen($new) - $from_end;
	
		$start = substr($new, 0, $from_start);
		$end = substr($new, $new_end);
		$new_diff = substr($new, $from_start, $new_end - $from_start);  
		$old_diff = substr($old, $from_start, $old_end - $from_start);
	
		return [$old_diff, $new_diff];
	}
}


