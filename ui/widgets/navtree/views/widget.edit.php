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
 * Map navigation tree widget form view.
 *
 * @var CView $this
 * @var array $data
 */

use Zabbix\Widgets\Fields\CWidgetFieldReference;

$form = (new CWidgetFormView($data))
	->addFieldVar($data['fields'][CWidgetFieldReference::FIELD_NAME]);

// Add dynamically created fields navtree.name.<N>, navtree.parent.<N>, navtree.order.<N> and navtree.sysmapid.<N>.
foreach ($data['fields']['navtree']->getValue() as $i => $navtree_item) {
	$form->addVar($data['fields']['navtree']->getName().'.name.'.$i, $navtree_item['name']);

	if ($navtree_item['order'] != 1) {
		$form->addVar($data['fields']['navtree']->getName().'.order.'.$i, $navtree_item['order']);
	}

	if ($navtree_item['parent'] != 0) {
		$form->addVar($data['fields']['navtree']->getName().'.parent.'.$i, $navtree_item['parent']);
	}

	if (array_key_exists('sysmapid', $navtree_item)) {
		$form->addVar($data['fields']['navtree']->getName().'.sysmapid.'.$i, $navtree_item['sysmapid']);
	}
}

$form
	->addField(new CWidgetFieldCheckBoxView($data['fields']['show_unavailable']))
	->show();
