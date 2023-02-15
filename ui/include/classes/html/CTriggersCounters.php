<?php declare(strict_types = 0);
/*
** Copyright (C) 2001-2023 Glaber
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


class CTriggersCounters extends CSpan {
    public function __construct(array &$severities) {
        parent::__construct();
        
        for ($i = TRIGGER_SEVERITY_COUNT-1; $i >= 0; $i--) {
            if (0 == $severities[$i])
                continue;
       
            $this->addItem((new CSpan($severities[$i]))
                ->addClass(ZBX_STYLE_PROBLEM_ICON_LIST_ITEM)
                ->addClass(CSeverityHelper::getStatusStyle($i))
                ->setAttribute('title', CSeverityHelper::getName($i))
            );
        }
    }
}