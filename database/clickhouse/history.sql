
CREATE TABLE glaber.history ( day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                ns UInt32, 
                                value Int64,  
                                value_dbl Float64,  
                                value_str String 
                            ) ENGINE = MergeTree(day, (itemid, clock), 8192);

CREATE TABLE glaber.history_buffer (day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                ns UInt32,  
                                value Int64,  
                                value_dbl Float64,  
                                value_str String ) ENGINE = Buffer(glaber, history, 8, 30, 60, 9000, 60000, 256000, 256000000) ;

--trends support tables, unlike the history one, they are zabbix-style - a table per value type
CREATE TABLE glaber.trends_uint ( day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                value_min Int64,  
                                value_max Int64,
                                value_avg Int64,
                                hostname String,
                                itemname String 
                            ) ENGINE = MergeTree(day, (itemid, clock), 8192);

CREATE TABLE glaber.trends_dbl ( day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                value_min Float64,  
                                value_max Float64,
                                value_avg Float64,
                                hostname String,
                                itemname String 
                            ) ENGINE = MergeTree(day, (itemid, clock), 8192);

CREATE TABLE glaber.trends_uint_buffer ( day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                value_min Int64,  
                                value_max Int64,
                                value_avg Int64,
                                hostname String,
                                itemname String 
                            ) ENGINE = Buffer(glaber, trends_uint, 8, 30, 60, 9000, 60000, 256000, 256000000) ;


CREATE TABLE glaber.trends_dbl_buffer ( day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                value_min Float64,  
                                value_max Float64,
                                value_avg Float64,
                                hostname String,
                                itemname String 
                            ) ENGINE = Buffer(glaber, trends_dbl, 8, 30, 60, 9000, 60000, 256000, 256000000) ;


--in case of nanoseconds aren't used, then remove ns field
--add hostname and itemname fileds to table and indexing if you use them:

CREATE TABLE zabbix.history ( day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                ns UInt32, 
                                value Int64,  
                                value_dbl Float64,  
                                value_str String 
                                hostname String,
                                itemname String) ENGINE = MergeTree(day, (itemid, clock, hostname, itemname), 8192);
CREATE TABLE zabbix.history_buffer (day Date,  
                                itemid UInt64,  
                                clock DateTime,  
                                ns UInt32,  
                                value Int64,  
                                value_dbl Float64,  
                                value_str String,
                                hostname String,
                                itemname String ) ENGINE = Buffer(zabbix, history, 8, 30, 60, 9000, 60000, 256000, 256000000) ;

--if you wish to have trends:

CREATE MATERIALIZED VIEW zabbix.trends
ENGINE = AggregatingMergeTree() PARTITION BY toYYYYMM(clock) ORDER BY (clock, itemid)
AS SELECT
    toStartOfHour(clock) AS clock,
    itemid,
    count(value_dbl) AS num,
    min(value_dbl) AS value_min,
    avg(value_dbl) AS value_avg,
    max(value_dbl) AS value_max
FROM zabbix.history
GROUP BY clock,itemid;

CREATE MATERIALIZED VIEW zabbix.trends_uint
ENGINE = AggregatingMergeTree() PARTITION BY toYYYYMM(clock) ORDER BY (clock, itemid)
AS SELECT
    toStartOfHour(clock) AS clock,
    itemid,
    count(value) AS num,
    min(value) AS value_min,
    avg(value) AS value_avg,
    max(value) AS value_max
FROM zabbix.history
GROUP BY clock,itemid;
