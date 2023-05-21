<?php
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


class CScreenHistory extends CScreenBase
{

    /**
     * @var int Type of graph to display.
     *
     * Supported values:
     * - GRAPH_TYPE_NORMAL
     * - GRAPH_TYPE_STACKED
     */
    protected $graphType;
    /** @var string Search string */
    public $filter;
    /** @var int Filter show/hide */
    public $filterTask;
    /** @var string Filter highlight color */
    public $markColor;
    /** @var boolean Is plain text displayed */
    public $plaintext;
    /** @var array Items ids. */
    public $itemids;
    /** @var int Graph id. */
    public $graphid = 0;
    /** @var string String containing base URL for pager. */
    public $page_file;

    /**
     * Init screen data.
     *
     * @param array $options
     * @param string $options ['filter']
     * @param int $options ['filterTask']
     * @param int $options ['markColor']
     * @param boolean $options ['plaintext']
     * @param array $options ['itemids']
     * @param array $options ['graphid']     When set defines graph id where item.
     * @param string $options ['pageFile']    Current page file, is used for pagination links.
     */
    public function __construct(array $options = [])
    {
        parent::__construct($options);

        $this->resourcetype = SCREEN_RESOURCE_HISTORY;

        // mandatory
        $this->filter = isset($options['filter']) ? $options['filter'] : '';
        $this->filterTask = isset($options['filter_task']) ? $options['filter_task'] : null;
        $this->markColor = isset($options['mark_color']) ? $options['mark_color'] : MARK_COLOR_RED;
        $this->graphType = isset($options['graphtype']) ? $options['graphtype'] : GRAPH_TYPE_NORMAL;

        // optional
        $this->itemids = array_key_exists('itemids', $options) ? $options['itemids'] : [];
        $this->plaintext = isset($options['plaintext']) ? $options['plaintext'] : false;
        $this->page_file = array_key_exists('pageFile', $options) ? $options['pageFile'] : null;
        //for proper timeticker work we need to set differnt dataIds to all
        //different screens. Now there are two screens poss


        $this->dataId = isset($options['screenid']) ? $options['screenid'] : 'historyGraph';

        if (!$this->itemids && array_key_exists('graphid', $options)) {
            $itemids = API::Item()->get([
                'output' => ['itemid'],
                'graphids' => [$options['graphid']]
            ]);
            $this->itemids = array_column($itemids, 'itemid');
            $this->graphid = $options['graphid'];
        }
    }

    /**
     * Process screen.
     *
     * @return array|string
     * @throws Exception
     */
    public function get()
    {
        $output = [];

        $items = API::Item()->get([
            'output' => ['itemid', 'hostid', 'name', 'key_', 'value_type', 'history', 'trends'],
            'selectHosts' => ['name'],
            'selectValueMap' => ['mappings'],
            'itemids' => $this->itemids,
            'webitems' => true,
            'preservekeys' => true
        ]);

        if (!$items) {
            show_error_message(_('No permissions to referred object or it does not exist!'));

            return;
        }

        if ($this->action == HISTORY_VALUES) {
            $options = [
                'output' => API_OUTPUT_EXTEND,
                'sortfield' => ['clock'],
                'sortorder' => ZBX_SORT_DOWN,
                'time_from' => $this->timeline['from_ts'],
                'time_till' => $this->timeline['to_ts'],
                'limit' => CSettingsHelper::get(CSettingsHelper::SEARCH_LIMIT)
            ];

            $numeric_items = !$this->hasStringItems($items);

            /**
             * View type: As plain text.
             * Item type: numeric (unsigned, char), float, text, log.
             */
            if ($this->plaintext) {
                return $this->getOutput(
                    $this->getPlainTextView($items, $options),
                    false
                );
            } /**
             * View type: Values
             * Item type: text, log
             */
            elseif (!$numeric_items) {
                $output[] = $this->getTextItemsView($items, $options);
            } /**
             * View type: Values.
             * Item type: numeric (unsigned, char), float.
             */
            else {
                $output[] = $this->getScalarValuesView($items, $options);
            }
        }


        // time control
		if (str_in_array($this->action, [HISTORY_GRAPH, HISTORY_BATCH_GRAPH])) {
            list($elem, $js) = $this->getGraphView($items, [], $this->mode);

            if ($this->mode == SCREEN_MODE_JS) {
                return $js;
            }

            $output[] = $elem;
            zbx_add_post_js($js);
        }

        if ($this->mode != SCREEN_MODE_JS) {
            $flickerfreeData = [
                'itemids' => $this->itemids,
                'action' => ($this->action == HISTORY_BATCH_GRAPH) ? HISTORY_GRAPH : $this->action,
                'filter' => $this->filter,
                'filterTask' => $this->filterTask,
                'markColor' => $this->markColor
            ];

            if ($this->action == HISTORY_VALUES) {
                $flickerfreeData['page'] = $this->page;
            }

            if ($this->graphid != 0) {
                unset($flickerfreeData['itemids']);
                $flickerfreeData['graphid'] = $this->graphid;
            }

            return $this->getOutput($output, true, $flickerfreeData);
        }

        return $output;
    }

    /**
     * Does filtering of the data is filter is set
     *
     * @param array $history_data
     * @param array $filter
     * @param int $filterTask
     *
     *
     */
    protected function filterDataArray(array &$history_data = null)
    {

        if ($this->filterTask != FILTER_TASK_HIDE &&
            $this->filterTask != FILTER_TASK_SHOW &&
            $this->filterTask != FILTER_TASK_INVERT_MARK &&
            $this->filterTask != FILTER_TASK_MARK)
            return;

        if ($this->filter == '')
            return;

        if (null == $history_data)
            return;

        $needle = mb_strtolower($this->filter);

        foreach ($history_data as $key => $data) {

            $haystack = mb_strtolower($data['value']);
            $pos = mb_strpos($haystack, $needle);
            $color = null;

            if ($pos !== false && $this->filterTask == FILTER_TASK_MARK) {
                $color = $this->markColor;
            } elseif ($pos === false && $this->filterTask == FILTER_TASK_INVERT_MARK) {
                $color = $this->markColor;
            } elseif ($pos !== false && $this->filterTask == FILTER_TASK_HIDE) {
                unset($history_data[$key]);
                continue;
            }
            if ($pos == false && $this->filterTask == FILTER_TASK_SHOW) {
                unset($history_data[$key]);
                continue;
            }

            switch ($color) {
                case MARK_COLOR_RED:
                    $history_data[$key]['color'] = ZBX_STYLE_RED;
                    break;
                case MARK_COLOR_GREEN:
                    $history_data[$key]['color'] = ZBX_STYLE_GREEN;
                    break;
                case MARK_COLOR_BLUE:
                    $history_data[$key]['color'] = ZBX_STYLE_BLUE;
                    break;
            }
        }
    }

    /**
     * Return the URL for the graph.
     *
     * @param array $itemIds
     *
     * @return string
     */
    protected function getGraphUrl(array $itemIds)
    {
        $url = (new CUrl('chart.php'))
            ->setArgument('from', $this->timeline['from'])
            ->setArgument('to', $this->timeline['to'])
            ->setArgument('itemids', $itemIds)
            ->setArgument('type', $this->graphType)
            ->setArgument('profileIdx', $this->profileIdx)
            ->setArgument('profileIdx2', $this->profileIdx2);

        if ($this->action == HISTORY_BATCH_GRAPH) {
            $url->setArgument('batch', 1);
        }

        return $url->getUrl();
    }

    /**
     * Checks the array of items for the presence of string items
     * @param array $items
     * @return bool
     */
    public function hasStringItems(array $items): bool
    {
        $iv_string = [
            ITEM_VALUE_TYPE_LOG => 1,
            ITEM_VALUE_TYPE_TEXT => 1,
            ITEM_VALUE_TYPE_STR => 1
        ];

        foreach ($items as $item) {
            if (array_key_exists($item['value_type'], $iv_string)) {
                return false;
            }
        }

        return true;
    }

    /**
     * @param array $items
     * @return bool
     */
    public function hasLogs(array $items): bool
    {
        foreach ($items as $item) {
            if (ITEM_VALUE_TYPE_LOG === $item['value_type']) {
                return true;
            }
        }

        return false;
    }

    /**
     * @param array $options
     * @param array $items
     * @param string $sort_order
     * @return array
     * @throws Exception
     */
    public function getData(array $items, array $options, string $sort_order): array
    {
        if ($this->filter !== '' && in_array($this->filterTask, [FILTER_TASK_SHOW, FILTER_TASK_HIDE])) {
            $options['search'] = ['value' => $this->filter];
            if ($this->filterTask == FILTER_TASK_HIDE) {
                $options['excludeSearch'] = true;
            }
        }

        $history_data = [];
        $items_by_type = [];

        foreach ($items as $item) {
            $items_by_type[$item['value_type']][] = $item['itemid'];
        }

        foreach ($items_by_type as $value_type => $itemids) {
            $options['history'] = $value_type;
            $options['itemids'] = $itemids;

            $item_data = API::History()->get($options);

            if ($item_data) {
                $history_data = array_merge($history_data, $item_data);
            }
        }

        CArrayHelper::sort($history_data, [
            ['field' => 'clock', 'order' => $sort_order],
            ['field' => 'ns', 'order' => $sort_order]
        ]);


        return array_key_exists('limit', $options)
            ? array_slice($history_data, 0, $options['limit'])
            : $history_data;
    }

    /**
     * @param array $options
     * @param array $items
     * @return CTag
     * @throws Exception
     */
    public function getPlainTextView(array $items, array $options): CTag
    {
        //if there are logs, we'll sort output showvalues data in ascending order
        $has_logs = $this->hasLogs($items);
        $is_many_items = (count($items) > 1);

        $pre = new CPre();
        $history_data = $this->getData($items, $options, $has_logs ? ZBX_SORT_UP : ZBX_SORT_DOWN);

        foreach ($history_data as $row) {
            $value = $row['value'];
            $item = $items[$row['itemid']];

            if (in_array($item['value_type'], [ITEM_VALUE_TYPE_LOG, ITEM_VALUE_TYPE_STR, ITEM_VALUE_TYPE_TEXT])) {
                $value = '"' . $value . '"';
            } elseif ($item['value_type'] == ITEM_VALUE_TYPE_FLOAT) {
                $value = formatFloat($value, null, ZBX_UNITS_ROUNDOFF_UNSUFFIXED);
            }

            $row = zbx_date2str(DATE_TIME_FORMAT_SECONDS, $row['clock']) . ' ' . $row['clock'] . ' ' . $value;

            if ($is_many_items) {
                // TODO: Разобраться почему нет name_expanded
                $name_expanded = array_key_exists('name_expanded', $item) ? $item['name_expanded'] : $item['name'];
                $row .= ' "' . $item['hosts'][0]['name'] . NAME_DELIMITER . $name_expanded . '"';
            }
            $pre->addItem([$row, BR()]);
        }

        return $pre;
    }

    /**
     * @param array $items
     * @param array $options
     * @return CTag
     * @throws Exception
     */
    public function getTextItemsView(array $items, array $options): CTag
    {
        //if there are logs, we'll sort output showvalues data in ascending order
        $has_logs = $this->hasLogs($items);
        $is_many_items = (count($items) > 1);

        $history_data = $this->getData($items, $options, $has_logs ? ZBX_SORT_UP : ZBX_SORT_DOWN);

        $history_table = (new CDataTable('logview'))
            ->setHeader([
                (new CColHeader(_('Timestamp')))->addClass(ZBX_STYLE_CELL_WIDTH)->addClass('search'),
                $is_many_items ? _('Item') : null,
                $has_logs ? (new CColHeader(_('Local time')))->addClass(ZBX_STYLE_CELL_WIDTH)->addClass('search') : null,
                $has_logs ? (new CColHeader(_('Source')))->addClass(ZBX_STYLE_CELL_WIDTH) : null,
                $has_logs ? (new CColHeader(_('Severity')))->addClass(ZBX_STYLE_CELL_WIDTH) : null,
                $has_logs ? (new CColHeader(_('Event ID')))->addClass(ZBX_STYLE_CELL_WIDTH) : null,
                (new CColHeader(_('Value')))->addClass('search')
            ]);

        foreach ($history_data as $data) {
            $data['value'] = rtrim($data['value'], " \t\r\n");

            if (!isset($data['severity']))
                $severity = TRIGGER_SEVERITY_UNDEFINED;
            else
                $severity = $data['severity'];

            if (isset($data['logeventid']) && isset($data['source'])
                && $data['logeventid'] > 0) {
                $cell_clock = (new CCol(new CLink(zbx_date2str(DATE_TIME_FORMAT_SECONDS, $data['clock']),
                    (new CUrl('tr_events.php'))
                        ->setArgument('triggerid', $data['source'])
                        ->setArgument('eventid', $data['logeventid'])
                )))->addClass(ZBX_STYLE_NOWRAP)
                    ->setAttribute('data-order', $data['clock']);

            } else {
                $cell_clock = (new CCol(zbx_date2str(DATE_TIME_FORMAT_SECONDS, $data['clock'])))
                    ->addClass(ZBX_STYLE_NOWRAP)
                    ->setAttribute('data-order', $data['clock']);
            }

            $item = $items[$data['itemid']];
            $host = reset($item['hosts']);

            isset($data['color'])
                ? $color = $data['color']
                : $color = null;

            $row = [];

            $row[] = $cell_clock;

            if ($is_many_items) {
                $row[] = (new CCol($host['name'] . NAME_DELIMITER . $item['name']))
                    ->addClass($color);
            }

            if ($has_logs) {
                $row[] = (array_key_exists('timestamp', $data) && $data['timestamp'] != 0)
                    ? (new CCol(zbx_date2str(DATE_TIME_FORMAT_SECONDS, $data['timestamp'])))
                        ->addClass(ZBX_STYLE_NOWRAP)
                        ->addClass($color)
                    : '';
                $row[] = array_key_exists('source', $data)
                    ? (new CCol($data['source']))
                        ->addClass(ZBX_STYLE_NOWRAP)
                        ->addClass($color)
                    : '';
                $row[] = (array_key_exists('severity', $data) && $data['severity'] != 0)
                    ? (new CCol(get_item_logtype_description($data['severity'])))
                        ->addClass(ZBX_STYLE_NOWRAP)
                        ->addClass(get_item_logtype_style($data['severity']))
                    : '';
                $row[] = array_key_exists('severity', $data)
                    ? (new CCol($data['logeventid']))
                        ->addClass(ZBX_STYLE_NOWRAP)
                        ->addClass($color)
                    : '';
            }

            $row[] = (new CCol(new CPre(zbx_nl2br($data['value']))))->addClass(CSeverityHelper::getStatusStyle($severity));

            $history_table->addRow($row);
        }

        return $history_table;
    }

    /**
     * @param array $items
     * @param array $options
     * @return CTag
     * @throws Exception
     */
    public function getScalarValuesView(array $items, array $options): CTag
    {
        CArrayHelper::sort($items, [['field' => 'name_expanded', 'order' => ZBX_SORT_UP]]);

        $table_header = [(new CColHeader(_('Timestamp')))->addClass(ZBX_STYLE_CELL_WIDTH)];
        $history_data = [];

        // TODO: разделить получение данных и формирование таблицы
        foreach ($items as $item) {
            $options['itemids'] = [$item['itemid']];
            $options['history'] = $item['value_type'];
            $item_data = API::History()->get($options);

            $this->filterDataArray($item_data);

            CArrayHelper::sort($item_data, [
                ['field' => 'clock', 'order' => ZBX_SORT_DOWN],
                ['field' => 'ns', 'order' => ZBX_SORT_DOWN]
            ]);

            $table_header[] = (new CColHeader($item['name']))
                ->addClass('vertical_rotation')
                ->setTitle($item['name']);
            $history_data_index = 0;

            foreach ($item_data as $item_data_row) {
                // Searching for starting 'insert before' index in results array.
                while (array_key_exists($history_data_index, $history_data)) {
                    $history_row = $history_data[$history_data_index];

                    if ($history_row['clock'] <= $item_data_row['clock']
                        && !array_key_exists($item['itemid'], $history_row['values'])) {
                        break;
                    }

                    ++$history_data_index;
                }

                if (array_key_exists($history_data_index, $history_data)
                    && !array_key_exists($item['itemid'], $history_row['values'])
                    && $history_data[$history_data_index]['clock'] === $item_data_row['clock']) {
                    $history_data[$history_data_index]['values'][$item['itemid']] = $item_data_row['value'];
                } else {
                    array_splice($history_data, $history_data_index, 0, [[
                        'clock' => $item_data_row['clock'],
                        'values' => [$item['itemid'] => $item_data_row['value']]
                    ]]);
                }
            }
        }

        $history_table = (new CTableInfo())->makeVerticalRotation()->setHeader($table_header);

        foreach ($history_data as $history_data_row) {
            $row = [(new CCol(zbx_date2str(DATE_TIME_FORMAT_SECONDS, $history_data_row['clock'])))
                ->addClass(ZBX_STYLE_NOWRAP)
            ];
            $values = $history_data_row['values'];

            foreach ($items as $item) {
                $value = array_key_exists($item['itemid'], $values) ? $values[$item['itemid']] : '';

                if ($item['value_type'] == ITEM_VALUE_TYPE_FLOAT && $value !== '') {
                    $value = formatFloat($value, null, ZBX_UNITS_ROUNDOFF_UNSUFFIXED);
                }

                $value = CValueMapHelper::applyValueMap($item['value_type'], $value, $item['valuemap']);

                $row[] = ($value === '') ? '' : new CPre($value);
            }

            $history_table->addRow($row);
        }

        return $history_table;
    }

    /**
     * @param array $items
     * @param array $options
     * @param int $mode Display mode
     * @return array
     */
    public function getGraphView(array $items, array $options, int $mode = SCREEN_MODE_SLIDESHOW): array {
        $timeControlData = [
            'id' => $this->getDataId(),
        ];

        $containerId = 'graph_cont1';
        $timeControlData['containerid'] = $containerId;
        $timeControlData['src'] = $this->getGraphUrl(array_column($items, 'itemid', 'itemid'));
        $timeControlData['objDims'] = getGraphDims();
        $timeControlData['loadSBox'] = 1;
        $timeControlData['loadImage'] = 1;
        $timeControlData['dynamic'] = 1;

        if ($mode == SCREEN_MODE_JS) {
            $timeControlData['dynamic'] = 0;
        }

        $js = 'timeControl.addObject("' . $this->getDataId() . '", ' . json_encode($this->timeline) . ', ' .
            json_encode($timeControlData) . ');';

        $elem = (new CDiv())
            ->addClass('center')
            ->setId($containerId);

        return [$elem, $js];
    }
}
