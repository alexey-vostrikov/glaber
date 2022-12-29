<?php declare(strict_types = 0);
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
 * @var CView $this
 * @var array $data
 */

$url = (new CUrl('zabbix.php'))
	->setArgument('action', ($data['tokenid'] == 0) ? 'token.create' : 'token.update')
	->getUrl();

$token_form = (new CForm('post', $url))
	->setId('token_form')
	->setName('token')
	->addVar('admin_mode', $data['admin_mode'])
	->addVar('tokenid', $data['tokenid'])
	->addItem((new CInput('submit', null))->addStyle('display: none;'));

if ($data['admin_mode'] === '0') {
	$token_form->addVar('userid', CWebUser::$data['userid']);
}

$token_from_grid = (new CFormGrid())->addItem([
	(new CLabel(_('Name'), 'name'))->setAsteriskMark(),
	new CFormField(
		(new CTextBox(
			'name',
			$data['name'],
			false,
			DB::getFieldLength('token', 'name'
			)))
			->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
			->setAttribute('autofocus', 'autofocus')
			->setAriaRequired()
	)
]);

if ($data['admin_mode'] === '1') {
	$token_from_grid->addItem([
		(new CLabel(_('User'), 'userid_ms'))->setAsteriskMark(),
		new CFormField(
			(new CMultiSelect([
				'disabled' => ($data['tokenid'] != 0),
				'multiple' => false,
				'name' => 'userid',
				'object_name' => 'users',
				'data' => $data['ms_user'],
				'placeholder' => '',
				'popup' => [
					'parameters' => [
						'srctbl' => 'users',
						'srcfld1' => 'userid',
						'srcfld2' => 'fullname',
						'dstfrm' => $token_form->getName(),
						'dstfld1' => 'userid'
					]
				]
			]))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
		)
	]);
}

$token_from_grid->addItem([
		new CLabel(_('Description'), 'description'),
		new CFormField(
			(new CTextArea('description', $data['description']))
				->addClass(ZBX_STYLE_MONOSPACE_FONT)
				->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
				->setMaxLength(DB::getFieldLength('token', 'description'))
				->setAriaRequired()
		)
	])
	->addItem([
		new CLabel(_('Set expiration date and time'), 'expires_state'),
		new CFormField(
			(new CCheckBox('expires_state', '1'))
				->setChecked($data['expires_state'])
				->setUncheckedValue('0')
		)
	])
	->addItem([
		(new CLabel(_('Expires at'), 'expires_at'))->setAsteriskMark(),
		new CFormField(
			(new CDateSelector('expires_at', $data['expires_at']))
				->setDateFormat(DATE_TIME_FORMAT_SECONDS)
				->setPlaceholder(_('YYYY-MM-DD hh:mm:ss'))
				->setAriaRequired()
				->setId('expires-at-row')
		)
	])
	->addItem([
		new CLabel(_('Enabled'), 'status'),
		new CFormField(
			(new CCheckBox('status', ZBX_AUTH_TOKEN_ENABLED))->setChecked($data['status'] == ZBX_AUTH_TOKEN_ENABLED)
		)
	]);

$token_form->addItem($token_from_grid);

$data['form_name'] = 'token_form';

if ($data['tokenid'] != 0) {
	$buttons = [
		[
			'title' => _('Update'),
			'class' => '',
			'keepOpen' => true,
			'isSubmit' => true,
			'action' => 'token_edit_popup.submit();'
		],
		[
			'title' => _('Regenerate'),
			'confirmation' => _('Regenerate selected API token? Previously generated token will become invalid.'),
			'class' => 'btn-alt',
			'keepOpen' => true,
			'isSubmit' => false,
			'action' => 'token_edit_popup.regenerate();'
		],
		[
			'title' => _('Delete'),
			'confirmation' => _('Delete selected API token?'),
			'class' => 'btn-alt',
			'keepOpen' => true,
			'isSubmit' => false,
			'action' => 'token_edit_popup.delete('.json_encode($data['tokenid']).');'
		]
	];
}
else {
	$buttons = [
		[
			'title' => _('Add'),
			'class' => '',
			'keepOpen' => true,
			'isSubmit' => true,
			'action' => 'token_edit_popup.submit();'
		]
	];
}

$output = [
	'header' =>($data['tokenid'] == 0) ? _('New API token') : ('API token'),
	'doc_url' => CDocHelper::getUrl(CDocHelper::POPUP_TOKEN_EDIT),
	'body' => $token_form->toString(),
	'script_inline' => getPagePostJs().
		$this->readJsFile('popup.token.edit.js.php').
		'token_edit_popup.init();',
	'buttons' => $buttons
];

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo json_encode($output);
