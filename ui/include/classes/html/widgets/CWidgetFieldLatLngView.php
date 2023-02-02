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


use Zabbix\Widgets\Fields\CWidgetFieldLatLng;

class CWidgetFieldLatLngView extends CWidgetFieldView {

	private string $placeholder = '';

	private int $width = ZBX_TEXTAREA_MEDIUM_WIDTH;

	public function __construct(CWidgetFieldLatLng $field) {
		$this->field = $field;

		$this->setHelpHint([
			_('Comma separated center coordinates and zoom level to display when the widget is initially loaded.'),
			BR(),
			_('Supported formats:'),
			(new CList([
				new CListItem((new CSpan('<lat>,<lng>,<zoom>'))->addClass(ZBX_STYLE_MONOSPACE_FONT)),
				new CListItem((new CSpan('<lat>,<lng>'))->addClass(ZBX_STYLE_MONOSPACE_FONT))
			]))->addClass(ZBX_STYLE_LIST_DASHED),
			BR(),
			_s('The maximum zoom level is "%1$s".', CSettingsHelper::get(CSettingsHelper::GEOMAPS_MAX_ZOOM)),
			BR(),
			_('Initial view is ignored if the default view is set.')
		]);
	}

	public function setPlaceholder(string $placeholder): self {
		$this->placeholder = $placeholder;

		return $this;
	}

	public function setWidth(int $width): self {
		$this->width = $width;

		return $this;
	}

	public function getView(): CTextBox {
		$textbox = (new CTextBox($this->field->getName(), $this->field->getValue()))
			->setWidth($this->width)
			->setEnabled(!$this->isDisabled())
			->setAriaRequired($this->isRequired());

		if ($this->placeholder !== '') {
			$textbox = $textbox->setAttribute('placeholder', $this->placeholder);
		}

		return $textbox;
	}
}
