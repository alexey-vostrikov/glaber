<?php

class CHostDepends extends CApiService
{
    public const ACCESS_RULES = [
        'get' => ['min_user_type' => USER_TYPE_ZABBIX_USER],
        'create' => ['min_user_type' => USER_TYPE_ZABBIX_ADMIN],
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
            'depids'					=> null,
            // filter
            'filter'					=> null,
            // output
            'output'					=> API_OUTPUT_EXTEND,
            'countOutput'				=> false,
            'preservekeys'				=> false,
            'sortfield'					=> '',
            'sortorder'					=> '',
            'limit'						=> null
        ];

        $options = zbx_array_merge($defOptions, $options);
        $this->validateGet($options);

        if (is_array($options['filter'])) {
            $this->dbFilter($this->tableName . ' '. $this->tableAlias, $options, $sqlParts);
        }

        $sqlParts = $this->applyQueryOutputOptions($this->tableName(), $this->tableAlias(), $options, $sqlParts);
        $sqlParts = $this->applyQuerySortOptions($this->tableName(), $this->tableAlias(), $options, $sqlParts);
        $res = DBselect(self::createSelectQueryFromParts($sqlParts), $sqlParts['limit']);
        while ($item = DBfetch($res)) {
            if (!$options['countOutput']) {
                $result[$item['itemid']] = $item;
                continue;
            }

            $result = $item['rowscount'];
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
            return self::exception(ZBX_API_ERROR_INTERNAL, _s('Error on insert'));
        }

        return ['depid' => $depids[0]];
    }

    /**
     * @param array $depIds
     * @return array[]
     */
    public function delete(array $depIds)
    {
        DB::delete($this->tableName, ['depid' => $depIds]);
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
            'type' => ['type' => API_STRINGS_UTF8, 'flags' => API_NORMALIZE, 'length' => 128],
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
                'type' => $dep['type'],
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