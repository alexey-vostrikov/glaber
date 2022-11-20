<?php declare(strict_types = 1);
/*
** Glaber
** Copyright (C) 2001-2038 Glaber
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
/*
	The class is to make object context's links general an unified across the system
	for exmple 
	make a "short" hosts links
 	array $links = getLinks(data['host'],'host', 'compact');
 	this specifically might be uses by modules to provide 
 	module-specific host link output, however for unification it might be a good
 	practice to use this as a general mechaning for all objects context link 
 	generation
*/
	 
class CContextLinksHelper {

	private $link_handlers = [];
	
	public function getLinks(array &$data, $obj_type, $context) {
		if (CWebUser::isGuest() || 
			!isset($this->link_handlers[$obj_type]) || 
			!isset($this->link_handlers[$obj_type][$context]))
			
			return [];
		
		$link_funcs = $this->link_handlers[$obj_type][$context];
		$links = [];

		foreach ($link_funcs as $link_func) {
			$link = $link_func($data, $obj_type, $context);
			if (isset($link))
				$links[] = $link;
		}

		return $links;
	}

	public function addLinksHandler($obj_type, $context, $order, $handler_func) {
		
		if (!isset($this->link_handlers[$obj_type]))
			$this->link_handlers[$obj_type] =[];
		
		if (!isset($this->link_handlers[$obj_type][$context]))
			$this->link_handlers[$obj_type][$context] =[];
		
		$this->link_handlers[$obj_type][$context][$order] = $handler_func;
		
		asort($this->link_handlers[$obj_type][$context], SORT_NUMERIC);
	}

	public function getLinksHandler() {
		return $this;
	}
}
