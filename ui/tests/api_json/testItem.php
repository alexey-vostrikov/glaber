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


require_once dirname(__FILE__).'/../include/CAPITest.php';
require_once dirname(__FILE__).'/../include/helpers/CDataHelper.php';

/**
 * @backup items
 * @on-before prepareUpdateData
 */
class testItem extends CAPITest {

	protected static $items;

	public static function getItemCreateData() {
		return [
			// Test update interval for mqtt key of the Agent item type.
			[
				'request_data' => [
					'hostid' => '50009',
					'name' => 'Test mqtt key',
					'key_' => 'mqtt.get[0]',
					'interfaceid' => '50022',
					'value_type' => ITEM_VALUE_TYPE_UINT64,
					'type' => ITEM_TYPE_ZABBIX,
					'delay' => '30s'
				],
				'expected_error' => null
			],
			[
				'request_data' => [
					'hostid' => '50009',
					'name' => 'Test mqtt key without delay',
					'key_' => 'mqtt.get[1]',
					'interfaceid' => '50022',
					'value_type' => ITEM_VALUE_TYPE_UINT64,
					'type' => ITEM_TYPE_ZABBIX
				],
				'expected_error' => 'Incorrect arguments passed to function.'
			],
			[
				'request_data' => [
					'hostid' => '50009',
					'name' => 'Test mqtt key with 0 delay',
					'key_' => 'mqtt.get[2]',
					'interfaceid' => '50022',
					'value_type' => ITEM_VALUE_TYPE_UINT64,
					'type' => ITEM_TYPE_ZABBIX,
					'delay' => '0'
				],
				'expected_error' => 'Item will not be refreshed. Specified update interval requires having at least one either flexible or scheduling interval.'
			],
			// Test update interval for mqtt key of the Active agent type.
			[
				'request_data' => [
					'hostid' => '50009',
					'name' => 'Test mqtt key for active agent',
					'key_' => 'mqtt.get[3]',
					'interfaceid' => '50022',
					'value_type' => ITEM_VALUE_TYPE_UINT64,
					'type' => ITEM_TYPE_ZABBIX_ACTIVE
				],
				'expected_error' => null
			],
			[
				'request_data' => [
					'hostid' => '50009',
					'name' => 'Test mqtt key with 0 delay for active agent',
					'key_' => 'mqtt.get[4]',
					'interfaceid' => '50022',
					'value_type' => ITEM_VALUE_TYPE_UINT64,
					'type' => ITEM_TYPE_ZABBIX_ACTIVE,
					'delay' => '0'
				],
				'expected_error' => null
			],
			[
				'request_data' => [
					'hostid' => '50009',
					'name' => 'Test mqtt with wrong key and 0 delay',
					'key_' => 'mqt.get[5]',
					'interfaceid' => '50022',
					'value_type' => ITEM_VALUE_TYPE_UINT64,
					'type' => ITEM_TYPE_ZABBIX_ACTIVE,
					'delay' => '0'
				],
				'expected_error' => 'Item will not be refreshed. Specified update interval requires having at least one either flexible or scheduling interval.'
			]
		];
	}

	/**
	 * @dataProvider getItemCreateData
	 */
	public function testItem_Create($request_data, $expected_error) {
		$result = $this->call('item.create', $request_data, $expected_error);

		if ($expected_error === null) {
			if ($request_data['type'] === ITEM_TYPE_ZABBIX_ACTIVE && substr($request_data['key_'], 0, 8) === 'mqtt.get') {
				$request_data['delay'] = CTestArrayHelper::get($request_data, 'delay', '0');
			}

			foreach ($result['result']['itemids'] as $id) {
				$db_item = CDBHelper::getRow('SELECT hostid, name, key_, type, delay FROM items WHERE itemid='.zbx_dbstr($id));

				foreach (['hostid', 'name', 'key_', 'type', 'delay'] as $field) {
					$this->assertSame($db_item[$field], strval($request_data[$field]));
				}
			}
		}
	}

	public static function prepareUpdateData() {
		$interfaces = [
			[
				'type' => 1,
				'main' => 1,
				'useip' => 1,
				'ip' => '127.0.0.1',
				'dns' => '',
				'port' => '10050'
			]
		];

		$groups = [
			[
				'groupid' => 4
			]
		];

		$result = CDataHelper::createHosts([
			[
				'host' => 'testItem_Update',
				'interfaces' => $interfaces,
				'groups' => $groups,
				'status' => HOST_STATUS_MONITORED,
				'items' => [
					[
						'name' => 'Agent ping',
						'key_' => 'agent.ping',
						'type' => ITEM_TYPE_ZABBIX,
						'value_type' => ITEM_VALUE_TYPE_UINT64,
						'delay' => '1s'
					],
					[
						'name' => 'Agent version',
						'key_' => 'agent.version',
						'type' => ITEM_TYPE_ZABBIX,
						'value_type' => ITEM_VALUE_TYPE_UINT64,
						'delay' => '1m'
					]
				]
			]
		]);

		self::$items = $result['itemids'];
	}

	public static function getItemUpdateData() {
		return [
			// Test update interval for mqtt key of the Agent item type.
			[
				'request_data' => [
					'item' => 'testItem_Update:agent.ping',
					'key_' => 'mqtt.get[00]',
					'delay' => '0'
				],
				'expected_error' => 'Item will not be refreshed. Specified update interval requires having at least one either flexible or scheduling interval.'
			],
			// Test update interval for wrong mqtt key of the Active agent item type.
			[
				'request_data' => [
					'item' => 'testItem_Update:agent.ping',
					'key_' => 'mqt.get[11]',
					'type' => ITEM_TYPE_ZABBIX,
					'delay' => '0'
				],
				'expected_error' => 'Item will not be refreshed. Specified update interval requires having at least one either flexible or scheduling interval.'
			],
			// Change type to active agent and check update interval for mqtt key.
			[
				'request_data' => [
					'item' => 'testItem_Update:agent.ping',
					'key_' => 'mqtt.get[22]',
					'type' => ITEM_TYPE_ZABBIX_ACTIVE,
					'delay' => '0'
				],
				'expected_error' => null
			],
			[
				'request_data' => [
					'item' => 'testItem_Update:agent.version',
					'name' => 'Test mqtt key for active agent',
					'key_' => 'mqtt.get[33]',
					'type' => ITEM_TYPE_ZABBIX_ACTIVE
				],
				'expected_error' => null
			]
		];
	}

	/**
	 * @dataProvider getItemUpdateData
	 */
	public function testItem_Update($request_data, $expected_error) {
		$request_data['itemid'] = self::$items[$request_data['item']];
		unset($request_data['item']);

		$result = $this->call('item.update', $request_data, $expected_error);

		if ($expected_error === null) {
			if ($request_data['type'] === ITEM_TYPE_ZABBIX_ACTIVE && substr($request_data['key_'], 0, 8) === 'mqtt.get') {
				$request_data['delay'] = CTestArrayHelper::get($request_data, 'delay', '0');
			}

			foreach ($result['result']['itemids'] as $id) {
				$db_item = CDBHelper::getRow('SELECT key_, type, delay FROM items WHERE itemid='.zbx_dbstr($id));

				foreach (['key_', 'type', 'delay'] as $field) {
					$this->assertSame($db_item[$field], strval($request_data[$field]));
				}
			}
		}
	}

	public static function getItemDeleteData() {
		return [
			[
				'item' => ['40072'],
				'data' => [
					'discovered_triggerids' => ['30002'],
					'dependent_item' => ['40074'],
					'dependent_item_disc_triggerids' => ['30004']
				],
				'expected_error' => null
			]
		];
	}

	/**
	* @dataProvider getItemDeleteData
	*/
	public function testItem_Delete($item, $data, $expected_error) {
		$result = $this->call('item.delete', $item, $expected_error);

		if ($expected_error === null) {
			foreach ($result['result']['itemids'] as $id) {
				$dbResult = 'SELECT * FROM items WHERE itemid='.zbx_dbstr($id);
				$this->assertEquals(0, CDBHelper::getCount($dbResult));
			}

			// Check that related discovered trigerid is removed with all related data.
			if (array_key_exists('discovered_triggerids', $data)) {
				foreach ($data['discovered_triggerids'] as $id) {
					$dbResult = 'SELECT * FROM triggers WHERE triggerid='.zbx_dbstr($id);
					$this->assertEquals(0, CDBHelper::getCount($dbResult));

					$dbResult = 'SELECT * FROM functions WHERE triggerid='.zbx_dbstr($id);
					$this->assertEquals(0, CDBHelper::getCount($dbResult));

					$dbResult = 'SELECT * FROM trigger_discovery WHERE triggerid='.zbx_dbstr($id);
					$this->assertEquals(0, CDBHelper::getCount($dbResult));
				}
			}

			// Check that dependent item is removed.
			if (array_key_exists('dependent_item', $data)) {
				foreach ($data['dependent_item'] as $id) {
					$dbResult = 'SELECT * FROM items WHERE itemid='.zbx_dbstr($id);
					$this->assertEquals(0, CDBHelper::getCount($dbResult));
				}
			}

			// Check that discovered trigger of dependent item is removed with all related data.
			if (array_key_exists('dependent_item_disc_triggerids', $data)) {
				foreach ($data['dependent_item_disc_triggerids'] as $id) {
					$dbResult = 'SELECT * FROM triggers WHERE triggerid='.zbx_dbstr($id);
					$this->assertEquals(0, CDBHelper::getCount($dbResult));

					$dbResult = 'SELECT * FROM functions WHERE triggerid='.zbx_dbstr($id);
					$this->assertEquals(0, CDBHelper::getCount($dbResult));

					$dbResult = 'SELECT * FROM trigger_discovery WHERE triggerid='.zbx_dbstr($id);
					$this->assertEquals(0, CDBHelper::getCount($dbResult));
				}
			}
		}
	}
}
