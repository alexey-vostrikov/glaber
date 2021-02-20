package histApi

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

type AggMetric struct {
	Host       string  `json:"hostname"`
	Item_key   string  `json:"item_key"`
	Itemid     uint64  `json:"itemid"`
	Time	  uint64  `json:"time"`
	Value_type uint8 `json:"value_type"`
	Avg  float64  `json:"avg"`
	Max  float64  `json:"max"`
	Min  float64  `json:"min"`
	AvgInt  uint64  `json:"avgint"`
	MaxInt  uint64  `json:"maxint"`
	MinInt  uint64  `json:"minint"`
	Count uint64  `json:"count"`
	I uint32	`json:i`
}

type HistoryRequest struct {
	Type	string `json:"request"`
	Itemid uint64 `json:"itemid"`
	Start  uint64 `json:"start"`
	End    uint64 `json:"end"`
	Count  uint64 `json:"count"`
	Value_type uint8 `json:"value_type"`
	Logeventid uint64  `json:"logeventid"`
	Severity uint8	`json:"severity"`
	Source	string `json:"source"`
	Metrics []Metric `json:"metrics"`
	AggMetrics []AggMetric `json:"aggmetrics"`
}

type HistoryEngine interface {
	ReadMetrics( hr HistoryRequest ,  f func(*Metric, *bufio.Writer, int) , wr *bufio.Writer, log *log.Logger ) 
	WriteMetrics ( m *Metric,  log *log.Logger ) 
	ReadAgg ( hr HistoryRequest , f func(*AggMetric, *bufio.Writer, int) , wr *bufio.Writer, log *log.Logger ) []AggMetric 
	ReadTrends ( hr HistoryRequest , f func(*AggMetric, *bufio.Writer, int) , wr *bufio.Writer, log *log.Logger ) []AggMetric 
	WriteTrends ( m *AggMetric , log *log.Logger ) 
	Flush() int
}


func ServeHistory ( he HistoryEngine, reader *bufio.Reader, writer *bufio.Writer, log *log.Logger) {

	var request []byte
	var err error
	var in_records int
	var lastflush int64
	
	//log.Print("Waiting for a request\n")
	
	var p fastjson.Parser
	for {
		
		request, err = (*reader).ReadBytes('\n')
		//log.Print("Got request:",string(request))
		if ( nil !=err ) {
			//log.Print(err)
		  	return
		}
				
		v,err := p.Parse(string(request))
		
		if ( nil != err ) {
		  log.Print(err)
		  return
		}
	
		switch string(v.GetStringBytes("request")) {
			case "get_history":
				hr := HistoryRequest {
					Itemid: uint64(v.GetInt64("itemid")),
					Value_type: uint8(v.GetInt("value_type")),
					Start  :uint64(v.GetInt64("start")),
					End:  uint64(v.GetInt64("end")),
					Count: uint64(v.GetInt64("count")),
				}
				fmt.Fprint(writer,"{\"metrics\":[")
				he.ReadMetrics(hr,dumpMetric,writer,log) 
				fmt.Fprintln(writer,"]}\n");
				writer.Flush()

			case "put_history":
				//log.Print("Processing put request")
				for _,metric := range v.GetArray("metrics") {
					m := Metric {
						Host :string(metric.GetStringBytes("hostname")),
						Item_key: string(metric.GetStringBytes("item_key")),
						Itemid: uint64(metric.GetInt64("itemid")),
						Sec: uint64(metric.GetInt64("time_sec")),
						Ns:  uint32(metric.GetInt64("time_ns")),
						Value_type: uint8(metric.GetInt("value_type")),
						
					} 
					switch m.Value_type {
						case ITEM_VALUE_TYPE_FLOAT:
							m.Value_dbl = float64(metric.GetFloat64("value_dbl"))
						case ITEM_VALUE_TYPE_UINT64:
							m.Value_int = int64(metric.GetInt("value_int"))
						case ITEM_VALUE_TYPE_TEXT, ITEM_VALUE_TYPE_STR:
							m.Value_str = string(metric.GetStringBytes("value_str"))
						case ITEM_VALUE_TYPE_LOG:	
							m.Value_str = string(metric.GetStringBytes("value_str"))
							m.Logeventid = uint64(metric.GetInt("logeventid"))
							m.Severity =  uint8(metric.GetInt("severity"))
							m.Source =  string(metric.GetStringBytes("source"))
					}
					in_records ++
					he.WriteMetrics(&m,log)
				}
	
				he.Flush()
				fmt.Fprint(writer,"\n")
				writer.Flush()

			case "put_trends":
				for _,metric := range v.GetArray("aggmetrics") {
					m := AggMetric {
						Host :string(metric.GetStringBytes("hostname")),
						Item_key: string(metric.GetStringBytes("item_key")),
						Itemid: uint64(metric.GetInt64("itemid")),
						Time: uint64(metric.GetInt64("time")),
						Value_type: uint8(metric.GetInt("value_type")),
						Count: uint64(metric.GetInt64("count")),
					} 
					switch m.Value_type {
						case ITEM_VALUE_TYPE_UINT64:
							m.AvgInt = uint64(metric.GetInt64("avgint"))
							m.MaxInt = uint64(metric.GetInt64("maxint"))
							m.MinInt = uint64(metric.GetInt64("minint"))
						case ITEM_VALUE_TYPE_FLOAT:
							m.Avg = float64(metric.GetFloat64("avg"))
							m.Max = float64(metric.GetFloat64("max"))
							m.Min = float64(metric.GetFloat64("min"))
						default:
							log.Panic("Unknown value type ",m.Value_type," for metric ",m.Host," ",m.Item_key)
					}
					
					he.WriteTrends(&m,log)
				}
				he.Flush()
				fmt.Fprintln(writer,"\n")
				writer.Flush()
			
			case "get_trends":
				//log.Print( string(request) )

				hr := HistoryRequest {
					Itemid: uint64(v.GetInt64("itemid")),
					Value_type: uint8(v.GetInt("value_type")),
					Start  :uint64(v.GetInt64("start")),
					End:  uint64(v.GetInt64("end")),
					Count: uint64(v.GetInt64("count")),
				}
				
				fmt.Fprint(writer,"{\"aggmetrics\":[")
				he.ReadTrends(hr,dumpAggMetric,writer,log) 
				fmt.Fprintln(writer,"]}\n");
				writer.Flush()

			case "get_agg":
				//log.Print( string(request) )

				hr := HistoryRequest {
					Itemid: uint64(v.GetInt64("itemid")),
					Value_type: uint8(v.GetInt("value_type")),
					Start  :uint64(v.GetInt64("start")),
					End:  uint64(v.GetInt64("end")),
					Count: uint64(v.GetInt64("count")),
				}
				
				fmt.Fprint(writer,"{\"aggmetrics\":[")
				he.ReadAgg(hr,dumpAggMetric,writer,log) 
				fmt.Fprintln(writer,"]}\n");
				writer.Flush()


			default:
				//log.Print("Unknown request type or EOF")
				return
		}
		if (lastflush + 5 < time.Now().Unix() ) {
			//log.Print("In records:",in_records)
			in_records = 0
			lastflush = time.Now().Unix()
		}
	}
	//os.Exit(0)
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