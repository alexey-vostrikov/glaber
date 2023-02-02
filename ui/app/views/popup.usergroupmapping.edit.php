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
 * @var CView $this
 * @var array $data
 */

$form_action = (new CUrl('zabbix.php'))
	->setArgument('action', 'popup.usergroupmapping.check')
	->getUrl();

$form = (new CForm('post', $form_action))
	->cleanItems()
	->setId('user-group-mapping-edit-form')
	->setName('user-group-mapping-edit-form')
	->addItem(
		(new CInput('submit', 'submit'))
			->addStyle('display: none;')
			->removeId()
	);

$usergroup_multiselect = (new CMultiSelect([
	'name' => 'user_groups[]',
	'object_name' => 'usersGroups',
	'multiple' => true,
	'data' => $data['user_groups'],
	'popup' => [
		'parameters' => [
			'srctbl' => 'usrgrp',
			'srcfld1' => 'usrgrpid',
			'srcfld2' => 'name',
			'dstfrm' => $form->getName(),
			'dstfld1' => 'user_groups'
		]
	]
]))
	->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH)
	->setId('user_groups');
$inline_js = $usergroup_multiselect->getPostJS();

$user_role_multiselect = (new CMultiSelect([
	'name' => 'roleid',
	'object_name' => 'roles',
	'data' => $data['user_role'],
	'multiple' => false,
	'popup' => [
		'parameters' => [
			'srctbl' => 'roles',
			'srcfld1' => 'roleid',
			'dstfrm' => $form->getName(),
			'dstfld1' => 'roleid'
		]
	]
]))
	->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH)
	->setId('roleid');
$inline_js .= $user_role_multiselect->getPostJS();

$source = $data['idp_type'] == IDP_TYPE_SAML ? _('SAML') : _('LDAP');

$name_hint_icon = makeHelpIcon([
	_('Naming requirements:'),
	(new CList([
		_s('group name must match %1$s group name', $source),
		_("wildcard patterns with '*' may be used")
	]))->addClass(ZBX_STYLE_LIST_DASHED)
])->addClass(ZBX_STYLE_LIST_DASHED);

$form
	->addItem((new CFormGrid())
		->addItem([
			(new CLabel([_s('%1$s group pattern', $source), $name_hint_icon], 'name'))->setAsteriskMark(),
			new CFormField(
				(new CTextBox('name', $data['name']))
					->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH)
					->setAttribute('autofocus', 'autofocus')
					->setAriaRequired()
			)
		])
		->addItem([
			(new CLabel(_('User groups'), 'user_groups__ms'))->setAsteriskMark(),
			new CFormField($usergroup_multiselect)
		])
		->addItem([
			(new CLabel(_('User role'), 'roleid_ms'))->setAsteriskMark(),
			new CFormField($user_role_multiselect)
		])
	)
	->addItem(
		(new CScriptTag('
			user_group_mapping_edit_popup.init();
		'))->setOnDocumentReady()
	);

if ($data['add_group']) {
	$title = _('New user group mapping');
	$buttons = [
		[
			'title' => _('Add'),
			'class' => 'js-add',
			'keepOpen' => true,
			'isSubmit' => true,
			'action' => 'user_group_mapping_edit_popup.submit();'
		]
	];
}
else {
	$title = _('User group mapping');
	$buttons = [
		[
			'title' => _('Update'),
			'class' => 'js-update',
			'keepOpen' => true,
			'isSubmit' => true,
			'action' => 'user_group_mapping_edit_popup.submit();'
		]
	];
}

$output = [
	'header' => $title,
	'script_inline' => $inline_js . $this->readJsFile('popup.usergroupmapping.edit.js.php'),
	'body' => $form->toString(),
	'buttons' => $buttons
];

if (($messages = getMessages()) !== null) {
	$output['errors'] = $messages->toString();
}

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo json_encode($output);
