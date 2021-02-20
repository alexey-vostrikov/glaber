package historyClickhouse
//overall testing idea is taken from
//https://dave.cheney.net/2019/05/07/prefer-table-driven-tests


import (
	"testing"
	//"fmt"
	"log"
	"os"
	"glaber.io/glapi/pkg/clickhouse"
	"glaber.io/glapi/pkg/api"
	"bytes"
	"strings"
	"bufio"
	"encoding/json"
	"math"
	//"strconv"
)

var url string = "http://10.100.11.6:8123?user=default&password=huyabbix"
var dbname string = "glaber" 
var batch int = 1000000
var flush int = 1
var disable_ns bool = false
var save_names bool = true

var logger *log.Logger = log.New(os.Stdout, "", log.LstdFlags)


func Test_Clickhouse(t *testing.T) {

	tests := map[string]struct {
		test_data string
		test_request string
		
    }{
		"INT_test": {test_data: `{"request":"put", "metrics":[{"hostname":"test_int_host", "item_key":"test_int_key","itemid":100001234, "time_sec":1599366137, "time_ns":274842581, "value_type":3, "value_int":772685824 }] }`,	
					 test_request:`{"request":"get", "itemid":100001234, "value_type":3, "start":1599366136, "count":1}`,},
		"DBL_test": {test_data: `{"request":"put", "metrics":[{"hostname":"test_dbl_host", "item_key":"test_dbl_key","itemid":100001235, "time_sec":1599366100, "time_ns":274842581, "value_type":0, "value_dbl":0.1234567 }] }`,	
					 test_request:`{"request":"get", "itemid":100001235, "value_type":0, "start":1599366100, "count":1}`,},
		"STR_test": {test_data: `{"request":"put", "metrics":[{"hostname":"test_str_host", "item_key":"test_str_key","itemid":100001238, "time_sec":1599366108, "time_ns":274842581, "value_type":1, "value_str":"some test simple text or it might be an asca[ed  log" }] }`,	
					 test_request:`{"request":"get", "itemid":100001238, "value_type":1, "start":1599366108, "count":1}`,},	
		"TEXT_test": {test_data: `{"request":"put", "metrics":[{"hostname":"test_text_host", "item_key":"test_text_key","itemid":100001288, "time_sec":1599366908, "time_ns":274842581, "value_type":4, "value_str":"some test simple text tetx text  text or it might be an asca[ed  log" }] }`,	
					 test_request:`{"request":"get", "itemid":100001288, "value_type":4, "start":1599366908, "count":1}`,},	
		"LOG_test": {test_data: `{"request":"put", "metrics":[{"hostname":"test_log_host", "item_key":"test_log_key","itemid":100008888, "time_sec":1599366908, "time_ns":274842581, "value_type":2, "value_str":"some test log", "logeventid":456789, "severity":5, "source":"test suite"}] }`,	
					 test_request:`{"request":"get", "itemid":100008888, "value_type":2, "start":1599366908, "count":1}`,},				 		 
    }

	for name, tc := range tests {
        t.Run(name, func(t *testing.T) {
			var resp histApi.HistoryRequest		
			var req histApi.HistoryRequest

			out := bytes.NewBufferString("")
			
			bwriter := bufio.NewWriter(out)		
			breader:= bufio.NewReader(strings.NewReader(tc.test_data+"\n"))
			
			hist := &historyClickhouse.ClickHouseHist{}
			historyClickhouse.Init(hist, url,dbname,batch,flush,disable_ns,save_names)
			
			histApi.ServeHistory( hist, breader, bwriter, logger )
		
			breader = bufio.NewReader(strings.NewReader(tc.test_request+"\n"))

			histApi.ServeHistory( hist, breader, bwriter, logger )

			//TODO :standard unmarshalling breaks on quites - so need to unquote them first
			//or change unmarshalling method (say, use valya's "fastjson" here either)
			//as for now tests containing strings with quites will fail

			err  := json.Unmarshal(([]byte)(out.String()),&resp)

			if err != nil {
				t.Error("error:", err)
			}
			
			err =json.Unmarshal(([]byte)(tc.test_data),&req)
			
			if err != nil {
				t.Error("error:", err)
			}
			if (len(resp.Metrics) <1 || len(req.Metrics) < 1) {
				t.Error("Test request didn't return any metrics or there is a problem in the data")
			} else {
				//we'll check only the first metric
				switch (resp.Metrics[0].Value_type) {
					case histApi.ITEM_VALUE_TYPE_UINT64:
						if (req.Metrics[0].Value_int != resp.Metrics[0].Value_int) {
							t.Errorf("Test request returned non expected INT data request: test data:'%s'\n request: '%s'\n responce: '%s'",
									tc.test_data, tc.test_request,out)
						}
					case histApi.ITEM_VALUE_TYPE_FLOAT:
						if ( math.Abs(1- req.Metrics[0].Value_dbl / resp.Metrics[0].Value_dbl) > 0.0001 ) {
							
							t.Errorf("Test request returned non expected DBL data request: test data:'%s'\n request: '%s'\n responce: '%s'",
									tc.test_data, tc.test_request,out)
						}	else {
					//		if (req.Metrics[0].Value_dbl != resp.Metrics[0].Value_dbl) {
					//			t.Logf("DBL data differs but within round margin error: %f != %f", req.Metrics[0].Value_dbl, resp.Metrics[0].Value_dbl)
					//		}
						}
					case histApi.ITEM_VALUE_TYPE_TEXT,histApi.ITEM_VALUE_TYPE_STR :
						if (req.Metrics[0].Value_str != resp.Metrics[0].Value_str) {
							t.Errorf("Test request returned non expected STR data request: test data:'%s'\n request: '%s'\n responce: '%s'",
									tc.test_data, tc.test_request,out)
						}	
					case histApi.ITEM_VALUE_TYPE_LOG :
						if (req.Metrics[0].Value_str != resp.Metrics[0].Value_str && 
							req.Metrics[0].Logeventid != resp.Metrics[0].Logeventid &&
							req.Metrics[0].Source != resp.Metrics[0].Source &&
							req.Metrics[0].Severity != resp.Metrics[0].Severity) {
							t.Errorf("Test request returned non expected LOG data request: test data:'%s'\n request: '%s'\n responce: '%s'",
								tc.test_data, tc.test_request,out)
						}			
				}
			}

        })
    }
}



func Test_Trends_Clickhouse(t *testing.T) {

	tests := map[string]struct {
		test_data string
		test_request string
		
    }{
		"trends_INT_test": {test_data: `{"request":"put_agg", "aggmetrics":[{"hostname":"test_int_agg_host", "item_key":"test_agg_int_key","itemid":100001234, "time":1599366137, "value_type":3, "avgint":20, "maxint":30, "minint":10, "count":34 }] }`,	
					 test_request:`{"request":"get_agg", "itemid":100001234, "value_type":3, "start":1599366136, "end":1599366500, "count":1}`,},
		"trends_DBL_test": {test_data: `{"request":"put_agg", "aggmetrics":[{"hostname":"test_agg_dbl_host", "item_key":"test_agg_dbl_key","itemid":100001235, "time":1599366100, "value_type":0, "avg":0.1234567, "min":0.001, "max":200.45 }] }`,	
					 test_request:`{"request":"get_agg", "itemid":100001235, "value_type":0, "start":1599366100, "end":1599366500, "count":1}`,},
		
    }

	for name, tc := range tests {
        t.Run(name, func(t *testing.T) {
			var resp histApi.HistoryRequest		
			var req histApi.HistoryRequest

			out := bytes.NewBufferString("")
			
			bwriter := bufio.NewWriter(out)		
			breader:= bufio.NewReader(strings.NewReader(tc.test_data+"\n"))
			
			hist := &historyClickhouse.ClickHouseHist{}
			historyClickhouse.Init(hist, url,dbname,batch,flush,disable_ns,save_names)
			
			histApi.ServeHistory( hist, breader, bwriter, logger )
		
			breader = bufio.NewReader(strings.NewReader(tc.test_request+"\n"))

			histApi.ServeHistory( hist, breader, bwriter, logger )

			//TODO :standard unmarshalling breaks on quotes - so need to unquote them first
			//or change unmarshalling method (say, use valya's "fastjson" here either)
			//as for now tests containing strings with quites will fail
			
			err  := json.Unmarshal(([]byte)(out.String()),&resp)

			if err != nil {
				t.Error("error:", err, out.String())
			}
			
			err =json.Unmarshal(([]byte)(tc.test_data),&req)
			
			if err != nil {
				t.Error("error:", err)
			}
			//log.Print(resp)
			//log.Print(req)
			if (len(resp.AggMetrics) <1 || len(req.AggMetrics) < 1) {
				t.Error("Test request didn't return any metrics or there is a problem in the data")
			} else {
				//we'll check only the first metric
				switch (resp.AggMetrics[0].Value_type) {
					case histApi.ITEM_VALUE_TYPE_UINT64:
						if ( req.AggMetrics[0].AvgInt != resp.AggMetrics[0].AvgInt || 
							req.AggMetrics[0].MaxInt != resp.AggMetrics[0].MaxInt ||
							req.AggMetrics[0].MinInt != resp.AggMetrics[0].MinInt ) {
							t.Errorf("Test request returned non expected INT data request: test data:'%s'\n request: '%s'\n responce: '%s'",
									tc.test_data, tc.test_request,out)
						}
					case histApi.ITEM_VALUE_TYPE_FLOAT:
						if ( math.Abs(req.AggMetrics[0].Avg - resp.AggMetrics[0].Avg) > 0.0001 ||  
							 math.Abs(req.AggMetrics[0].Max - resp.AggMetrics[0].Max) > 0.0001 ||
							 math.Abs(req.AggMetrics[0].Min - resp.AggMetrics[0].Min) > 0.0001 ) {
							t.Errorf("Test request returned non expected DBL data request: test data:'%s'\n request: '%s'\n responce: '%s'",
									tc.test_data, tc.test_request,out)
						}	
							
				}
			}

        })
    }
}


//func Benchmark_Write(b *testing.B) {
	
	
	//for i := 0; i < b.N; i++ {
	//	breader:= bufio.NewReader(strings.NewReader(`{"request":"put_agg", "metrics":[{"hostname":"bbr1-chel1.is74.ru", "item_key":"card.memfree.asr9k[2]","itemid":2791557, "time":1599366137, "value_type":3, "avg_int":772685824, "max_int":972685824, "min_int":672685824, "count":12 },{"hostname":"zbxs1-chel2.is74.ru", "item_key":"zabbix[process,\"async agent poller\",avg,busy]","itemid":254035594, "time":1599366137, "value_type":3, "avg_int":50,"max_int":150, "min_int":5, "count":50},{"hostname":"wefwernfj", "item_key":"wbfhwbckjwlcjowe","itemid":593493, "time":1599366130, "value_type":0, "avg":0.5,"max":1.50, "min":0.5, "count":8}]}`));
	
//	}
//}

//go test -bench  ' ' -v  ./cmd/clickhouse/clickhouse_test.go 