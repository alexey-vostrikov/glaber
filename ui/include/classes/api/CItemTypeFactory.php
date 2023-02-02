<?php declare(strict_types = 1);
/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

final class CItemTypeFactory {

	/**
	 * An array of created object instances.
	 *
	 * @param array
	 */
	private static $instances = [];

	/**
	 * @param int $type
	 *
	 * @return CItemType
	 *
	 * @throws APIException
	 */
	public static function getObject(int $type): CItemType {
		if (array_key_exists($type, self::$instances)) {
			return self::$instances[$type];
		}

		switch ($type) {
			case ITEM_TYPE_ZABBIX:
				return self::$instances[$type] = new CItemTypeZabbix();

			case ITEM_TYPE_TRAPPER:
				return self::$instances[$type] = new CItemTypeTrapper();

			case ITEM_TYPE_SIMPLE:
				return self::$instances[$type] = new CItemTypeSimple();

			case ITEM_TYPE_INTERNAL:
				return self::$instances[$type] = new CItemTypeInternal();

			case ITEM_TYPE_ZABBIX_ACTIVE:
				return self::$instances[$type] = new CItemTypeZabbixActive();

			case ITEM_TYPE_EXTERNAL:
				return self::$instances[$type] = new CItemTypeExternal();

			case ITEM_TYPE_DB_MONITOR:
				return self::$instances[$type] = new CItemTypeDbMonitor();

			case ITEM_TYPE_IPMI:
				return self::$instances[$type] = new CItemTypeIpmi();

			case ITEM_TYPE_SSH:
				return self::$instances[$type] = new CItemTypeSsh();

			case ITEM_TYPE_TELNET:
				return self::$instances[$type] = new CItemTypeTelnet();

			case ITEM_TYPE_CALCULATED:
				return self::$instances[$type] = new CItemTypeCalculated();

			case ITEM_TYPE_JMX:
				return self::$instances[$type] = new CItemTypeJmx();

			case ITEM_TYPE_SNMPTRAP:
				return self::$instances[$type] = new CItemTypeSnmpTrap();

			case ITEM_TYPE_DEPENDENT:
				return self::$instances[$type] = new CItemTypeDependent();

			case ITEM_TYPE_HTTPAGENT:
				return self::$instances[$type] = new CItemTypeHttpAgent();

			case ITEM_TYPE_SNMP:
				return self::$instances[$type] = new CItemTypeSnmp();

			case ITEM_TYPE_SCRIPT:
				return self::$instances[$type] = new CItemTypeScript();
			
			case ITEM_TYPE_WORKER_SERVER:
				return self::$instances[$type] = new CItemTypeWorkerServer();	

		}

		throw new APIException(ZBX_API_ERROR_INTERNAL, 'Incorrect item type.');
	}
}
