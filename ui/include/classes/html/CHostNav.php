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

/**
 * Host navigation element.
 *
 * Usage:
 *
 * $widget->setNavigation(new CHostNav(CHostNav::getData($hostid)));
 *
 */
class CHostNav extends CList {
    /**
     * @param array $data
     */
    public function __construct(array $data = [])
    {
        $items = [];

        if ($data['is_template']) {
            $template = new CSpan(
                new CLink($data['name'], 'templates.php?form=update&templateid=' . $data['templateid'])
            );

            $items[] = new CListItem(new CBreadcrumbs([
                new CSpan(new CLink(_('All templates'), new CUrl('templates.php'))),
                $template
            ]));
        } else {
            $items[] = new CListItem($this->getHostLink($data));
            $items[] = new CListItem(new CHostStatus($data['status'], $data['maintenance_status']));
            $items[] = new CListItem(getHostAvailabilityTable($data['interfaces']));

            if ($data['flags'] == ZBX_FLAG_DISCOVERY_CREATED && $data['hostDiscovery']['ts_delete'] != 0) {
                $info_icons = [getHostLifetimeIndicator(time(), $data['hostDiscovery']['ts_delete'])];
                $items[] = new CListItem(makeInformationList($info_icons));
            }

            $items[] = new CListItem($this->getLatestDataLink($data['hostid']));
            $items[] = new CListItem(new CHostProblemsStatus($data['hostid'], $data['problems']));
            $items[] = new CListItem($this->getGraphsLink($data['hostid']));

            if ($data['dashboards']) {
                $items[] = new CListItem($this->getDashboardsLink($data['hostid']));
            }else{
                $items[] = new CListItem(
                    (new CSpan(_('Dashboards')))->addClass("grey")
                );
            }

            if ($data['httpTests']) {
                $items[] = new CListItem($this->getWebLink($data['hostid']));
            }else{
                $items[] = new CListItem(
                    (new CSpan(_('Web')))->addClass("grey")
                );
            }

        }

        parent::__construct($items);
    }
    
    /**
     * Get data for host menu
     *
     * @param int $hostid
     * @param int $lld_ruleid
     * @return array
     * @todo Убрать в пользу получения данных снаружи, или явного вызова метода снаружи
     */
    public static function getData(int $hostid, int $lld_ruleid = 0): array
    {
        $options = [
            'output' => [
                'hostid', 'status', 'name', 'maintenance_status', 'flags', 'active_available'
            ],
            'selectHostDiscovery' => ['ts_delete'],
            'selectHttpTests' => API_OUTPUT_COUNT,
            'selectInterfaces' => ['type', 'useip', 'ip', 'dns', 'port', 'version', 'details', 'available', 'error'],
            'hostids' => [$hostid],
            'editable' => true
        ];

        // get hosts
        $db_host = API::Host()->get($options);
        $is_template = false;

        if (!$db_host) {
            $options = [
                'output' => ['templateid', 'name', 'flags'],
                'templateids' => [$hostid],
                'editable' => true
            ];

            // get templates
            $db_host = API::Template()->get($options);

            if ($db_host) {
                $db_host['hostid'] = $db_host['templateid'];
                $is_template = true;
            }
        }

        if (!$db_host) {
            return [];
        }

        $db_host = reset($db_host);

        if (!$is_template) {
            // Get count for item type ITEM_TYPE_ZABBIX_ACTIVE (7).
            $db_item_active_count = API::Item()->get([
                'countOutput' => true,
                'filter' => ['type' => ITEM_TYPE_ZABBIX_ACTIVE],
                'hostids' => [$hostid]
            ]);

            if ($db_item_active_count > 0) {
                // Add active checks interface if host have items with type ITEM_TYPE_ZABBIX_ACTIVE (7).
                $db_host['interfaces'][] = [
                    'type' => INTERFACE_TYPE_AGENT_ACTIVE,
                    'available' => $db_host['active_available'],
                    'error' => ''
                ];
                unset($db_host['active_available']);
            }
        }

        // get lld-rules
        if ($lld_ruleid != 0) {
            $db_discovery_rule = API::DiscoveryRule()->get([
                'output' => ['name'],
                'selectItems' => API_OUTPUT_COUNT,
                'selectTriggers' => API_OUTPUT_COUNT,
                'selectGraphs' => API_OUTPUT_COUNT,
                'selectHostPrototypes' => API_OUTPUT_COUNT,
                'itemids' => [$lld_ruleid],
                'editable' => true
            ]);
            $db_host['db_discovery_rule'] = reset($db_discovery_rule);
        }

        $db_host['is_template'] = $is_template;

        $db_host['problems'] = API::Problem()->get([
            'output' => ['severity'],
            'hostids' => [$hostid],
        ]);

        $db_host['dashboards'] = count(getHostDashboards($hostid));

        return $db_host;
    }

    /**
     * @param array $data
     * @return CSpan
     */
    private function getHostLink(array $data): CSpan
    {
        $menu = CMenuPopupHelper::getHostAdmin($data['hostid']);
        $hostname = (new CSpan($data['name']))->addClass(ZBX_STYLE_LINK_ACTION);
        return (new CSpan($hostname))->setMenuPopup($menu);
    }

    /**
     * @param $hostid
     * @return CLink
     */
    private function getLatestDataLink($hostid): CLink
    {
        return (new CLink(_('Latest data'),
            (new CUrl('zabbix.php'))
                ->setArgument('action', 'latest.view')
                ->setArgument('filter_set', '1')
                ->setArgument('filter_hostids', [$hostid])));
    }

    /**
     * @param $hostid
     * @return CLink
     */
    private function getGraphsLink($hostid): CLink
    {
        return new CLink(_('Graphs'),
            (new CUrl('zabbix.php'))
                ->setArgument('action', 'charts.view')
                ->setArgument('filter_hostids', (array)$hostid)
                ->setArgument('filter_show', GRAPH_FILTER_HOST)
                ->setArgument('filter_set', '1')
        );
    }

    /**
     * @param $hostid
     * @return CLink
     */
    private function getDashboardsLink($hostid): CLink
    {
        return new CLink(_('Dashboards'),
            (new CUrl('zabbix.php'))
                ->setArgument('action', 'host.dashboard.view')
                ->setArgument('hostid', $hostid)
        );
    }

    /**
     * @param $hostid
     * @return CLink
     */
    private function getWebLink($hostid): CLink
    {
        return new CLink(_('Web'),
            (new CUrl('zabbix.php'))
                ->setArgument('action', 'web.view')
                ->setArgument('filter_set', '1')
                ->setArgument('filter_hostids', (array)$hostid)
        );
    }
}