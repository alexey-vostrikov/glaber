<?php

class CHostDepends extends CApiService
{
    public const ACCESS_RULES = [
        'get' => ['min_user_type' => USER_TYPE_ZABBIX_USER],
        'create' => ['min_user_type' => USER_TYPE_ZABBIX_ADMIN],
        'update' => ['min_user_type' => USER_TYPE_ZABBIX_ADMIN],
        'delete' => ['min_user_type' => USER_TYPE_ZABBIX_ADMIN]
    ];

    protected $tableName = 'hosts_depends';
    protected $tableAlias = 'hd';

    public function get($options = [])
    {
        $result = [];

        $sqlParts = [
            'select'	=> ['deps' => 'hd.depid'],
            'from'		=> [$this->tableName => $this->tableName . ' '. $this->tableAlias],
            'where'		=> [],
            'group'		=> [],
            'order'		=> [],
            'limit'		=> null
        ];

        $defOptions = [
            'depids' => null,
            // filter
            'hostId' => null,
            'filter' => null,
            // output
            'output' => API_OUTPUT_EXTEND,
            'with_hostname' => false,
            'countOutput' => false,
            'preservekeys' => false,
            'sortfield' => '',
            'sortorder' => '',
            'limit' => null
        ];

        $options = zbx_array_merge($defOptions, $options);
        $this->validateGet($options);

        if ($options['hostId'] !== null) {
            $sqlParts['where'][] = '('. dbConditionInt('hd.hostid_up', [$options['hostId']])
                                        . ' OR ' . dbConditionInt('hd.hostid_down', [$options['hostId']])
                                        . ' )';
        }

        if ($options['with_hostname']) {
            $sqlParts['from']['host_up'] = 'hosts h_up';
            $sqlParts['where']['host_up'] = 'h_up.hostid=hd.hostid_up';
            $sqlParts['select'][] = 'h_up.name as hostname_up';

            $sqlParts['from']['host_down'] = 'hosts h_down';
            $sqlParts['where']['host_down'] = 'h_down.hostid=hd.hostid_down';
            $sqlParts['select'][] = 'h_down.name as hostname_down';
        }

        if (is_array($options['filter'])) {
            $this->dbFilter($this->tableName . ' '. $this->tableAlias, $options, $sqlParts);
        }

        $sqlParts = $this->applyQueryOutputOptions($this->tableName(), $this->tableAlias(), $options, $sqlParts);
        $sqlParts = $this->applyQuerySortOptions($this->tableName(), $this->tableAlias(), $options, $sqlParts);

        $res = DBselect(self::createSelectQueryFromParts($sqlParts), $sqlParts['limit']);
        if ($options['countOutput'] && $item = DBfetch($res)) {
            return $item['rowscount'];
        }

        while ($item = DBfetch($res)) {
            $result[$item['depid']] = $item;
        }

        // removing keys (hash -> array)
        if (!$options['preservekeys']) {
            $result = zbx_cleanHashes($result);
        }

        return $result;
    }

    /**
     * @param $dep
     * @return array
     * @throws APIException
     *
     * @todo Add hosts_dep by hostname, and convert it to hostid
     */
    public function create($dep)
    {
        $this->validateCreate($dep);

        $depids = DB::insert($this->tableName, [$dep]);

        if (empty($depids)) {
            self::exception(ZBX_API_ERROR_INTERNAL, _s('Error on insert'));
        }

        return ['depid' => $depids[0]];
    }

    /**
     * @param $dep
     * @return array
     * @throws APIException
     */
    public function update($dep)
    {
        $this->validateUpdate($dep);

        $depid = $dep['depid'];
        unset($dep['depid']);

        DB::update($this->tableName, [['values' => $dep, 'where' => ['depid'=>$depid]]]);

        return ['depid' => $depid];
    }

    /**
     * @param array $depIds
     * @return array[]
     */
    public function delete(array $depIds)
    {
        if(!DB::delete($this->tableName, ['depid' => $depIds])){
            self::exception(ZBX_API_ERROR_INTERNAL, _s('Error on delete'));
        }
        return ['depids' => $depIds];
    }

    private function validateCreate(array $dep)
    {
        // required fields
        if (!array_key_exists('hostid_up', $dep) || !array_key_exists('hostid_down', $dep)) {
            self::exception(ZBX_API_ERROR_PARAMETERS, _s('Fields "hostid_up" and "hostid_down" is required'));
        }

        // field types
        $rules = ['type' => API_OBJECT, 'fields' => [
            'hostid_up' => ['type' => API_UINT64],
            'hostid_down' => ['type' => API_UINT64],
            'name' => ['type' => API_STRINGS_UTF8, 'flags' => API_NORMALIZE, 'length' => 128],
        ]];

        $error = '';

        if (!CApiInputValidator::validate($rules, $dep, '/', $error)) {
            self::exception(ZBX_API_ERROR_PARAMETERS, $error);
        }

        // @todo Сделать проверку наличия хостов

        // validate uniq
        $count = $this->get([
            'countOutput' => true,
            'filter' => [
                'hostid_up' => $dep['hostid_up'],
                'hostid_down' => $dep['hostid_down'],
                'name' => $dep['name'],
            ],
        ]);

        if($count != 0){
            self::exception(ZBX_API_ERROR_PARAMETERS, _s("Dependency is exists"));
        }
    }

    private function validateUpdate(array $dep)
    {
        // required fields
        if (!array_key_exists('hostid_up', $dep) || !array_key_exists('hostid_down', $dep)) {
            self::exception(ZBX_API_ERROR_PARAMETERS, _s('Fields "hostid_up" and "hostid_down" is required'));
        }

        // field types
        $rules = ['type' => API_OBJECT, 'fields' => [
            'depid' => ['type' => API_UINT64],
            'hostid_up' => ['type' => API_UINT64],
            'hostid_down' => ['type' => API_UINT64],
            'name' => ['type' => API_STRINGS_UTF8, 'flags' => API_NORMALIZE, 'length' => 128],
        ]];

        $error = '';

        if (!CApiInputValidator::validate($rules, $dep, '/', $error)) {
            self::exception(ZBX_API_ERROR_PARAMETERS, $error);
        }

        // @todo Сделать проверку наличия хостов

        // validate uniq
        $count = $this->get([
            'countOutput' => true,
            'filter' => [
                'hostid_up' => $dep['hostid_up'],
                'hostid_down' => $dep['hostid_down'],
                'name' => $dep['name'],
            ],
        ]);

        if($count != 0){
            self::exception(ZBX_API_ERROR_PARAMETERS, _s("Dependency is exists"));
        }
    }

    private function validateGet(array $options)
    {
    }
}