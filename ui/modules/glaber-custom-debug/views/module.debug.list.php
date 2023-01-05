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
 * @var CView $this
 */

$page = (new CHtmlPage())
	->setTitle(('Set Debug objects '))
	->addItem( (new CForm('get'))
		->addItem((new CFormList())->addRow(('Item ID'),
				(new CTextBox('itemid', isset($data['itemid'])?$data['itemid']:'0'))
					->setWidth(ZBX_TEXTAREA_FILTER_SMALL_WIDTH)
					->setAttribute('autofocus', 'autofocus')
			))
		->addItem((new CFormList())->addRow(('Trigger ID'),
				(new CTextBox('triggerid', isset($data['triggerid'])?$data['triggerid']:'0'))
					->setWidth(ZBX_TEXTAREA_FILTER_SMALL_WIDTH)
		))
		->addVar('action', 'module.debug.list')
		->addVar('apply_new','1')
		->addItem((new CFormList())->addRow((''), (new CSubmitButton('Apply'))))
	);

$page->show();
