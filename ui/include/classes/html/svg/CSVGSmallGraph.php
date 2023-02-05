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


class CSVGSmallGraph extends CSvgTag {

	public function __construct(array $history = [], int $max_height = 50, int $max_width = 150) {
		//adding the points to the polyline object 
		parent::__construct('svg', true);
				
		$p = [];

		if (count($history) > 0) {
			$max_clock = max(array_column($history,'clock'));
			$min_clock = min(array_column($history,'clock'));
		
			$max_value = max(array_column($history,'value'));
			$min_value = 0 ;//min(array_column($history,'value'));
	
			if ($max_value != $min_value ) {
				$k_x = $max_height/($max_value * 1.3);
			}
			else 
				$k_x = 1;

			if ($max_clock != $min_clock )
				$k_y = $max_width/($max_clock - $min_clock);
			else 
				$k_y = 1;
								
			foreach ($history as $point) {
				if (!is_numeric($point['value']))
					continue;
				$x = $max_height - ($point['value'] - $min_value) * $k_x;
				$y = ($point['clock'] - $min_clock) * $k_y;
	
				array_push($p,[$y,$x]);
			}
			
			array_push($p, [0, $max_height]);
			array_push($p, [$max_width,$max_height]);

			$this->addItem((new CSvgPolygon($p))
			  	 ->addStyle("fill:#009900; stroke:none;")
			);
			array_pop($p);
			array_pop($p);
	
			foreach ($history as $point) {
				if (!is_numeric($point['value']))
					continue;
				$x = $max_height - ($point['value'] - $min_value) * $k_x;
				$y = ($point['clock'] - $min_clock) * $k_y;
	
				$this->addItem((new CSvgCircle($y,$x,2))
					 ->addStyle("stroke:#00DD00; stroke-width:2;")
				);
			}
	
		}
		
		$this->addItem((new CSvgPolyline($p))
		  	 ->addStyle("fill:none; stroke:#00DD00; stroke-width:2;")
		);

		$this->addItem((new CSvgRect(0,0,$max_width,$max_height))
			->addStyle("fill:none; stroke:#009900; stroke-width:2;")
		);
		
		$this->setAttribute('viewBox',"0 0 ".($max_width).','.($max_height))
			 ->addStyle("stroke-width:2;")
			 ->setSize($max_width,$max_height)
			 ->setAttribute('preserveAspectRatio',"none");

	}
}
