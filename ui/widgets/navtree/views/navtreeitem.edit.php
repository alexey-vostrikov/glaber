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
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**/


/**
 * Map navigation tree item edit form view.
 *
 * @var CView $this
 * @var array $data
 */

use Widgets\NavTree\Widget;

$form = (new CForm('post'))
	->setId('widget-dialogue-form')
	->setName('widget_dialogue_form');

// Enable form submitting on Enter.
$form->addItem((new CSubmitButton(null))->addClass(ZBX_STYLE_FORM_SUBMIT_HIDDEN));

$multiselect = (new CMultiSelect([
	'name' => 'sysmapid',
	'object_name' => 'sysmaps',
	'multiple' => false,
	'data' => $data['sysmap'] ? [$data['sysmap']] : [],
	'add_post_js' => false,
	'popup' => [
		'parameters' => [
			'srctbl' => 'sysmaps',
			'srcfld1' => 'sysmapid',
			'dstfrm' => $form->getName(),
			'dstfld1' => 'sysmapid'
		]
	]
]))->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH);

$form_grid = (new CFormGrid())
	->addItem([
		(new CLabel(_('Name'), 'name'))->setAsteriskMark(),
		new CFormField(
			(new CTextBox('name', $data['name']))
				->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH)
				->setAttribute('autofocus', 'autofocus')
				->setAriaRequired()
		)
	])
	->addItem([
		new CLabel(_('Linked map'), 'sysmapid_ms'),
		new CFormField($multiselect)
	]);

if ($data['depth'] >= Widget::MAX_DEPTH) {
	$form_grid->addItem([
		null,
		new CFormField(_('Cannot add submaps. Max depth reached.'))
	]);
}
else {
	$form_grid->addItem([
		null,
		new CFormField([
			new CCheckBox('add_submaps', 1),
			new CLabel(_('Add submaps'), 'add_submaps')
		])
	]);
}

$form
	->addItem($form_grid)
	->addItem(
		(new CScriptTag('navtreeitem_edit_popup.init();'))->setOnDocumentReady()
	);

$output = [
	'body' => $form->toString(),
	'script_inline' => $multiselect->getPostJs().
		$this->readJsFile('navtreeitem.edit.js.php', null, '')
];

if ($messages = get_and_clear_messages()) {
	$output['messages'] = array_column($messages, 'message');
}

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo json_encode($output, JSON_THROW_ON_ERROR);
