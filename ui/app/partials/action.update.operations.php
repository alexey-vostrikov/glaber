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
	->setId('upd-table')
	->setAttribute('style', 'width: 100%;')
	->setHeader([_('Details'), _('Action')]);

$i = 0;
foreach ($data['action']['update_operations'] as $operationid => $operation) {
	if (!str_in_array($operation['operationtype'], $data['allowedOperations'][ACTION_UPDATE_OPERATION])) {
		continue;
	}

	$operation += [
		'opconditions' => []
	];

	$operation_for_popup = array_merge($operation, ['id' => $operationid]);
	foreach (['opcommand_grp' => 'groupid', 'opcommand_hst' => 'hostid'] as $var => $field) {
		if (array_key_exists($var, $operation_for_popup)) {
			$operation_for_popup[$var] = zbx_objectValues($operation_for_popup[$var], $field);
		}
	}

	if (array_key_exists('update', $data['descriptions'])) {
		$data['descriptions'] = $data['descriptions']['update'];
	}

	$details_column = getActionOperationDescriptions(
		$data['action']['update_operations'], $data['eventsource'], $data['descriptions']
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
						'operationtype' => ACTION_UPDATE_OPERATION,
						'data' => $operation
					])),
				[
					(new CButton('remove', _('Remove')))
						->setAttribute('data-operationid', $i)
						->addClass('js-remove')
						->addClass(ZBX_STYLE_BTN_LINK)
						->removeId(),
					new CVar('update_operations['.$i.']', $hidden_data)
				]
			])
		))->addClass(ZBX_STYLE_NOWRAP)
	], null, 'update_operations_'.$i)->addClass(ZBX_STYLE_WORDBREAK);

	$i++;
}

$operations_table->addItem(
		(new CTag('tfoot', true))
			->addItem(
				(new CCol(
					(new CSimpleButton(_('Add')))
						->setAttribute('data-actionid', array_key_exists('actionid', $data) ? $data['actionid'] : 0)
						->setAttribute('operationtype', ACTION_UPDATE_OPERATION)
						->setAttribute('data-eventsource', array_key_exists('eventsource', $data)
							? $data['eventsource']
							: $operation['eventsource'])
						->addClass('js-update-operations-create')
						->addClass(ZBX_STYLE_BTN_LINK)
				))->setColSpan(4)
			)
	);

$operations_table->show();
