<?php
/*
** Glaber
** Copyright (C) 2001-2389 Glaber JSC
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



//changeset related constants, need to be in sync with changset.h

abstract class CChangeset {
    const DB_CREATE = 1;
    const DB_UPDATE = 2;
    const DB_DELETE = 3;

    const OBJ_HOSTS = 12;
    const OBJ_ITEMS = 13;
    const OBJ_TRIGGERS = 14;
    const OBJ_FUNCTIONS = 15;
    const OBJ_PREPROCS = 16;
    const OBJ_TRIGGERDEPS = 17;
    const OBJ_TRIGGERTAGS = 18;
    const OBJ_ITEMTAGS = 19;
    const OBJ_TEMPLATES = 20;
    const OBJ_PROTOTYPES = 21;
    
    const TABLE_NAME = 'changeset';

    public static function add_objects(int $obj_type, int $change_type, array $ids) {
        $clock = time();
        $sql ="";
        
        foreach ($ids as $id) {
            $sql =$sql. "DELETE FROM ". self::TABLE_NAME ." WHERE obj_id = $id;".
                  "INSERT INTO ". self::TABLE_NAME . 
                    " (clock, obj_type, obj_id, change_type) VALUES ( $clock, $obj_type, $id, $change_type);";
        }
        
        DBExecute($sql); 
	}

    public static function add_items(int $change_type, array $ids) {
        CChangeset::add_objects(CChangeset::OBJ_ITEMS, $change_type, $ids);

    }
    //host reloading isn't supported yet, but need to reload items - they might get disabled/enabled
    public static function process_changed_hosts(array $hostids) {
        						
		$items = API::Item()->get([
			'output' => ['itemid'],
			'hostids' => $hostids,
		]);
		$itemids = array_column($items,'itemid');
 
        CChangeset::add_objects(OBJ_ITEMS, DB_UPDATE, $itemids);
    }

}