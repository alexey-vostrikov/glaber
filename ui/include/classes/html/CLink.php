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


class CLink extends CTag {

	private $csrf_token = '';
	private	$confirm_message = '';
	private $url;

	public function __construct($item = null, $url = null) {
		parent::__construct('a', true);

		if ($item !== null) {
			$this->addItem($item);
		}
		$this->url = $url;
	}

	/**
	 * Adds CSRF token into the URL.
	 * POST method will be used for '_csrf_token' argument.
	 *
	 * @param string $csrf_token  already generated CSRF token string.
	 *
	 * @return $this
	 */
	public function addCsrfToken(string $csrf_token) {
		$this->csrf_token = $csrf_token;

		return $this;
	}

	/*
	 * Add a confirmation message
	 */
	public function addConfirmation($value) {
		$this->confirm_message = $value;
		return $this;
	}

	public function setIcon(string $icon_class): self {
		$this->addClass = $icon_class;

		return $this;
	}

	/**
	 * Set URL target. If target is "_blank", add "rel" tag and tag values "noopener" and "noreferrer". The "noreferrer"
	 * depends if it is set to true in defines.inc.php.
	 *
	 * @param string $value  URL target value.
	 *
	 * @return CLink
	 */
	public function setTarget(?string $value = null): self {
		$this->setAttribute('target', $value);

		if ($value === '_blank') {
			$this->setAttribute('rel', 'noopener'.(ZBX_NOREFERER ? ' noreferrer' : ''));
		}

		return $this;
	}

	public function toString($destroy = true) {
		$url = $this->url;

		if ($url === null) {
			$this->setAttribute('role', 'button');
		}

		if ($this->csrf_token != '') {
			if (array_key_exists(ZBX_SESSION_NAME, $_COOKIE)) {
				$url .= (strpos($url, '&') !== false || strpos($url, '?') !== false) ? '&' : '?';
				$url .= CCsrfTokenHelper::CSRF_TOKEN_NAME.'='.$this->csrf_token;
			}
			$confirm_script = ($this->confirm_message !== '')
				? 'Confirm('.CHtml::encode(json_encode($this->confirm_message)).') && '
				: '';
			$this->onClick("javascript: return ".$confirm_script."redirect('".$url."', 'post', '".
				CCsrfTokenHelper::CSRF_TOKEN_NAME."', true)"
			);
			$this->setAttribute('href', 'javascript:void(0)');
		}
		else {
			$this->setAttribute('href', ($url == null) ? 'javascript:void(0)' : $url);

			if ($this->confirm_message !== '') {
				$this->onClick('javascript: return Confirm('.json_encode($this->confirm_message).');');
			}
		}

		return parent::toString($destroy);
	}


}
