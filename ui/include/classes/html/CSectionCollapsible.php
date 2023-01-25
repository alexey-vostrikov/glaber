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


class CSectionCollapsible extends CSection {

	private const ZBX_STYLE_COLLAPSED = 'section-collapsed';
	private const ZBX_STYLE_TOGGLE = 'section-toggle';

	private bool $is_expanded = true;
	private string $profile_key = '';

	public function setExpanded(bool $is_expanded): self {
		$this->is_expanded = $is_expanded;

		return $this;
	}

	public function setProfileIdx(string $profile_key): self {
		$this->profile_key = $profile_key;

		return $this;
	}

	public function toString($destroy = true): string {
		$this->addClass($this->is_expanded ? null : self::ZBX_STYLE_COLLAPSED);

		$toggle = (new CSimpleButton())
			->addClass(self::ZBX_STYLE_TOGGLE)
			->setTitle($this->is_expanded ? _('Collapse') : _('Expand'))
			->onClick('toggleSection("'.$this->getId().'", "'.$this->profile_key.'");');

		if ($this->header === null) {
			$this->setHeader($toggle);
		}
		else {
			$this->header->addItem($toggle);
		}

		return parent::toString($destroy);
	}
}
