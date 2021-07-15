package pollApi

import (
	"github.com/valyala/fastjson"
	"log"
	"bufio"
	"fmt"
	"time"
)

const ITEM_VALUE_TYPE_FLOAT = 0
const ITEM_VALUE_TYPE_STR = 1
const ITEM_VALUE_TYPE_LOG = 2
const ITEM_VALUE_TYPE_UINT64 = 3
const ITEM_VALUE_TYPE_TEXT = 4
const ITEM_VALUE_TYPE_MAX = 5
const ITEM_VALUE_TYPE_NONE = 6

type Metric struct {
	Host       string  `json:"hostname"`
	Item_key   string  `json:"item_key"`
	Itemid     uint64  `json:"itemid"`
	Sec   uint64  `json:"time_sec"`
	Ns    uint32  `json:"time_ns"`
	Value_type uint8   `json:"value_type"`
	Value_int  int64   `json:"value_int"`
	Value_dbl  float64 `json:"value_dbl"`
	Value_str  string  `json:"value_str"`
	Logeventid uint64  `json:"logeventid"`
	Severity uint8	`json:"severity"`
	Source	string `json:"source"`

}


type AsyncMetricPoller interface {
	PollRequestMetric( hr HistoryRequest ,  f func(*Metric, *bufio.Writer, int) , wr *bufio.Writer, log *log.Logger ) 
	ReadResults( hr HistoryRequest , f func(*AggMetric, *bufio.Writer, int) , wr *bufio.Writer, log *log.Logger ) []AggMetric 
	Flush() int
}


func ServePoll ( he HistoryEngine, reader *bufio.Reader, writer *bufio.Writer, log *log.Logger) {

	var request []byte
	var err error
	var in_records int
	var lastflush int64
	
	log.Print("Waiting for a request\n")
	
	var p fastjson.Parser
	for {
		
		request, err = (*reader).ReadBytes('\n')
		log.Print("Got request:",string(request))
		if ( nil !=err ) {
			log.Print(err)
		  	return
		}
				
		v,err := p.Parse(string(request))
		
		if ( nil != err ) {
		  log.Print(err)
		  return
		}
	
		//there is only one type of poll request possible: 

		switch string(v.GetStringBytes("request")) {
			case "poll":
				pr := PollRequest {
					Itemid: uint64(v.GetInt64("itemid")),
					Ip: uint8(v.GetInt("addr")),
					Key  :uint64(v.GetInt64("key")),
				}
				
				pe.ProcessRequest()
				fmt.Fprint(writer,"{\"metrics\":[")
				he.ReadMetrics(hr,dumpMetric,writer,log) 
				fmt.Fprintln(writer,"]}\n");
				writer.Flush()

			default:
				//log.Print("Unknown request type or EOF")
				return
		}
	}
}

//history engine is expected to return itemid, time, nanosecond time, and value
//worker module will treat the value according to it's value type
func dumpMetric(metric *Metric,wr *bufio.Writer, num int) {
	if num > 0 {
		fmt.Fprintln(wr,",")
	}
	fmt.Fprint(wr,"{\"time_sec\":",metric.Sec,", \"time_ns\":",metric.Ns, ", \"value_type\":",metric.Value_type)
	switch metric.Value_type {
		case ITEM_VALUE_TYPE_FLOAT:
			fmt.Fprint(wr,", \"value_dbl\":",metric.Value_dbl)
		case ITEM_VALUE_TYPE_UINT64:	
			fmt.Fprint(wr,", \"value_int\":",metric.Value_int)
		case ITEM_VALUE_TYPE_STR, ITEM_VALUE_TYPE_TEXT:	
			fmt.Fprint(wr,", \"value_str\":\"",metric.Value_str,"\"")
		case ITEM_VALUE_TYPE_LOG:
			fmt.Fprint(wr,", \"value_str\":\"",metric.Value_str,"\" , \"logeventid\":",metric.Logeventid, 
				", \"source\":\"",metric.Source,"\", \"severity\":",metric.Severity)
	}
	fmt.Fprint(wr,"}")
}

func dumpAggMetric(agg_metric *AggMetric,wr *bufio.Writer, num int) {
	
	if num > 0 {
		fmt.Fprintln(wr,",")
	}

	fmt.Fprint(wr,"{\"clock\":",agg_metric.Time,", \"value_type\":",agg_metric.Value_type, ", \"itemid\":",agg_metric.Itemid, ", \"i\":",agg_metric.I)
	switch agg_metric.Value_type {
		case ITEM_VALUE_TYPE_FLOAT:
			fmt.Fprint(wr,", \"avg\":",agg_metric.Avg,", \"max\":",agg_metric.Max,", \"min\":",agg_metric.Min)
		case ITEM_VALUE_TYPE_UINT64:	
			fmt.Fprint(wr,", \"avg\":",agg_metric.AvgInt,", \"max\":",agg_metric.MaxInt,", \"min\":",agg_metric.MinInt)
	}
	fmt.Fprint(wr,", \"count\":",agg_metric.Count,"}")
}