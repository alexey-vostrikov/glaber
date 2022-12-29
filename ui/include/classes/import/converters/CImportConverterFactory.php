<?php
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


/**
 * Factory for creating import conversions.
 */
final class CImportConverterFactory extends CRegistryFactory {

	private const SEQUENTIAL_CONVERTERS = [
		'1.0' => 'C10ImportConverter',
		'2.0' => 'C20ImportConverter',
		'3.0' => 'C30ImportConverter',
		'3.2' => 'C32ImportConverter',
		'3.4' => 'C34ImportConverter',
		'4.0' => 'C40ImportConverter',
		'4.2' => 'C42ImportConverter',
		'4.4' => 'C44ImportConverter',
		'5.0' => 'C50ImportConverter',
		'5.2' => 'C52ImportConverter',
		'5.4' => 'C54ImportConverter',
		'6.0' => 'C60ImportConverter',
		'6.2' => 'C62ImportConverter'
	];

	public function __construct() {
		parent::__construct(self::SEQUENTIAL_CONVERTERS);
	}

	public static function getSequentialVersions() {
		return array_keys(self::SEQUENTIAL_CONVERTERS);
	}
}
