<?php declare(strict_types = 0);
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


/**
 * @var CPartial $this
 * @var array    $data
 */

$operations_table = (new CTable())
	->setId('rec-table')
	->setAttribute('style', 'width: 100%;')
	->setHeader([_('Details'), _('Action')]);

$i = 0;
foreach ($data['action']['recovery_operations'] as $operationid => $operation) {
	if (!str_in_array($operation['operationtype'], $data['allowedOperations'][ACTION_RECOVERY_OPERATION])) {
		continue;
	}

	if (!isset($operation['opconditions'])) {
		$operation['opconditions'] = [];
	}

	if (!array_key_exists('opmessage', $operation)) {
		$operation['opmessage'] = [];
	}

	$operation['opmessage'] += [
		'mediatypeid' => '0',
		'message' => '',
		'subject' => '',
		'default_msg' => '1'
	];

	$operation_for_popup = array_merge($operation, ['id' => $operationid]);

	foreach (['opcommand_grp' => 'groupid', 'opcommand_hst' => 'hostid'] as $var => $field) {
		if (array_key_exists($var, $operation_for_popup)) {
			$operation_for_popup[$var] = zbx_objectValues($operation_for_popup[$var], $field);
		}
	}

	if (array_key_exists('recovery', $data['descriptions'])) {
		$data['descriptions'] = $data['descriptions']['recovery'];
	}

	$details_column = getActionOperationDescriptions(
		$data['action']['recovery_operations'], $data['eventsource'], $data['descriptions']
	)[$i];

	// Create hidden input fields for each row.
	$hidden_data = array_filter($operation, function ($key) {
		return !in_array($key, [
			'row_index', 'duration', 'steps', 'details'
		]);
	}, ARRAY_FILTER_USE_KEY);

	$operations_table->addRow([
		$details_column,
		(new CCol(
			new CHorList([
				(new CSimpleButton(_('Edit')))
					->addClass(ZBX_STYLE_BTN_LINK)
					->addClass('js-edit-operation')
					->setAttribute('data-operation', json_encode([
						'operationid' => $i,
						'actionid' => array_key_exists('actionid', $data) ? $data['actionid'] : 0,
						'eventsource' => array_key_exists('eventsource', $data)
							? $data['eventsource']
							: $operation['eventsource'],
						'operationtype' => ACTION_RECOVERY_OPERATION,
						'data' => $operation
					])),
				[
					(new CButton('remove', _('Remove')))
						->setAttribute('data-operationid', $i)
						->addClass('js-remove')
						->addClass(ZBX_STYLE_BTN_LINK)
						->removeId(),
					new CVar('recovery_operations['.$i.']', $hidden_data)
				]
			])
		))->addClass(ZBX_STYLE_NOWRAP)
	], null, 'recovery_operations_'.$i)->addClass(ZBX_STYLE_WORDBREAK);

	$i++;
}

$operations_table->addItem(
	(new CTag('tfoot', true))
		->addItem(
			(new CCol(
				(new CSimpleButton(_('Add')))
					->setAttribute('operationtype', ACTION_RECOVERY_OPERATION)
					->setAttribute('data-actionid', array_key_exists('actionid', $data) ? $data['actionid'] : 0)
					->setAttribute('data-eventsource', array_key_exists('eventsource', $data)
						? $data['eventsource']
						: $operation['eventsource']
					)
					->addClass('js-recovery-operations-create')
					->addClass(ZBX_STYLE_BTN_LINK)
			))->setColSpan(4)
		)
);

$operations_table->show();
