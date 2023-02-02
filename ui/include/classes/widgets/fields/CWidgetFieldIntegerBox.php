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


namespace Zabbix\Widgets\Fields;

use Zabbix\Widgets\CWidgetField;

class CWidgetFieldIntegerBox extends CWidgetField {

	private int $max;

	/**
	 * @param int $min  Minimal allowed value.
	 * @param int $max  Maximal allowed value.
	 */
	public function __construct(string $name, string $label = null, int $min = 0, int $max = ZBX_MAX_INT32) {
		parent::__construct($name, $label);

		$this->max = $max;

		$this
			->setSaveType(ZBX_WIDGET_FIELD_TYPE_INT32)
			->setExValidationRules(['in' => $min.':'.$this->max]);
	}

	public function setValue($value): self {
		return parent::setValue((int) $value);
	}

	public function getMaxLength(): int {
		return strlen((string) $this->max);
	}
}
