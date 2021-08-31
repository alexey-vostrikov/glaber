package historyClickhouse

import (
	"glaber.io/glapi/pkg/histapi"

	"github.com/valyala/bytebufferpool"
	"github.com/valyala/fastjson"

	"net/http"
	"time"
	"strings"
	"log"
	"fmt"
	"io/ioutil"
	"bufio"
//	"io"
//	"bytes"
)

type ClickHouseHist struct {
	url string
	dbname string
	batch int
	flush int64
	disable_ns bool
	save_names bool
	cluster_suffix string
	buf			*bytebufferpool.ByteBuffer
	metrics 	*[histApi.ITEM_VALUE_TYPE_MAX]int
	agg_metrics	*[histApi.ITEM_VALUE_TYPE_MAX]int
	lastflush 	*[histApi.ITEM_VALUE_TYPE_MAX]int64
	agg_lastflush 	*[histApi.ITEM_VALUE_TYPE_MAX]int64
	sql_buffer 	[histApi.ITEM_VALUE_TYPE_MAX]*bytebufferpool.ByteBuffer
	agg_sql_buf [histApi.ITEM_VALUE_TYPE_MAX]*bytebufferpool.ByteBuffer
	parser *fastjson.Parser
	quotter *strings.Replacer
}


var trend_tables = []string {"trends_dbl", "", "", "trends_uint",""};
var hist_tables = []string {"history_dbl", "history_str", "history_log", "history_uint", "history_str"};


func Init(he *ClickHouseHist, url string,dbname string , batch int ,flush int ,disable_ns bool ,save_names bool, cluster_suffix string)   {

	he.url = url
	he.dbname = dbname
	he.batch = batch
	he.flush = int64(flush)
	he.disable_ns = disable_ns
	he.save_names = save_names
	
	//things that change has to be refernced by pointers 
	//since there will be copying of type when calling interface methods 
	he.metrics = new([histApi.ITEM_VALUE_TYPE_MAX]int)
	he.agg_metrics = new([histApi.ITEM_VALUE_TYPE_MAX]int)
	he.lastflush = new([histApi.ITEM_VALUE_TYPE_MAX]int64)
	he.agg_lastflush = new([histApi.ITEM_VALUE_TYPE_MAX]int64)
	he.parser = new(fastjson.Parser)
	he.cluster_suffix = cluster_suffix
	he.buf = bytebufferpool.Get()
	he.quotter = strings.NewReplacer("\n","\\n","\"","\\\"","'","\\'", "\\","\\\\")

	for i := 0; i < histApi.ITEM_VALUE_TYPE_MAX; i++ {
		he.sql_buffer[i]=bytebufferpool.Get()
		he.agg_sql_buf[i]=bytebufferpool.Get()
	}
	
}


func (he ClickHouseHist) WriteMetrics (metric *histApi.Metric, log *log.Logger) {
	var buf = he.sql_buffer[metric.Value_type]

	if buf.Len() == 0 {
		fmt.Fprintf(buf,"INSERT INTO %s.%s%s (day, itemid, clock, value ", he.dbname,hist_tables[metric.Value_type],he.cluster_suffix)
		
		if metric.Value_type == histApi.ITEM_VALUE_TYPE_LOG {
			fmt.Fprintf(buf,",logeventid, severity, source")
		}
		
		if ! he.disable_ns {
			fmt.Fprintf(buf,",ns");
		}
			
		if he.save_names {
			fmt.Fprintf(buf,",hostname, itemname")
		}
			
		fmt.Fprintf(buf,") VALUES ")
	} else {
		fmt.Fprintf(buf,",");
	}

	fmt.Fprintf(buf,"(CAST(%d as date),%d,%d", metric.Sec,metric.Itemid,metric.Sec);

	switch metric.Value_type {

		case histApi.ITEM_VALUE_TYPE_UINT64:
			fmt.Fprintf(buf,",%d",metric.Value_int)

		case histApi.ITEM_VALUE_TYPE_FLOAT:
			fmt.Fprintf(buf,",%f",metric.Value_dbl)

		case histApi.ITEM_VALUE_TYPE_STR: 
			fmt.Fprintf(buf,",'%s'",he.quotter.Replace(string(metric.Value_str)))
				
		case histApi.ITEM_VALUE_TYPE_TEXT:
			fmt.Fprintf(buf,",'%s'",he.quotter.Replace(string(metric.Value_str)))
	
		case histApi.ITEM_VALUE_TYPE_LOG:
			fmt.Fprintf(buf,", '%s', %d, %d, '%s'",he.quotter.Replace(string(metric.Value_str)),
						metric.Logeventid, metric.Severity, 
						he.quotter.Replace(metric.Source))

		default:
			log.Panic("Unsupported value type: ", metric.Value_type)
	}

	if ! he.disable_ns {
		fmt.Fprintf(buf,",%d", metric.Ns)
	}
		
	if he.save_names {
		fmt.Fprintf(buf,",'%s','%s'",he.quotter.Replace(metric.Host), 
									he.quotter.Replace(metric.Item_key))
	}
	
	fmt.Fprintf(buf,")")
	(*he.metrics)[metric.Value_type]++
	
}

func (he ClickHouseHist) WriteTrends(agg_metric *histApi.AggMetric, log *log.Logger) {
	
	var buf = he.agg_sql_buf[agg_metric.Value_type]

	if buf.Len() == 0 {
		fmt.Fprintf(buf,"INSERT INTO %s.%s%s (day,itemid,clock,value_min,value_max,value_avg,count,hostname,itemname) VALUES", he.dbname,trend_tables[agg_metric.Value_type],he.cluster_suffix)

	} else {
		fmt.Fprintf(buf,",");
	}


	fmt.Fprintf(buf,"(CAST(%d as date),%d,%d", agg_metric.Time,agg_metric.Itemid,agg_metric.Time);

	switch agg_metric.Value_type {

		case histApi.ITEM_VALUE_TYPE_UINT64:
			fmt.Fprintf(buf,",%d,%d,%d",agg_metric.MinInt, agg_metric.MaxInt, agg_metric.AvgInt)

		case histApi.ITEM_VALUE_TYPE_FLOAT:
			fmt.Fprintf(buf,",%f,%f,%f",agg_metric.Min, agg_metric.Max, agg_metric.Avg)

		default:
			log.Panic("Unsupported value type for aggregated (trend) metric: ", agg_metric.Value_type)
	}

			
	fmt.Fprintf(buf,",%d,'%s','%s'",agg_metric.Count, he.quotter.Replace(agg_metric.Host), he.quotter.Replace(agg_metric.Item_key))
	

	fmt.Fprintf(buf,")")
	//log.Printf(buf.String())
	(*he.agg_metrics)[agg_metric.Value_type]++

}

func (he ClickHouseHist) ReadTrends (hr histApi.HistoryRequest, dumpf func(*histApi.AggMetric, *bufio.Writer, int), wr* bufio.Writer, log *log.Logger) []histApi.AggMetric  {
	var buf strings.Builder
	
	//log.Print("Reading trend metrics")
	//note: Clickhouse will retrun uint64 as a string for JS compatibility, so need to set  output_format_json_quote_64bit_integers='0' as fastjson 
	//will not handle quoted numbers
	fmt.Fprintf(&buf,`SELECT itemid, 
	round( multiply((toUnixTimestamp(clock)-%d), %d) / %d ,0) as i,
	max(toUnixTimestamp(clock)) as clcck ,
	avg(value_avg) as avg, 
	sum(count) as cnt, 
	min(value_min) as min , 
	max(value_max) as max 
	FROM %s.%s h 
	WHERE clock BETWEEN %d AND %d AND itemid = %d 
	GROUP BY itemid, i 
	FORMAT JSON SETTINGS output_format_json_quote_64bit_integers='0'` , 
	hr.Start, hr.Count, hr.End-hr.Start, he.dbname, trend_tables[hr.Value_type],hr.Start, hr.End, hr.Itemid);

	//log.Print("Sending trends query", buf.String());

	resp, err := http.Post(he.url, "text/html",strings.NewReader(buf.String()))

	if err != nil {
		log.Print(err)
  	} else {
		
		//log.Print("Got responce")
		body, _ := ioutil.ReadAll(resp.Body)

		if resp.StatusCode < 200 || resp.StatusCode > 299 {
			log.Println("Couldn't read data from Clickhouse on request:",buf.String())	
			log.Println(string(body)) 
			log.Println(resp)   
	  	} else {
			
			//log.Print(string(body)) 
			//log.Print("Parsing responce");
			//todo - as soon as we have some data, create parsing here and returning it as metrics
			v,err := he.parser.Parse(string(body))
			
			if ( nil !=err ) {
				log.Panic(err)
			}
			//log.Print(string(body)) 
			//log.Print("Parsing responce");
			
			for i,metric := range v.GetArray("data") {
				m := histApi.AggMetric {
					Time: uint64(metric.GetInt64("clcck")),
					//this might need fixing
					Count: 1,
					Itemid: hr.Itemid,
					I: uint32(metric.GetInt64("i")),
				}
				
				m.Value_type=hr.Value_type
				//log.Print("Value type is ",m.Value_type)

				switch m.Value_type {
					case histApi.ITEM_VALUE_TYPE_UINT64:
						m.AvgInt = uint64(metric.GetFloat64("avg"))	
						m.MaxInt = uint64(metric.GetUint64("max"))	
						m.MinInt = uint64(metric.GetUint64("min"))	
						
					case histApi.ITEM_VALUE_TYPE_FLOAT:
						m.Avg = float64(metric.GetFloat64("avg"))
						m.Max = float64(metric.GetFloat64("max"))
						m.Min = float64(metric.GetFloat64("min"))
				}
				
				dumpf(&m,wr,i)			 
			}
		
		}
		defer resp.Body.Close()
  	}

	return nil
}


func (he ClickHouseHist) ReadAgg (hr histApi.HistoryRequest, dumpf func(*histApi.AggMetric, *bufio.Writer, int), wr* bufio.Writer, log *log.Logger) []histApi.AggMetric  {

	var buf strings.Builder
	
	//log.Print("Reading aggregated metrics")

	fmt.Fprintf(&buf, 
	`SELECT itemid, 
		round( multiply((toUnixTimestamp(clock)-%d), %d) / %d ,0) as i,
		max(toUnixTimestamp(clock)) as clcck ,
		avg(value) as avg, 
		count(value) as count, 
		min(value) as min , 
		max(value) as max 
	FROM %s.%s h 
	WHERE clock BETWEEN %d AND %d AND itemid = %d
	GROUP BY itemid, i 
	ORDER BY i FORMAT JSON 	SETTINGS output_format_json_quote_64bit_integers='0'` , 
			hr.Start, hr.Count, hr.End-hr.Start, 
				he.dbname, hist_tables[hr.Value_type],  hr.Start, hr.End, hr.Itemid);

	//log.Print("Sending agg query", buf.String());

	resp, err := http.Post(he.url, "text/html",strings.NewReader(buf.String()))

	if err != nil {
		log.Print(err)
  	} else {
		
		//log.Print("Got responce")
		body, _ := ioutil.ReadAll(resp.Body)

		if resp.StatusCode < 200 || resp.StatusCode > 299 {
			log.Println("Couldn't read data from Clickhouse on request:",buf.String())	
			log.Println(string(body)) 
			log.Println(resp)   
	  	} else {
			
			//log.Print(string(body)) 
			//log.Print("Parsing responce");
			//todo - as soon as we have some data, create parsing here and returning it as metrics
			v,err := he.parser.Parse(string(body))
			
			if ( nil !=err ) {
				log.Panic(err)
			}
			//log.Print(string(body)) 
			//log.Print("Parsing responce");
			
			for i,metric := range v.GetArray("data") {
				m := histApi.AggMetric {
					Time: uint64(metric.GetInt64("clcck")),
					Count: uint64(metric.GetInt64("count")),
					//TODO: figure why count is set to 0 in the data so far
					//Count: 1,
					Itemid: hr.Itemid,
					I: uint32(metric.GetInt64("i")),
				}
				
				m.Value_type=hr.Value_type
				//log.Print("Value type is ",m.Value_type)

				switch m.Value_type {
					case histApi.ITEM_VALUE_TYPE_UINT64:
						m.AvgInt = uint64(metric.GetFloat64("avg"))	
						m.MaxInt = uint64(metric.GetUint64("max"))	
						m.MinInt = uint64(metric.GetUint64("min"))	
						
					case histApi.ITEM_VALUE_TYPE_FLOAT:
						m.Avg = float64(metric.GetFloat64("avg"))
						m.Max = float64(metric.GetFloat64("max"))
						m.Min = float64(metric.GetFloat64("min"))
				}
				
				dumpf(&m,wr,i)			 
			}
		
		}
		defer resp.Body.Close()
  	}

	return nil
}


func (he ClickHouseHist) ReadMetrics (hr histApi.HistoryRequest, dumpf func(*histApi.Metric, *bufio.Writer, int), wr *bufio.Writer, log *log.Logger)  {
	var buf *bytebufferpool.ByteBuffer = he.buf
	buf.Reset();

	fmt.Fprintf(buf,"SELECT toUInt32(clock) clock,ns,value");
	
	if  !he.disable_ns {
		fmt.Fprintf(buf,",ns");
	}

	if hr.Value_type == histApi.ITEM_VALUE_TYPE_LOG {
		fmt.Fprintf(buf,",logeventid,severity,source")
	}
	
	fmt.Fprintf(buf, " FROM %s.%s WHERE itemid=%d",	he.dbname,hist_tables[hr.Value_type],hr.Itemid)

	if  1 == hr.End-hr.Start {
		fmt.Fprintf(buf, " AND clock = %d ", hr.End);
	} else {
		if (0 < hr.Start) {
			fmt.Fprintf(buf, " AND clock >= %d ", hr.Start);
		}
		if (0 < hr.End ) {
			fmt.Fprintf(buf, " AND clock <= %d ", hr.End);
		}
	}

	if hr.Value_type == histApi.ITEM_VALUE_TYPE_LOG {
		if hr.Logeventid > 0 {
			fmt.Fprintf(buf, " AND logeventid = %d ", hr.Logeventid)
		}
		if hr.Severity > 0 {
			fmt.Fprintf(buf, " AND severity >= %d ", hr.Severity)
		}
		if len(hr.Source) > 0 {
			fmt.Fprintf(buf, " AND source = '%s' ", hr.Source)
		}
	}

	fmt.Fprintf(buf, " ORDER BY clock DESC");

	if 0 < hr.Count	{
	    fmt.Fprintf(buf, " LIMIT %d ", hr.Count);
	}

    fmt.Fprintf(buf, " format JSON SETTINGS output_format_json_quote_64bit_integers='0'");
	
//	log.Print("Will do query:",buf.String())
	resp, err := http.Post(he.url, "text/html",strings.NewReader(buf.String()))

	if err != nil {
		log.Print(err)
  	} else {
		
		body, _ := ioutil.ReadAll(resp.Body)

		if resp.StatusCode < 200 || resp.StatusCode > 299 {
			log.Print("Couldn't read data from Clickhouse on request:",buf.String())	
		  	log.Print(string(body))  
	  	} else {
			//log.Print("Parsing responce");
			//log.Print(string(body)) 
			v,err := he.parser.Parse(string(body))
			
			if err != nil {
				log.Panic(err)
			} 

			for i,metric := range v.GetArray("data") {
				m := histApi.Metric {
					Sec: uint64(metric.GetInt64("clock")),
					Ns:  uint32(metric.GetInt64("ns")),
					
				}
				
				m.Value_type=hr.Value_type

				switch hr.Value_type {
					case histApi.ITEM_VALUE_TYPE_UINT64:
						m.Value_int = int64(metric.GetInt("value"))	
					case histApi.ITEM_VALUE_TYPE_FLOAT:
						m.Value_dbl = float64(metric.GetFloat64("value"))
					case histApi.ITEM_VALUE_TYPE_STR, histApi.ITEM_VALUE_TYPE_TEXT:
						m.Value_str = he.quotter.Replace(string(metric.GetStringBytes("value")))
					case histApi.ITEM_VALUE_TYPE_LOG:
						m.Source = string(metric.GetStringBytes("source"))
						m.Value_str = he.quotter.Replace(string(metric.GetStringBytes("value")))
						m.Logeventid = uint64(metric.GetInt("logeventid"))
						m.Severity =  uint8(metric.GetInt("severity"))
				}
				//log.Print("Parsed retruned value",m)
				dumpf(&m,wr,i)			 
			}
		}
		defer resp.Body.Close()
  	}
}

func (he ClickHouseHist) Flush () int {
	var flushed int

	for i, buffer := range he.sql_buffer {
		if ( buffer.Len() > 0 	&&  ( (*he.metrics)[i] > he.batch || he.lastflush[i] + he.flush < time.Now().Unix() ) ) {
			resp, err := http.Post(he.url, "text/html",strings.NewReader(buffer.String()))
			
			if err != nil {
      			log.Print(err)
    		} else {
			
				if resp.StatusCode < 200 || resp.StatusCode > 299 {
					body, _ := ioutil.ReadAll(resp.Body)
					println(buffer.String())
					println(string(body))  
					println(he.lastflush[i])
					println(buffer.Len())
				}
							
				resp.Body.Close()
			}
			
			buffer.Reset();
			he.lastflush[i] = time.Now().Unix()
			flushed+=(*he.metrics)[i]
		//	log.Print("Flushed metrics:",(*he.metrics)[i])
			(*he.metrics)[i]=0
		}	
	}
	for i, buffer := range he.agg_sql_buf {
		
		if ( buffer.Len() > 0 	&& ( (*he.agg_metrics)[i] > he.batch || he.agg_lastflush[i] + he.flush < time.Now().Unix() ) ) {
			resp, err := http.Post(he.url, "text/html",strings.NewReader(buffer.String()))
			
			if err != nil {
      			log.Print(err)
    		} else {
			
				if resp.StatusCode < 200 || resp.StatusCode > 299 {
					body, _ := ioutil.ReadAll(resp.Body)
					println(buffer.String())
					println(string(body))  
					println(he.lastflush[i])
					println(buffer.Len())
				}
							
				resp.Body.Close()
			}
			
			buffer.Reset();
			he.lastflush[i] = time.Now().Unix()
			flushed+=(*he.metrics)[i]
			(*he.metrics)[i]=0
		}	
	}

	//log.Print("Flushed records:",flushed)
	return flushed
}