<?php
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

class CHostProblemsStatus extends CDiv
{
    public function __construct(int $hostId, array $problems)
    {
        parent::__construct();

        $this->addItem($this->createLinkIcon($hostId, _('Problems').' ', ZBX_SEVERITY_OK));

        $icons = (new CDiv(''))->addClass(ZBX_STYLE_PROBLEM_ICON_LIST);

        if (count($problems) == 0) {
            $icons->addItem($this->createLinkIcon($hostId, '0', ZBX_SEVERITY_OK));
        }else{
            $counts = $this->calcCounts($problems);
            foreach ($counts as $severity => $cnt) {
                $icons->addItem($this->createLinkIcon($hostId, "$cnt", $severity));
            }
        }

        $this->addItem($icons);
    }

    private function createLinkIcon($hostId, string $text, int $severity): CLink
    {
        $url = (new CUrl('zabbix.php'))
            ->setArgument('action', 'problem.view')
            ->setArgument('filter_name', '')
            ->setArgument('hostids', [$hostId]);

        if ($severity != ZBX_SEVERITY_OK) {
            $url->setArgument('severities', [$severity => $severity]);
        }

        $link = (new CLink($text, $url))
            ->addClass(ZBX_STYLE_PROBLEM_ICON_LIST_ITEM)
            ->addClass(CSeverityHelper::getStatusStyle($severity));

        if ($severity != ZBX_SEVERITY_OK) {
            $link->setAttribute('title', CSeverityHelper::getName($severity));
        }

        return $link;
    }

    private function calcCounts(array $problems): array
    {
        $counts = [];
        foreach ($problems as $problem) {
            if (array_key_exists($problem['severity'], $counts)) {
                $counts[$problem['severity']]++;
            } else {
                $counts[$problem['severity']] = 1;
            }
        }
        return $counts;
    }
}