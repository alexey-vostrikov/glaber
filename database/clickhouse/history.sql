
CREATE TABLE zabbix.history_test ( day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                ns UInt32,  
                                value String,
                                hostname String,
                                itemname String) ENGINE = MergeTree(day, (itemid, clock, hostname, itemname), 8192);

CREATE TABLE zabbix.history_test_buffer (day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                ns UInt32,  
                                value String,
                                hostname String,
                                itemname String) ENGINE = Buffer(zabbix, history_test, 16, 30, 100, 50000, 1000000, 1000000, 10000000) ;