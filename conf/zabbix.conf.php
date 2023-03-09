<?php

global $DB;

$DB["TYPE"]                             = 'POSTGRESQL';
#$DB["TYPE"]                             = 'MYSQL';

$DB["SERVER"]                   = '127.0.0.1'; 
#$DB["PORT"]                     = '5432'; //uncomment and set if non-default

$DB["SCHEMA"]                   = 'zabbix'; //no required for MySQL
$DB["DATABASE"]                 = 'glaber_srv';

$DB["USER"]                     = 'glaber';   //DB Access credentials
$DB["PASSWORD"]                 = 'glaber';

$ZBX_SERVER                    = '127.0.0.1'; //ip credentials of the server
$ZBX_SERVER_PORT                = '10055';

//name for showing in UI, not necessary for functionality                                
$ZBX_SERVER_NAME                = $_SERVER['SERVER_NAME'] . '(' . gethostname() . ')';
$IMAGE_FORMAT_DEFAULT   = IMAGE_FORMAT_PNG;

//should be so, will deprecate soon
global $HISTORY;
$HISTORY['storagetype']='server';

?>
