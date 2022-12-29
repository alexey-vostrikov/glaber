<?php declare(strict_types = 0);
/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

use CTimezoneHelper;

class CWidgetFieldTimeZone extends CWidgetFieldSelect {

	public const DEFAULT_VALUE = '';

	public function __construct(string $name, string $label = null, array $values = null) {
		parent::__construct($name, $label, $values === null
			? [
				ZBX_DEFAULT_TIMEZONE => CTimezoneHelper::getTitle(CTimezoneHelper::getSystemTimezone(),
					_('System default')
				),
				TIMEZONE_DEFAULT_LOCAL => _('Local default')
			] + CTimezoneHelper::getList()
			: null
		);

		$this
			->setDefault(self::DEFAULT_VALUE)
			->setSaveType(ZBX_WIDGET_FIELD_TYPE_STR);
	}

	public function setValue($value): self {
		$this->value = $value;

		return $this;
	}
}
