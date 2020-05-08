#!/usr/bin/python3
import fileinput
import logging
import json
import sys
import time
from datetime import datetime
import argparse
from influxdb import InfluxDBClient

logging.basicConfig(filename='/tmp/worker.log', level=logging.INFO)
points = []

def get_history(data):
    #logging.debug("Called read_history func")
    query='SELECT value FROM itemid'+str(data['itemid'])+' WHERE '
    
    if ( int(data["end"]) - int(data["start"])) == 1 :
          query += " time ="+str(data['end']*1000000000)
    else :
        if int(data["start"]) > 0:
             query += " time >= "+str(data['start']*1000000000)
        if int(data["end"]) > 0:
            if int(data["start"]) > 0:
                query += " AND "
            query += " time <= "+str(data['end']*1000000000)
    
    if ( int(data["count"])>0):
            query +=' LIMIT '+str(data['count'])

    #logging.debug(query)
    #logging.debug(json.dumps(data))
 
    result = client.query(query=query, epoch="rfc3339")
    points = list(result.get_points(measurement="itemid" + str(data["itemid"])))
    
    #logging.debug(result)
    print("{\"itemid\":"+str(data["itemid"])+", \"data\":[",flush=True)
    for point in points:
        #logging.debug(point)
        print ("{\"itemid\":\"" + str(data["itemid"]) + 
            "\",\"time_sec\":\""+str(int(point["time"]/1000000000)) + 
            "\",\"time_ns\":\""+str(point["time"]%1000000000) + 
             "\",\"value\":\""+ str(point["value"]) + "\"},",flush=True)
    print("]}\n",flush=True)
    return

def put_history(request):
    #logging.debug("Called write_history func"+args.server)
    for data in request["data"]:
        point = {"measurement": "itemid" + str(data['itemid']),
             "tags": {
                "hostname": data["hostname"],
                "item": data["item_key"],
                "value_type": data["value_type"]
                },
            "fields": {
                "value": data["value"],
            },
            "time": int(data['time_sec'])*1000000000+int(data['time_ns'])
        }
        #logging.debug(json.dumps(point))
        
        #buffering
        points.append(point)
        if len(points) > 1024:
            client.write_points(points)
            points.clear()
       # else:
       #    logging.debug("Buffering point, " +
       #              str(len(points)) + " in the buffer now")
    #flushing is important!
    print("\n",flush=True)
    return

def get_aggregated_history(request):
    #logging.debug("Called get_aggregated_history func")
    #logging.debug(json.dumps(request))

    #need to trabslate to this: 
    #select max(value),min(value),mean(value),count(value) from itemid2769658 where time > 1587085275000000000 and time < 1587258075000000000 group by time(623s) order by time asc
    group_sec=int((request['end']-request['start'])/request['steps'])
    
    if group_sec == 0:
        group_sec = 1

    query = "select max(value),min(value),mean(value),count(value) from itemid" +\
              str(request['itemid'])+' WHERE ' +\
            " time >= " + str(request['start']*1000000000) +\
            " AND time <= " + str(request['end']*1000000000) +\
            " GROUP BY time(" + str(group_sec) + "s) ORDER BY time ASC"
    #will be only be able do this having enough data collected
    #logging.debug(query)
    result = client.query(query=query, epoch="rfc3339")
    points = list(result.get_points(measurement="itemid" + str(request["itemid"])))
    
    #logging.debug(result)
    
    #perhaps, making an automatic struct export would be much compact and prettier here
    i = 0
    print("{\"itemid\":"+str(request["itemid"])+", \"data\":[",flush=True)
    first_rec = True
    for point in points:
        #logging.debug(point)
        if point["count"] > 0 :
            if first_rec :
                first_rec = False
            else:
                print (",")
            print ("{\"max\":\"" + str(point["max"]) + \
                "\",\"itemid\":\"" + str(request["itemid"]) +\
                "\",\"clock\":\""+str(int(point["time"]/1000000000)) + 
                "\",\"ns\":\""+str(int(point["time"]%1000000000)) + 
                "\",\"min\":\""+str(point["min"]) + 
                "\",\"avg\":\""+ str(point["mean"]) + 
                "\",\"count\":\""+ str(point["count"]) + 
                "\",\"i\":\""+ str(i) + 
                "\"}",flush=True, end='')
        i+=1
    print("]}\n",flush=True)
    return


switcher = {
    'put': put_history,
    'get': get_history,
    'get_aggregated': get_aggregated_history
}


parser = argparse.ArgumentParser(
    description='Galber to influxdb worker script')

parser.add_argument("-s", "--server", dest="server",
                    help="InfluxDB server host address", metavar="HOST", default="localhost")
parser.add_argument("--port", dest="port", help=" listen port",
                    metavar="PORT", default="8086")
parser.add_argument("-db", "--dbname", dest="dbname",
                    help=" database name", metavar="DBNAME", default="glaber")
parser.add_argument("-u", "--username", dest="username",
                    help=" username", metavar="<USERNAME>", default="default")
parser.add_argument("-p", "--pass", dest="passs",
                    help=" password", metavar="<PASSWORD>", default="")

args = parser.parse_args()

client = InfluxDBClient(host=args.server, port=args.port)
client.create_database(args.dbname)
client.switch_database(args.dbname)

logging.debug("Strated,  waiting for data")

for line in sys.stdin:
    #logging.warning('Got request:' + line)

    if not line in  ['\n', '\r\n'] :
        parsed_line = (json.loads(line))
        func = switcher.get(parsed_line['request'])
        func(parsed_line)
   
# influx.py -db glaber
#{"request":"put", "data":[{"itemid":12345, "value":"0.94", "value_type":1, "time_sec":12345678 , "time_ns":"2442342", "hostname":"test.is74.ru", "item_key":"the_item_key"},{"itemid":12345, "value":"0.94", "value_type":1, "time_sec":12345678 , "time_ns":"2442342", "hostname":"test.is74.ru", "item_key":"the_item_key"}]}
#{"request":"get", "itemid":12345, "start":12345676 , "end":12345680 , "count":"23" }
#{"request":"get_aggregated", "itemid":%d, "start": %d, "steps":%d, "end":%d }
