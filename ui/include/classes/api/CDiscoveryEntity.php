<?php
/*
 ** Glaber
 ** Copyright (C) Glaber
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

/*class to retieve DiscoveryEntities by hosts and itemids */
/* now entities are built out of the items which isn't precise
but in most cases give nice results, when glaber will support
in-memory entities and direct entitites api, then it will be
able to retrieve items bound to entities without items at all */

/* for now to function properly, it needs items with discovery and tempated
data fetched:

so please pass items fetched via API with the options:

'selectTemplates' => ['templateid','name'],
'selectDiscoveryRule' => ['itemid', 'name', 'templateid', 'key_'],
'selectItemDiscovery' => ['parent_itemid','itemdiscoveryid','itemid'],

 */
class CDiscoveryEntity
{

    public const ACCESS_RULES = [
        'get' => ['min_user_type' => USER_TYPE_ZABBIX_USER],
        'create' => ['min_user_type' => USER_TYPE_ZABBIX_ADMIN],
        'update' => ['min_user_type' => USER_TYPE_ZABBIX_ADMIN],
        'delete' => ['min_user_type' => USER_TYPE_ZABBIX_ADMIN],
    ];

    public function get($options = [])
    {

        if (!isset($options['items']) && !is_array($options['items'])) {
            return [];
        }

        if (!isset($options['hostids']) && !is_array($options['hostids'])) {
            return [];
        }

        $entities = $this->getEntitiesStructure($options['hostids']);
        
        $items = $options['items'];

        $this->assignItemsToEntities($entities, $items);
        //$this->assignItemsToTemplates($entities, $items);
       // $templates = $this->assignItemsToTemplates($items);

        return $entities;
    }

	
    protected function assignItemsToEntities(array &$entities, array &$items) {
	
		foreach ($items as $key => $item) {
			if ( isset($item['discoveryRule']) && 
				 is_array($item['discoveryRule']) &&
				 isset($item['discoveryRule']['itemid']) ) {
			
				
				$entity_discovery_id = $item['discoveryRule']['itemid'];
				$entity_discovery_item_id = $item['itemDiscovery']['parent_itemid'];


				[$entity_macro,$entity_name] = $this->str_diff(
						$entities[$entity_discovery_id]['items'][$entity_discovery_item_id]['key_'],$item['key_']);
				
				if (strlen($entity_name) < 1) 
					continue;
				
				$entities[$entity_discovery_id]['entities'][$entity_name][$entity_discovery_item_id] = $item['itemid'];
				//unset($items[$key]);
			} 
		}
	
    }

    protected function assignItemsToTemplates(array &$items) {
		$templates = getItemParentTemplates($items, ZBX_FLAG_DISCOVERY_NORMAL);
		
		foreach ($items as $key => $item) {
			if (isset ($item['templateid'])) {		
	
				if (isset($templates['links'][$item['itemid']])) {

					$template_id = $templates['links'][$item['itemid']]['hostid'];
			
					if (!isset($templates['templates'][$template_id]['items']))
						$templates['templates'][$template_id]['items'] = [];

					array_push($templates['templates'][$template_id]['items'], $item);
				
					unset($items[$key]);
				} 
			} 
		}
		
		return $templates['templates'];
	}

    protected function getEntitiesStructure($hostids)
    {
        $entities = API::DiscoveryRule()->get(['hostids' => $hostids]);

        $entity_items = API::ItemPrototype()->get([
            'discoveryids' => array_column($entities, 'itemid'),
            'output' => ['itemid', 'name', 'description', 'key_'],
            'selectDiscoveryRule' => ['itemid'],
        ]);

        $result = [];

        foreach ($entities as $entity) {
            $result[$entity['itemid']] = $entity;
            $result[$entity['itemid']]['items'] = [];
        }

        foreach ($entity_items as $item) {
            $entity_id = $item['discoveryRule']['itemid'];
            $result[$entity_id]['items'][$item['itemid']] = $item;
   
            $name = preg_replace('/{#[A-Z0-9_]*}/', "", $item['name']);
            $name = preg_replace('/\$[0-9]/', "", $name);

            $result[$entity_id]['items'][$item['itemid']]['name'] = $name;
        }

        return $result;
    }

    private function str_diff(string $old, string $new)
    {
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
