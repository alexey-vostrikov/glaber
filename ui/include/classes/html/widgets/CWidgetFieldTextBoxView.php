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


use Zabbix\Widgets\Fields\CWidgetFieldTextBox;

class CWidgetFieldTextBoxView extends CWidgetFieldView {

	private string $placeholder = '';

	private int $width = ZBX_TEXTAREA_STANDARD_WIDTH;
	private ?int $adaptive_width = null;

	public function __construct(CWidgetFieldTextBox $field) {
		$this->field = $field;
	}

	public function setPlaceholder(string $placeholder): self {
		$this->placeholder = $placeholder;

		return $this;
	}

	public function setWidth(int $width): self {
		$this->width = $width;

		return $this;
	}

	public function setAdaptiveWidth(int $adaptive_width): self {
		$this->adaptive_width = $adaptive_width;

		return $this;
	}

	public function getView(): CTextBox {
		$textbox = (new CTextBox($this->field->getName(), $this->field->getValue()))
			->setEnabled(!$this->isDisabled())
			->setAriaRequired($this->isRequired());

		if ($this->placeholder !== '') {
			$textbox = $textbox->setAttribute('placeholder', $this->placeholder);
		}

		if ($this->adaptive_width !== null) {
			$textbox->setAdaptiveWidth($this->adaptive_width);
		}
		else {
			$textbox->setWidth($this->width);
		}

		return $textbox;
	}
}
