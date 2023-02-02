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

class CItemTypeSnmp extends CItemType {

	/**
	 * @inheritDoc
	 */
	const TYPE = ITEM_TYPE_SNMP;

	/**
	 * @inheritDoc
	 */
	const FIELD_NAMES = ['interfaceid', 'snmp_oid', 'delay'];

	/**
	 * @inheritDoc
	 */
	public static function getCreateValidationRules(array $item): array {
		return [
			'interfaceid' =>	self::getCreateFieldRule('interfaceid', $item),
			'snmp_oid' =>		['type' => API_STRING_UTF8, 'flags' => API_REQUIRED | API_NOT_EMPTY, 'length' => DB::getFieldLength('items', 'snmp_oid')],
			'delay' =>			self::getCreateFieldRule('delay', $item)
		];
	}

	/**
	 * @inheritDoc
	 */
	public static function getUpdateValidationRules(array $db_item): array {
		return [
			'interfaceid' =>	self::getUpdateFieldRule('interfaceid', $db_item),
			'snmp_oid' =>		['type' => API_STRING_UTF8, 'flags' => API_NOT_EMPTY, 'length' => DB::getFieldLength('items', 'snmp_oid')],
			'delay' =>			self::getUpdateFieldRule('delay', $db_item)
		];
	}

	/**
	 * @inheritDoc
	 */
	public static function getUpdateValidationRulesInherited(array $db_item): array {
		return [
			'interfaceid' =>	self::getUpdateFieldRuleInherited('interfaceid', $db_item),
			'snmp_oid' =>		['type' => API_UNEXPECTED, 'error_type' => API_ERR_INHERITED],
			'delay' =>			self::getUpdateFieldRuleInherited('delay', $db_item)
		];
	}

	/**
	 * @inheritDoc
	 */
	public static function getUpdateValidationRulesDiscovered(): array {
		return [
			'interfaceid' =>	self::getUpdateFieldRuleDiscovered('interfaceid'),
			'snmp_oid' =>		['type' => API_UNEXPECTED, 'error_type' => API_ERR_DISCOVERED],
			'delay' =>			self::getUpdateFieldRuleDiscovered('delay')
		];
	}
}
