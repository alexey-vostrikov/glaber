package main

import (
	"glaber.io/glapi/pkg/histapi"
	"glaber.io/glapi/pkg/histback/victoria"
	"flag"
	"log"
	"os"
	"bufio"
	_ "net/http/pprof"
)

func main() {
	var url,dbname string
	var batch,flush int
	var disable_ns,save_names bool

	logger := log.New(os.Stderr, "", log.LstdFlags)
	logger.Print("Started")
	logger.Print(os.Args)

	flag.StringVar(&url,"url","http://localhost:8428","VictoriaMetrics http/https address")
	flag.StringVar(&dbname,"dbname","glaber","unique prefix for metrics")
	flag.IntVar(&batch,"batch",10000,"Number of metrics to group before uploading to DB")
	flag.IntVar(&flush,"flush",2,"if batch size is too big for metrics rate, how often to flush the data")
	flag.BoolVar(&disable_ns,"disable_ns",true, "disable writing of nanoseconds")
	flag.Parse()
	
	
	var hist = &historyVictoria.VictoriaHist{}
	historyVictoria.Init(hist, url,dbname,batch,flush,disable_ns,save_names)
	
	var reader = bufio.NewReader(os.Stdin)
	var writer = bufio.NewWriter(os.Stdout)

	histApi.ServeHistory( hist, reader, writer, logger )
	
}
//some requests for testing

//{"request":"put_history", "metrics":[{"hostname":"bbr1-chel1.is74.ru", "item_key":"card.memfree.asr9k[2]","itemid":2791557, "time_sec":1599366137, "time_ns":274842581, "value_type":3, "value_int":772685824},{"hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"async agent poller\",avg,busy]","itemid":254035594, "time_sec":1599366137, "time_ns":277290317, "value_type":3, "value_int":0},{"request":"put", "hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,poller,avg,busy]","itemid":2916685, "time_sec":1599366137, "time_ns":277290317, "value_type":0, "value_dbl":0.000000},{"request":"put", "hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"timer\",avg,busy]","itemid":2916681, "time_sec":1599366137, "time_ns":277290317, "value_type":0, "value_dbl":0.000000},{"request":"put", "hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"alerter\",avg,busy]","itemid":2916669, "time_sec":1599366137, "time_ns":277290317, "value_type":0, "value_dbl":0.000000},{"request":"put", "hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"trapper\",avg,busy]","itemid":2916682, "time_sec":1599366137, "time_ns":277290317, "value_type":0, "value_dbl":0.000000},{"request":"put", "hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"proxy poller\",avg,busy]","itemid":2916679, "time_sec":1599366137, "time_ns":277290317, "value_type":0, "value_dbl":0.000000},{"request":"put", "hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"configuration syncer\",avg,busy]","itemid":2916670, "time_sec":1599366137, "time_ns":277290317, "value_type":0, "value_dbl":0.000000},{"request":"put", "hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"ipmi poller\",avg,busy]","itemid":2916677, "time_sec":1599366137, "time_ns":277290317, "value_type":0, "value_dbl":0.000000},{"request":"put", "hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"history syncer\",avg,busy]","itemid":2916673, "time_sec":1599366137, "time_ns":277290317, "value_type":0, "value_dbl":0.000000},{"request":"put", "hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"unreachable poller\",avg,busy]","itemid":2916683, "time_sec":1599366137, "time_ns":277290317, "value_type":0, "value_dbl":1.230000},{"request":"put", "hostname":"cordex1-chel12.is74.ru", "item_key":"dcPwrSysInvAlrmIntegerValue[3]","itemid":2791237, "time_sec":1599366137, "time_ns":284148137, "value_type":3, "value_int":12}]}
//{"request":"get", "itemid":2926147, "end":1601339120, "count":4}
//{"request":"put_trends", "aggmetrics":[{"hostname":"bbr1-chel1.is74.ru", "item_key":"card.memfree.asr9k[2]","itemid":2791557, "time":1599366137, "value_type":3, "avg_int":772685824, "max_int":972685824, "min_int":672685824, "count":12 },{"hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"async agent poller\",avg,busy]","itemid":254035594, "time":1599366137, "value_type":3, "avg_int":50,"max_int":150, "min_int":5, "count":50},{"hostname":"wefwernfj", "item_key":"wbfhwbckjwlcjowe","itemid":593493, "time":1599366130, "value_type":0, "avg":0.5,"max":1.50, "min":0.5, "count":8}]}
//{"request":"get_trends", "itemid":2791557, "value_type":3, "start":1599366110, "end":1599366150, "count":1}

//working with vmetrics

//the proper way of using the export - for getting trends and items
// curl  'http://localhost:8428/api/v1/export' -d 'match[]={__name__="item_2098384",label="zabbix1"}&start=0&end=1584157644'

//url for adding data - for both pu
// http://localhost:8428/write
