<?php
/*
** Zabbix
** Copyright (C) 2001-2018 Zabbix SIA
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


$itemWidget = (new CWidget())->setTitle(_('Items'));

if ($data['hostid'] != 0) {
	$itemWidget->addItem(get_header_host_table('items', $data['hostid']));
}

// create form
$itemForm = (new CForm())
	->setName('itemForm')
	->setAttribute('aria-labeledby', ZBX_STYLE_PAGE_TITLE)
	->addVar('group_itemid', $data['itemids'])
	->addVar('hostid', $data['hostid'])
	->addVar('action', $data['action']);

// create form list
$itemFormList = new CFormList('itemFormList');

// append type to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[type]', 'type', _('Original')))
		->setLabel(_('Type'))
		->setChecked(isset($data['visible']['type']))
		->setAttribute('autofocus', 'autofocus'),
	new CComboBox('type', $data['type'], null, $data['itemTypes'])
);

// append hosts to form list
if ($data['displayInterfaces']) {
	$interfacesComboBox = new CComboBox('interfaceid', $data['interfaceid']);
	$interfacesComboBox->addItem(new CComboItem(0, '', false, false));

	// Set up interface groups sorted by priority.
	$interface_types = zbx_objectValues($data['hosts']['interfaces'], 'type');
	$interface_groups = [];
	foreach ([INTERFACE_TYPE_AGENT, INTERFACE_TYPE_SNMP, INTERFACE_TYPE_JMX, INTERFACE_TYPE_IPMI] as $interface_type) {
		if (in_array($interface_type, $interface_types)) {
			$interface_groups[$interface_type] = new COptGroup(interfaceType2str($interface_type));
		}
	}

	// add interfaces to groups
	foreach ($data['hosts']['interfaces'] as $interface) {
		$option = new CComboItem(
			$interface['interfaceid'],
			$interface['useip']
				? $interface['ip'].' : '.$interface['port']
				: $interface['dns'].' : '.$interface['port'],
			($interface['interfaceid'] == $data['interfaceid'])
		);
		$option->setAttribute('data-interfacetype', $interface['type']);
		$interface_groups[$interface['type']]->addItem($option);
	}
	foreach ($interface_groups as $interface_group) {
		$interfacesComboBox->addItem($interface_group);
	}

	$span = (new CSpan(_('No interface found')))
		->addClass(ZBX_STYLE_RED)
		->setId('interface_not_defined')
		->setAttribute('style', 'display: none;');

	$itemFormList->addRow(
		(new CVisibilityBox('visible[interfaceid]', 'interfaceDiv', _('Original')))
			->setLabel(_('Host interface'))
			->setChecked(isset($data['visible']['interfaceid']))
			->setAttribute('data-multiple-interface-types', $data['multiple_interface_types']),
		(new CDiv([$interfacesComboBox, $span]))->setId('interfaceDiv'),
		'interface_row'
	);
	$itemForm->addVar('selectedInterfaceId', $data['interfaceid']);
}

// append jmx endpoint to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[jmx_endpoint]', 'jmx_endpoint', _('Original')))
		->setLabel(_('JMX endpoint'))
		->setChecked(array_key_exists('jmx_endpoint', $data['visible'])),
	(new CTextBox('jmx_endpoint', $data['jmx_endpoint']))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// ITEM_TYPE_HTTPAGENT URL field.
$itemFormList->addRow(
	(new CVisibilityBox('visible[url]', 'url', _('Original')))
		->setLabel(_('URL'))
		->setChecked(array_key_exists('url', $data['visible'])),
	(new CTextBox('url', $data['url'], false, DB::getFieldLength('items', 'url')))
		->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// ITEM_TYPE_HTTPAGENT Request body type.
$itemFormList->addRow(
	(new CVisibilityBox('visible[post_type]', 'post_type_container', _('Original')))
		->setLabel(_('Request body type'))
		->setChecked(array_key_exists('post_type', $data['visible'])),
	(new CDiv(
		(new CRadioButtonList('post_type', (int) $data['post_type']))
			->addValue(_('Raw data'), ZBX_POSTTYPE_RAW)
			->addValue(_('JSON data'), ZBX_POSTTYPE_JSON)
			->addValue(_('XML data'), ZBX_POSTTYPE_XML)
			->setModern(true)
	))->setId('post_type_container')
);

// ITEM_TYPE_HTTPAGENT Request body.
$itemFormList->addRow(
	(new CVisibilityBox('visible[posts]', 'posts', _('Original')))
		->setLabel(_('Request body'))
		->setChecked(array_key_exists('posts', $data['visible'])),
	(new CTextArea('posts', $data['posts']))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// ITEM_TYPE_HTTPAGENT Headers fields.
$headers_data = [];

if (is_array($data['headers']) && $data['headers']) {
	foreach ($data['headers'] as $pair) {
		$headers_data[] = ['name' => key($pair), 'value' => reset($pair)];
	}
}
else {
	$headers_data[] = ['name' => '', 'value' => ''];
}
$headers = (new CTag('script', true))->setAttribute('type', 'text/json');
$headers->items = [CJs::encodeJson($headers_data)];

$itemFormList->addRow(
	(new CVisibilityBox('visible[headers]', 'headers_pairs', _('Original')))
		->setLabel(_('Headers'))
		->setChecked(array_key_exists('headers', $data['visible'])),
	(new CDiv([
		(new CTable())
			->setAttribute('style', 'width: 100%;')
			->setHeader(['', _('Name'), '', _('Value'), ''])
			->addRow((new CRow)->setAttribute('data-insert-point', 'append'))
			->setFooter(new CRow(
				(new CCol(
					(new CButton(null, _('Add')))
						->addClass(ZBX_STYLE_BTN_LINK)
						->setAttribute('data-row-action', 'add_row')
				))->setColSpan(5)
			)),
		(new CTag('script', true))
			->setAttribute('type', 'text/x-jquery-tmpl')
			->addItem(new CRow([
				(new CCol((new CDiv)->addClass(ZBX_STYLE_DRAG_ICON)))->addClass(ZBX_STYLE_TD_DRAG_ICON),
				(new CTextBox('headers[name][#{index}]', '#{name}'))->setWidth(ZBX_TEXTAREA_TAG_WIDTH),
				'&rArr;',
				(new CTextBox('headers[value][#{index}]', '#{value}'))->setWidth(ZBX_TEXTAREA_TAG_WIDTH),
				(new CButton(null, _('Remove')))
					->addClass(ZBX_STYLE_BTN_LINK)
					->setAttribute('data-row-action', 'remove_row')
			])),
		$headers
	]))
		->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
		->setId('headers_pairs')
		->setAttribute('data-sortable-pairs-table', '1')
		->setAttribute('style', 'min-width: '.ZBX_TEXTAREA_BIG_WIDTH.'px;')
);

// append snmp community to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[snmp_community]', 'snmp_community', _('Original')))
		->setLabel(_('SNMP community'))
		->setChecked(isset($data['visible']['snmp_community'])),
	(new CTextBox('snmp_community', $data['snmp_community']))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// append snmpv3 contextname to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[snmpv3_contextname]', 'snmpv3_contextname', _('Original')))
		->setLabel(_('Context name'))
		->setChecked(isset($data['visible']['snmpv3_contextname'])),
	(new CTextBox('snmpv3_contextname', $data['snmpv3_contextname']))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// append snmpv3 securityname to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[snmpv3_securityname]', 'snmpv3_securityname', _('Original')))
		->setLabel(_('Security name'))
		->setChecked(isset($data['visible']['snmpv3_securityname'])),
	(new CTextBox('snmpv3_securityname', $data['snmpv3_securityname']))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// append snmpv3 securitylevel to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[snmpv3_securitylevel]', 'snmpv3_securitylevel', _('Original')))
		->setLabel(_('Security level'))
		->setChecked(isset($data['visible']['snmpv3_securitylevel'])),
	new CComboBox('snmpv3_securitylevel', $data['snmpv3_securitylevel'], null, [
		ITEM_SNMPV3_SECURITYLEVEL_NOAUTHNOPRIV => 'noAuthNoPriv',
		ITEM_SNMPV3_SECURITYLEVEL_AUTHNOPRIV => 'authNoPriv',
		ITEM_SNMPV3_SECURITYLEVEL_AUTHPRIV => 'authPriv'
	])
);

// append snmpv3 authprotocol to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[snmpv3_authprotocol]', 'authprotocol_div', _('Original')))
		->setLabel(_('Authentication protocol'))
		->setChecked(isset($data['visible']['snmpv3_authprotocol'])),
	(new CDiv(
		(new CRadioButtonList('snmpv3_authprotocol', (int) $data['snmpv3_authprotocol']))
			->addValue(_('MD5'), ITEM_AUTHPROTOCOL_MD5)
			->addValue(_('SHA'), ITEM_AUTHPROTOCOL_SHA)
			->setModern(true)
	))->setId('authprotocol_div')
);

// append snmpv3 authpassphrase to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[snmpv3_authpassphrase]', 'snmpv3_authpassphrase', _('Original')))
		->setLabel(_('Authentication passphrase'))
		->setChecked(isset($data['visible']['snmpv3_authpassphrase'])),
	(new CTextBox('snmpv3_authpassphrase', $data['snmpv3_authpassphrase']))
		->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// append snmpv3 privprotocol to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[snmpv3_privprotocol]', 'privprotocol_div', _('Original')))
		->setLabel(_('Privacy protocol'))
		->setChecked(isset($data['visible']['snmpv3_privprotocol'])),
	(new CDiv(
		(new CRadioButtonList('snmpv3_privprotocol', (int) $data['snmpv3_privprotocol']))
			->addValue(_('DES'), ITEM_PRIVPROTOCOL_DES)
			->addValue(_('AES'), ITEM_PRIVPROTOCOL_AES)
			->setModern(true)
	))
		->setId('privprotocol_div')
);

// append snmpv3 privpassphrase to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[snmpv3_privpassphrase]', 'snmpv3_privpassphrase', _('Original')))
		->setLabel(_('Privacy passphrase'))
		->setChecked(isset($data['visible']['snmpv3_privpassphrase'])),
	(new CTextBox('snmpv3_privpassphrase', $data['snmpv3_privpassphrase']))
		->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// append port to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[port]', 'port', _('Original')))
		->setLabel(_('Port'))
		->setChecked(isset($data['visible']['port'])),
	(new CTextBox('port', $data['port']))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
);

// append value type to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[value_type]', 'value_type', _('Original')))
		->setLabel(_('Type of information'))
		->setChecked(isset($data['visible']['value_type'])),
	new CComboBox('value_type', $data['value_type'], null, [
		ITEM_VALUE_TYPE_UINT64 => _('Numeric (unsigned)'),
		ITEM_VALUE_TYPE_FLOAT => _('Numeric (float)'),
		ITEM_VALUE_TYPE_STR => _('Character'),
		ITEM_VALUE_TYPE_LOG => _('Log'),
		ITEM_VALUE_TYPE_TEXT => _('Text')
	])
);

// append units to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[units]', 'units', _('Original')))
		->setLabel(_('Units'))
		->setChecked(isset($data['visible']['units'])),
	(new CTextBox('units', $data['units']))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// append authtype to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[authtype]', 'authtype', _('Original')))
		->setLabel(_('Authentication method'))
		->setChecked(isset($data['visible']['authtype'])),
	new CComboBox('authtype', $data['authtype'], null, [
		ITEM_AUTHTYPE_PASSWORD => _('Password'),
		ITEM_AUTHTYPE_PUBLICKEY => _('Public key')
	])
);

// append username to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[username]', 'username', _('Original')))
		->setLabel(_('User name'))
		->setChecked(isset($data['visible']['username'])),
	(new CTextBox('username', $data['username']))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
);

// append publickey to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[publickey]', 'publickey', _('Original')))
		->setLabel(_('Public key file'))
		->setChecked(isset($data['visible']['publickey'])),
	(new CTextBox('publickey', $data['publickey']))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
);

// append privatekey to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[privatekey]', 'privatekey', _('Original')))
		->setLabel(_('Private key file'))
		->setChecked(isset($data['visible']['privatekey'])),
	(new CTextBox('privatekey', $data['privatekey']))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
);

// append password
$itemFormList->addRow(
	(new CVisibilityBox('visible[password]', 'password', _('Original')))
		->setLabel(_('Password'))
		->setChecked(isset($data['visible']['password'])),
	(new CTextBox('password', $data['password']))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
);

// append item pre-processing
$preprocessing = (new CTable())
	->setId('preprocessing')
	->setHeader([
		'',
		new CColHeader(_('Name')),
		new CColHeader(_('Parameters')),
		new CColHeader(null),
		(new CColHeader(_('Action')))->setWidth(50)
	]);

foreach ($data['preprocessing'] as $i => $step) {
	// Depeding on preprocessing type, display corresponding params field and placeholders.
	$params = [];

	// Use numeric box for multiplier, otherwise use text box.
	if ($step['type'] == ZBX_PREPROC_MULTIPLIER) {
		$params[] = (new CTextBox('preprocessing['.$i.'][params][0]',
			array_key_exists('params', $step) ? $step['params'][0] : ''
		))->setAttribute('placeholder', _('number'));
	}
	else {
		$params[] = new CTextBox('preprocessing['.$i.'][params][0]',
			array_key_exists('params', $step) ? $step['params'][0] : ''
		);
	}

	// Create a secondary param text box, so it can be hidden if necessary.
	$params[] = (new CTextBox('preprocessing['.$i.'][params][1]',
		(array_key_exists('params', $step) && array_key_exists(1, $step['params']))
			? $step['params'][1]
			: ''
	))->setAttribute('placeholder', _('output'));

	// Add corresponding placeholders and show or hide text boxes.
	switch ($step['type']) {
		case ZBX_PREPROC_MULTIPLIER:
			$params[1]->addStyle('display: none;');
			break;

		case ZBX_PREPROC_RTRIM:
		case ZBX_PREPROC_LTRIM:
		case ZBX_PREPROC_TRIM:
			$params[0]->setAttribute('placeholder', _('list of characters'));
			$params[1]->addStyle('display: none;');
			break;

		case ZBX_PREPROC_XPATH:
		case ZBX_PREPROC_JSONPATH:
			$params[0]->setAttribute('placeholder', _('path'));
			$params[1]->addStyle('display: none;');
			break;

		case ZBX_PREPROC_REGSUB:
			$params[0]->setAttribute('placeholder', _('pattern'));
			break;

		case ZBX_PREPROC_BOOL2DEC:
		case ZBX_PREPROC_OCT2DEC:
		case ZBX_PREPROC_HEX2DEC:
		case ZBX_PREPROC_DELTA_VALUE:
		case ZBX_PREPROC_DELTA_SPEED:
			$params[0]->addStyle('display: none;');
			$params[1]->addStyle('display: none;');
			break;
	}

	$preproc_types_cbbox = new CComboBox('preprocessing['.$i.'][type]', $step['type']);

	foreach (get_preprocessing_types() as $group) {
		$cb_group = new COptGroup($group['label']);

		foreach ($group['types'] as $type => $label) {
			$cb_group->addItem(new CComboItem($type, $label, ($type == $step['type'])));
		}

		$preproc_types_cbbox->addItem($cb_group);
	}

	$preprocessing->addRow(
		(new CRow([
			(new CCol((new CDiv())->addClass(ZBX_STYLE_DRAG_ICON)))->addClass(ZBX_STYLE_TD_DRAG_ICON),
			$preproc_types_cbbox,
			$params[0],
			$params[1],
			(new CButton('preprocessing['.$i.'][remove]', _('Remove')))
				->addClass(ZBX_STYLE_BTN_LINK)
				->addClass('element-table-remove')
		]))->addClass('sortable')
	);
}

$preprocessing->addRow(
	(new CCol(
		(new CButton('param_add', _('Add')))
			->addClass(ZBX_STYLE_BTN_LINK)
			->addClass('element-table-add')
	))->setColSpan(5)
);

$itemFormList->addRow(
	(new CVisibilityBox('visible[preprocessing]', 'preprocessing_div', _('Original')))
		->setLabel(_('Preprocessing steps'))
		->setChecked(isset($data['visible']['preprocessing'])),
	(new CDiv($preprocessing))
		->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
		->setAttribute('style', 'min-width: '.ZBX_TEXTAREA_STANDARD_WIDTH.'px;')
		->setId('preprocessing_div')
);

$update_interval = (new CTable())
	->setId('update_interval')
	->addRow([_('Delay'),
		(new CDiv((new CTextBox('delay', $data['delay']))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)))
	]);

$custom_intervals = (new CTable())
	->setId('custom_intervals')
	->setHeader([
		new CColHeader(_('Type')),
		new CColHeader(_('Interval')),
		new CColHeader(_('Period')),
		(new CColHeader(_('Action')))->setWidth(50)
	])
	->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
	->setAttribute('style', 'min-width: '.ZBX_TEXTAREA_STANDARD_WIDTH.'px;');

foreach ($data['delay_flex'] as $i => $delay_flex) {
	$type_input = (new CRadioButtonList('delay_flex['.$i.'][type]', (int) $delay_flex['type']))
		->addValue(_('Flexible'), ITEM_DELAY_FLEXIBLE)
		->addValue(_('Scheduling'), ITEM_DELAY_SCHEDULING)
		->setModern(true);

	if ($delay_flex['type'] == ITEM_DELAY_FLEXIBLE) {
		$delay_input = (new CTextBox('delay_flex['.$i.'][delay]', $delay_flex['delay']))
			->setAttribute('placeholder', ZBX_ITEM_FLEXIBLE_DELAY_DEFAULT);
		$period_input = (new CTextBox('delay_flex['.$i.'][period]', $delay_flex['period']))
			->setAttribute('placeholder', ZBX_DEFAULT_INTERVAL);
		$schedule_input = (new CTextBox('delay_flex['.$i.'][schedule]'))
			->setAttribute('placeholder', ZBX_ITEM_SCHEDULING_DEFAULT)
			->setAttribute('style', 'display: none;');
	}
	else {
		$delay_input = (new CTextBox('delay_flex['.$i.'][delay]'))
			->setAttribute('placeholder', ZBX_ITEM_FLEXIBLE_DELAY_DEFAULT)
			->setAttribute('style', 'display: none;');
		$period_input = (new CTextBox('delay_flex['.$i.'][period]'))
			->setAttribute('placeholder', ZBX_DEFAULT_INTERVAL)
			->setAttribute('style', 'display: none;');
		$schedule_input = (new CTextBox('delay_flex['.$i.'][schedule]', $delay_flex['schedule']))
			->setAttribute('placeholder', ZBX_ITEM_SCHEDULING_DEFAULT);
	}

	$button = (new CButton('delay_flex['.$i.'][remove]', _('Remove')))
		->addClass(ZBX_STYLE_BTN_LINK)
		->addClass('element-table-remove');

	$custom_intervals->addRow([$type_input, [$delay_input, $schedule_input], $period_input, $button], 'form_row');
}

$custom_intervals->addRow([(new CButton('interval_add', _('Add')))
	->addClass(ZBX_STYLE_BTN_LINK)
	->addClass('element-table-add')]);

$update_interval->addRow(
	(new CRow([
		(new CCol(_('Custom intervals')))->setAttribute('style', 'vertical-align: top;'),
		new CCol($custom_intervals)
	]))
);

// append delay to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[delay]', 'update_interval_div', _('Original')))
		->setLabel(_('Update interval'))
		->setChecked(isset($data['visible']['delay'])),
	(new CDiv($update_interval))->setId('update_interval_div')
);

$itemFormList
	->addRow(
		(new CVisibilityBox('visible[history]', 'history', _('Original')))
			->setLabel(_('History storage period'))
			->setChecked(isset($data['visible']['history'])),
		(new CTextBox('history', $data['history']))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
	)
	->addRow(
		(new CVisibilityBox('visible[trends]', 'trends', _('Original')))
			->setLabel(_('Trend storage period'))
			->setChecked(isset($data['visible']['trends'])),
		(new CTextBox('trends', $data['trends']))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
	);

// append status to form list
$statusComboBox = new CComboBox('status', $data['status']);
foreach ([ITEM_STATUS_ACTIVE, ITEM_STATUS_DISABLED] as $status) {
	$statusComboBox->addItem($status, item_status2str($status));
}
$itemFormList->addRow(
	(new CVisibilityBox('visible[status]', 'status', _('Original')))
		->setLabel(_('Status'))
		->setChecked(isset($data['visible']['status'])),
	$statusComboBox
);

// append logtime to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[logtimefmt]', 'logtimefmt', _('Original')))
		->setLabel(_('Log time format'))
		->setChecked(isset($data['visible']['logtimefmt'])),
	(new CTextBox('logtimefmt', $data['logtimefmt']))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// append valuemap to form list
$valueMapsComboBox = new CComboBox('valuemapid', $data['valuemapid']);
$valueMapsComboBox->addItem(0, _('As is'));
foreach ($data['valuemaps'] as $valuemap) {
	$valueMapsComboBox->addItem($valuemap['valuemapid'], $valuemap['name']);
}
$valueMapLink = (new CLink(_('show value mappings'), 'adm.valuemapping.php'))
	->setAttribute('target', '_blank');

$itemFormList
	->addRow(
		(new CVisibilityBox('visible[valuemapid]', 'valuemap', _('Original')))
			->setLabel(_('Show value'))
			->setChecked(isset($data['visible']['valuemapid'])),
		(new CDiv([$valueMapsComboBox, SPACE, $valueMapLink]))
			->setId('valuemap')
	)
	->addRow(
		(new CVisibilityBox('visible[allow_traps]', 'allow_traps', _('Original')))
			->setLabel(_('Enable trapping'))
			->setChecked(array_key_exists('allow_traps', $data['visible'])),
		(new CCheckBox('allow_traps', HTTPCHECK_ALLOW_TRAPS_ON))
			->setChecked($data['allow_traps'] == HTTPCHECK_ALLOW_TRAPS_ON)
	)
	->addRow(
		(new CVisibilityBox('visible[trapper_hosts]', 'trapper_hosts', _('Original')))
			->setLabel(_('Allowed hosts'))
			->setChecked(array_key_exists('trapper_hosts', $data['visible'])),
		(new CTextBox('trapper_hosts', $data['trapper_hosts']))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
	);

// append applications to form list
if ($data['displayApplications']) {
	// replace applications
	$app_to_replace = hasRequest('applications')
		? CArrayHelper::renameObjectsKeys(API::Application()->get([
			'output' => ['applicationid', 'name'],
			'applicationids' => getRequest('applications')
		]), ['applicationid' => 'id'])
		: [];

	$replace_app = (new CDiv(
		(new CMultiSelect([
			'name' => 'applications[]',
			'object_name' => 'applications',
			'data' => $app_to_replace,
			'popup' => [
				'parameters' => [
					'srctbl' => 'applications',
					'srcfld1' => 'applicationid',
					'dstfrm' => $itemForm->getName(),
					'dstfld1' => 'applications_',
					'hostid' => $data['hostid'],
					'noempty' => true
				]
			]
		]))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
	))->setId('replaceApp');

	$itemFormList->addRow(
		(new CVisibilityBox('visible[applications]', 'replaceApp', _('Original')))
			->setLabel(_('Replace applications'))
			->setChecked(isset($data['visible']['applications'])),
		$replace_app
	);

	// add new or existing applications
	$applications_to_add = [];
	if (hasRequest('new_applications')) {
		$applicationids = [];

		foreach (getRequest('new_applications') as $newApplication) {
			if (is_array($newApplication) && isset($newApplication['new'])) {
				$applications_to_add[] = [
					'id' => $newApplication['new'],
					'name' => $newApplication['new'].' ('._x('new', 'new element in multiselect').')',
					'isNew' => true
				];
			}
			else {
				$applicationids[] = $newApplication;
			}
		}

		$applications_to_add = array_merge($applications_to_add, $applicationids
			? CArrayHelper::renameObjectsKeys(API::Application()->get([
				'output' => ['applicationid', 'name'],
				'applicationids' => $applicationids
			]), ['applicationid' => 'id'])
			: []);
	}

	$newApp = (new CDiv(
		(new CMultiSelect([
			'name' => 'new_applications[]',
			'object_name' => 'applications',
			'add_new' => true,
			'data' => $applications_to_add,
			'popup' => [
				'parameters' => [
					'srctbl' => 'applications',
					'srcfld1' => 'applicationid',
					'dstfrm' => $itemForm->getName(),
					'dstfld1' => 'new_applications_',
					'hostid' => $data['hostid'],
					'noempty' => true
				]
			]
		]))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
	))->setId('newApp');

	$itemFormList->addRow(
		(new CVisibilityBox('visible[new_applications]', 'newApp', _('Original')))
			->setLabel(_('Add new or existing applications'))
			->setChecked(isset($data['visible']['new_applications'])),
		$newApp
	);
}

// Append master item select.
if ($data['displayMasteritems']) {
	$master_item = (new CDiv([
		(new CMultiSelect([
			'name' => 'master_itemid',
			'object_name' => 'items',
			'multiple' => false,
			'data' => ($data['master_itemid'] != 0)
				? [
					[
						'id' => $data['master_itemid'],
						'prefix' => $data['master_hostname'].NAME_DELIMITER,
						'name' => $data['master_itemname']
					]
				]
				: [],
			'popup' => [
				'parameters' => [
					'srctbl' => 'items',
					'srcfld1' => 'itemid',
					'dstfrm' => $itemForm->getName(),
					'dstfld1' => 'master_itemid',
					'hostid' => $data['hostid'],
					'excludeids' => $data['itemids'],
					'webitems' => true
				]
			]
		]))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAriaRequired(true)
	]))->setId('master_item');

	$itemFormList->addRow(
		(new CVisibilityBox('visible[master_itemid]', 'master_item', _('Original')))
			->setLabel(_('Master item'))
			->setChecked(array_key_exists('master_itemid', $data['visible'])),
		$master_item
	);
}

// append description to form list
$itemFormList->addRow(
	(new CVisibilityBox('visible[description]', 'description', _('Original')))
		->setLabel(_('Description'))
		->setChecked(isset($data['visible']['description'])),
	(new CTextArea('description', $data['description']))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);
// append tabs to form
$itemTab = new CTabView();
$itemTab->addTab('itemTab', _('Mass update'), $itemFormList);

// append buttons to form
$itemTab->setFooter(makeFormFooter(
	new CSubmit('massupdate', _('Update')),
	[new CButtonCancel(url_param('hostid'))]
));

$itemForm->addItem($itemTab);
$itemWidget->addItem($itemForm);

require_once dirname(__FILE__).'/js/configuration.item.massupdate.js.php';

return $itemWidget;
