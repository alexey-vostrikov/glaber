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


/**
 * Convert a correlation condition operator to string.
 *
 * @param int $operator
 *
 * @return string
 */
function corrConditionOperatorToString($operator) {
	$operators = [
		CONDITION_OPERATOR_EQUAL => _('equals'),
		CONDITION_OPERATOR_NOT_EQUAL => _('does not equal'),
		CONDITION_OPERATOR_LIKE => _('contains'),
		CONDITION_OPERATOR_NOT_LIKE => _('does not contain')
	];

	return $operators[$operator];
}

/**
 * Returns correlation condition types or one type depending on input.
 *
 * @param int $type			Default: null. Returns all condition types.
 *
 * @return mixed			Returns condition type and it's string translation as array key => value pair.
 */
function corrConditionTypes($type = null) {
	$types = [
		ZBX_CORR_CONDITION_OLD_EVENT_TAG => _('Old event tag name'),
		ZBX_CORR_CONDITION_NEW_EVENT_TAG => _('New event tag name'),
		ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP => _('New event host group'),
		ZBX_CORR_CONDITION_EVENT_TAG_PAIR => _('Event tag pair'),
		ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE => _('Old event tag value'),
		ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE => _('New event tag value')
	];

	return ($type === null) ? $types : $types[$type];
}

/**
 * Returns correlation operation types or one type depending on input.
 *
 * @param int $type			Default: null. Returns all operation types.
 *
 * @return mixed			Returns operation type and it's string translation as array key => value pair.
 */
function corrOperationTypes($type = null) {
	$types = [
		ZBX_CORR_OPERATION_CLOSE_OLD => _('Close old events'),
		ZBX_CORR_OPERATION_CLOSE_NEW => _('Close new event')
	];

	return ($type === null) ? $types : $types[$type];
}

/**
 * Return an array of operators supported by the given correlation condition.
 *
 * @param int $type						Correlation condition type.
 *
 * @return array						Returns array of supported operators.
 */
function getOperatorsByCorrConditionType($type) {
	switch ($type) {
		case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
		case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
		case ZBX_CORR_CONDITION_EVENT_TAG_PAIR:
			return [CONDITION_OPERATOR_EQUAL];

		case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
		case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			return [CONDITION_OPERATOR_EQUAL, CONDITION_OPERATOR_NOT_EQUAL, CONDITION_OPERATOR_LIKE,
				CONDITION_OPERATOR_NOT_LIKE
			];

		case ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
			return [CONDITION_OPERATOR_EQUAL, CONDITION_OPERATOR_NOT_EQUAL];
	}
}

/**
 * Return the HTML representation of correlation operation type.
 *
 * @param array $operation					An array of correlation operation data.
 * @param int   $operation['type']			Correlation operation type.
 *
 * @return string
 */
function getCorrOperationDescription(array $operation) {
	return corrOperationTypes($operation['type']);
}

/**
 * Returns the HTML representation of a correlation condition.
 *
 * @param array  $condition              Array of correlation condition data.
 * @param int    $condition['type']      Condition type.
 * @param int    $condition['operator']  Condition operator.
 * @param int    $condition['groupid']   (optional) Depends on type.
 * @param int    $condition['newtag']    (optional) Depends on type.
 * @param int    $condition['oldtag']    (optional) Depends on type.
 * @param int    $condition['tag']       (optional) Depends on type.
 * @param int    $condition['value']     (optional) Depends on type.
 * @param array  $group_names            Host group names keyed by host group ID.
 *
 * @return array
 */
function getCorrConditionDescription(array $condition, array $group_names) {
	$description = [];

	switch ($condition['type']) {
		case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
			$description[] = _('Old event tag name').' '.corrConditionOperatorToString($condition['operator']).' ';
			$description[] = italic(CHtml::encode($condition['tag']));
			break;

		case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
			$description[] = _('New event tag name').' '.corrConditionOperatorToString($condition['operator']).' ';
			$description[] = italic(CHtml::encode($condition['tag']));
			break;

		case ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
			$description[] = _('New event host group').' '.corrConditionOperatorToString($condition['operator']).' ';
			$description[] = italic(CHtml::encode(array_key_exists($condition['groupid'], $group_names)
				? $group_names[$condition['groupid']]
				: _('Unknown')
			));
			break;

		case ZBX_CORR_CONDITION_EVENT_TAG_PAIR:
			$description[] = _('Value of old event tag').' ';
			$description[] = italic(CHtml::encode($condition['oldtag']));
			$description[] = ' '.corrConditionOperatorToString($condition['operator']).' '.
				_('value of new event tag').' ';
			$description[] = italic(CHtml::encode($condition['newtag']));
			break;

		case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			$description[] = _('Value of old event tag').' ';
			$description[] = italic(CHtml::encode($condition['tag']));
			$description[] = ' '.corrConditionOperatorToString($condition['operator']).' ';
			$description[] = italic(CHtml::encode($condition['value']));
			break;

		case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			$description[] = _('Value of new event tag').' ';
			$description[] = italic(CHtml::encode($condition['tag']));
			$description[] = ' '.corrConditionOperatorToString($condition['operator']).' ';
			$description[] = italic(CHtml::encode($condition['value']));
			break;
	}

	return $description;
}
