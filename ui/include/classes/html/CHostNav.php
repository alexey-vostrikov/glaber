<?php

class CHostNav extends CList {

    /**
     * @param int $hostid
     * @param int $lld_ruleid
    * @param string $current_element
     * @param array $data
     */
    public function __construct(int $hostid, int $lld_ruleid = 0, string $current_element = "", array $data = [])
    {
        if ($data === []) {
            $data = $this->getData($hostid, $lld_ruleid);
        }

        parent::__construct($this->prepareItems($data, $current_element));
    }
    
    private function prepareItems(array $data, string $current_element): array
    {
        $items = [];

        if ($data['is_template']) {
            $template = new CSpan(
                new CLink($data['name'], 'templates.php?form=update&templateid=' . $data['templateid'])
            );

            if ($current_element === '') {
                $template->addClass(ZBX_STYLE_SELECTED);
            }

            $items[] = new CListItem(new CBreadcrumbs([
                new CSpan(new CLink(_('All templates'), new CUrl('templates.php'))),
                $template
            ]));
        } else {
            $host = new CSpan($data['name']);


            $items[] = new CListItem(new CBreadcrumbs([$host]));
            $items[] = new CListItem(new CHostStatus($data['status'], $data['maintenance_status']));
            $items[] = new CListItem(getHostAvailabilityTable($data['interfaces']));


            if ($current_element !== 'latest data'){
                $latest_link = (new CLink(_('Latest data'),
                    (new CUrl('zabbix.php'))
                        ->setArgument('action', 'latest.view')
                        ->setArgument('filter_set', '1')
                        ->setArgument('filter_hostids', [$data['hostid']])));

                $items[] = new CListItem($latest_link);
            }

            if ($data['flags'] == ZBX_FLAG_DISCOVERY_CREATED && $data['hostDiscovery']['ts_delete'] != 0) {
                $info_icons = [getHostLifetimeIndicator(time(), $data['hostDiscovery']['ts_delete'])];
                $items[] = new CListItem(makeInformationList($info_icons));
            }

            $items[] = new CHostProblemsStatus($data['hostid'], $data['problems']);
        }

        // Problems

        // Graphs

        // Web scenarios ???

//        $content_menu = (new CList())
//            ->setAttribute('role', 'navigation')
//            ->setAttribute('aria-label', _('Content menu'));
//
//        $context = $is_template ? 'template' : 'host';
//
//        /*
//         * the count of rows
//         */
//        if ($lld_ruleid == 0) {
//            // items
//            $items = new CSpan([
//                new CLink(_('Items'),
//                    (new CUrl('items.php'))
//                        ->setArgument('filter_set', '1')
//                        ->setArgument('filter_hostids', [$db_host['hostid']])
//                        ->setArgument('context', $context)
//                ),
//                CViewHelper::showNum($db_host['items'])
//            ]);
//            if ($current_element == 'items') {
//                $items->addClass(ZBX_STYLE_SELECTED);
//            }
//            $content_menu->addItem($items);
//
//            // triggers
//            $triggers = new CSpan([
//                new CLink(_('Triggers'),
//                    (new CUrl('triggers.php'))
//                        ->setArgument('filter_set', '1')
//                        ->setArgument('filter_hostids', [$db_host['hostid']])
//                        ->setArgument('context', $context)
//                ),
//                CViewHelper::showNum($db_host['triggers'])
//            ]);
//            if ($current_element == 'triggers') {
//                $triggers->addClass(ZBX_STYLE_SELECTED);
//            }
//            $content_menu->addItem($triggers);
//
//            // graphs
//            $graphs = new CSpan([
//                new CLink(_('Graphs'), (new CUrl('graphs.php'))
//                    ->setArgument('filter_set', '1')
//                    ->setArgument('filter_hostids', [$db_host['hostid']])
//                    ->setArgument('context', $context)
//                ),
//                CViewHelper::showNum($db_host['graphs'])
//            ]);
//            if ($current_element == 'graphs') {
//                $graphs->addClass(ZBX_STYLE_SELECTED);
//            }
//            $content_menu->addItem($graphs);
//
//            // Dashboards
//            if ($is_template) {
//                $dashboards = new CSpan([
//                    new CLink(_('Dashboards'),
//                        (new CUrl('zabbix.php'))
//                            ->setArgument('action', 'template.dashboard.list')
//                            ->setArgument('templateid', $db_host['hostid'])
//                    ),
//                    CViewHelper::showNum($db_host['dashboards'])
//                ]);
//                if ($current_element == 'dashboards') {
//                    $dashboards->addClass(ZBX_STYLE_SELECTED);
//                }
//                $content_menu->addItem($dashboards);
//            }
//
//            // discovery rules
//            $lld_rules = new CSpan([
//                new CLink(_('Discovery rules'), (new CUrl('host_discovery.php'))
//                    ->setArgument('filter_set', '1')
//                    ->setArgument('filter_hostids', [$db_host['hostid']])
//                    ->setArgument('context', $context)
//                ),
//                CViewHelper::showNum($db_host['discoveries'])
//            ]);
//            if ($current_element == 'discoveries') {
//                $lld_rules->addClass(ZBX_STYLE_SELECTED);
//            }
//            $content_menu->addItem($lld_rules);
//
//            // web scenarios
//            $http_tests = new CSpan([
//                new CLink(_('Web scenarios'),
//                    (new CUrl('httpconf.php'))
//                        ->setArgument('filter_set', '1')
//                        ->setArgument('filter_hostids', [$db_host['hostid']])
//                        ->setArgument('context', $context)
//                ),
//                CViewHelper::showNum($db_host['httpTests'])
//            ]);
//            if ($current_element == 'web') {
//                $http_tests->addClass(ZBX_STYLE_SELECTED);
//            }
//            $content_menu->addItem($http_tests);
//        }
//        else {
//            $discovery_rule = (new CSpan())->addItem(
//                new CLink(
//                    CHtml::encode($db_discovery_rule['name']),
//                    (new CUrl('host_discovery.php'))
//                        ->setArgument('form', 'update')
//                        ->setArgument('itemid', $db_discovery_rule['itemid'])
//                        ->setArgument('context', $context)
//                )
//            );
//
//            if ($current_element == 'discoveries') {
//                $discovery_rule->addClass(ZBX_STYLE_SELECTED);
//            }
//
//            $list->addItem(new CBreadcrumbs([
//                (new CSpan())->addItem(new CLink(_('Discovery list'),
//                    (new CUrl('host_discovery.php'))
//                        ->setArgument('filter_set', '1')
//                        ->setArgument('filter_hostids', [$db_host['hostid']])
//                        ->setArgument('context', $context)
//                )),
//                $discovery_rule
//            ]));
//
//            // item prototypes
//            $item_prototypes = new CSpan([
//                new CLink(_('Item prototypes'),
//                    (new CUrl('disc_prototypes.php'))
//                        ->setArgument('parent_discoveryid', $db_discovery_rule['itemid'])
//                        ->setArgument('context', $context)
//                ),
//                CViewHelper::showNum($db_discovery_rule['items'])
//            ]);
//            if ($current_element == 'items') {
//                $item_prototypes->addClass(ZBX_STYLE_SELECTED);
//            }
//            $content_menu->addItem($item_prototypes);
//
//            // trigger prototypes
//            $trigger_prototypes = new CSpan([
//                new CLink(_('Trigger prototypes'),
//                    (new CUrl('trigger_prototypes.php'))
//                        ->setArgument('parent_discoveryid', $db_discovery_rule['itemid'])
//                        ->setArgument('context', $context)
//                ),
//                CViewHelper::showNum($db_discovery_rule['triggers'])
//            ]);
//            if ($current_element == 'triggers') {
//                $trigger_prototypes->addClass(ZBX_STYLE_SELECTED);
//            }
//            $content_menu->addItem($trigger_prototypes);
//
//            // graph prototypes
//            $graph_prototypes = new CSpan([
//                new CLink(_('Graph prototypes'),
//                    (new CUrl('graphs.php'))
//                        ->setArgument('parent_discoveryid', $db_discovery_rule['itemid'])
//                        ->setArgument('context', $context)
//                ),
//                CViewHelper::showNum($db_discovery_rule['graphs'])
//            ]);
//            if ($current_element === 'graphs') {
//                $graph_prototypes->addClass(ZBX_STYLE_SELECTED);
//            }
//            $content_menu->addItem($graph_prototypes);
//
//            // host prototypes
//            if ($db_host['flags'] == ZBX_FLAG_DISCOVERY_NORMAL) {
//                $host_prototypes = new CSpan([
//                    new CLink(_('Host prototypes'),
//                        (new CUrl('host_prototypes.php'))
//                            ->setArgument('parent_discoveryid', $db_discovery_rule['itemid'])
//                            ->setArgument('context', $context)
//                    ),
//                    CViewHelper::showNum($db_discovery_rule['hostPrototypes'])
//                ]);
//                if ($current_element == 'hosts') {
//                    $host_prototypes->addClass(ZBX_STYLE_SELECTED);
//                }
//                $content_menu->addItem($host_prototypes);
//            }
//        }
//
//        $list->addItem($content_menu);


        return $items;
    }

    /**
     * Get data for host menu
     *
     * @param int $hostid
     * @param int $lld_ruleid
     * @return array
     * @todo Убрать в пользу получения данных снаружи, или явного вызова метода снаружи
     */
    private function getData(int $hostid, int $lld_ruleid): array
    {
        $options = [
            'output' => [
                'hostid', 'status', 'name', 'maintenance_status', 'flags', 'active_available'
            ],
            'selectHostDiscovery' => ['ts_delete'],
            'selectInterfaces' => ['type', 'useip', 'ip', 'dns', 'port', 'version', 'details', 'available', 'error'],
            'hostids' => [$hostid],
            'editable' => true
        ];
        if ($lld_ruleid == 0) {
            $options['selectItems'] = API_OUTPUT_COUNT;
            $options['selectTriggers'] = API_OUTPUT_COUNT;
            $options['selectGraphs'] = API_OUTPUT_COUNT;
            $options['selectDiscoveries'] = API_OUTPUT_COUNT;
            $options['selectHttpTests'] = API_OUTPUT_COUNT;
        }

        // get hosts
        $db_host = API::Host()->get($options);
        $is_template = false;

        if (!$db_host) {
            $options = [
                'output' => ['templateid', 'name', 'flags'],
                'templateids' => [$hostid],
                'editable' => true
            ];
            if ($lld_ruleid == 0) {
                $options['selectItems'] = API_OUTPUT_COUNT;
                $options['selectTriggers'] = API_OUTPUT_COUNT;
                $options['selectGraphs'] = API_OUTPUT_COUNT;
                $options['selectDashboards'] = API_OUTPUT_COUNT;
                $options['selectDiscoveries'] = API_OUTPUT_COUNT;
                $options['selectHttpTests'] = API_OUTPUT_COUNT;
            }

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

        // TODO: Доделать получение данных
        $db_host['problems'] = API::Problem()->get([
            'output' => ['severity'],
            'hostids' => [$hostid],
        ]);

        return $db_host;
    }
}

class CHostProblemsStatus extends CSpan
{
    public function __construct(int $hostid, array $problemsCnts)
    {
//        foreach ($problemsCnts)
//
//
//
//        $problems_link = new CLink('', (new CUrl('zabbix.php'))
//            ->setArgument('action', 'problem.view')
//            ->setArgument('filter_name', '')
//            ->setArgument('severities', $data['filter']['severities'])
//            ->setArgument('hostids', [$host['hostid']]));
//
//        $total_problem_count = 0;
//
//        // Fill the severity icons by problem count and style, and calculate the total number of problems.
//        foreach ($host['problem_count'] as $severity => $count) {
//            if (($count > 0 && $data['filter']['severities'] && in_array($severity, $data['filter']['severities']))
//                || (!$data['filter']['severities'] && $count > 0)) {
//                $total_problem_count += $count;
//
//                $problems_link->addItem((new CSpan($count))
//                    ->addClass(ZBX_STYLE_PROBLEM_ICON_LIST_ITEM)
//                    ->addClass(CSeverityHelper::getStatusStyle($severity))
//                    ->setAttribute('title', CSeverityHelper::getName($severity))
//                );
//            }
//
//        }
//
//        if ($total_problem_count == 0) {
//            $problems_link->addItem('Problems');
//        }
//        else {
//            $problems_link->addClass(ZBX_STYLE_PROBLEM_ICON_LINK);
//       }
        $hint = (new CList(["Admin menu","Items","Triggers","Graphs"]))
                ->addClass(ZBX_STYLE_HINTBOX_WRAP)
                ;

        parent::__construct();
        $this->addItem(  (new CButton(''))
                ->addClass('fa-solid fa-gear fa-xl')
               // ->sethint($hint)
                ->addClass(ZBX_STYLE_BTN_EDIT)
                ->setMenuPopup(CMenuPopupHelper::getHost($hostid,true,
                        ['allowed_ui_hosts' => 0, 'allowed_ui_problems' => 0]))
                
            );
        
    }
}
class CHostStatus extends CTag
{
    public function __construct(int $status, int $maintenance = 0)
    {
        parent::__construct('span', true);

        switch ($status) {
            case HOST_STATUS_MONITORED:
                if ($maintenance == HOST_MAINTENANCE_STATUS_ON) {
                    $this->addItem(_('In maintenance'))->addClass(ZBX_STYLE_ORANGE);
                } else {
                    $this->addItem(_('Enabled'))->addClass(ZBX_STYLE_GREEN);
                }
                break;
            case HOST_STATUS_NOT_MONITORED:
                $this->addItem(_('Disabled'))->addClass(ZBX_STYLE_RED);
                break;
            default:
                $this->addItem(_('Unknown'))->addClass(ZBX_STYLE_GREY);
                break;
        }
    }
}
