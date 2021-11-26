package historyVictoria

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
	"strconv"
)

type VictoriaHist struct {
	baseurl string
	writeurl string
	readurl string
	dbname string
	batch int
	flush int64
	disable_ns bool
	save_names bool
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

const MAX_VMETRICS_VALUES = 20000
const MAX_VMETRICS_TIMEFRAME = 86400


func Init(he *VictoriaHist, url string, dbname string, batch int, flush int, disable_ns bool, save_names bool )   {

	he.baseurl = url
	he.writeurl = url + "/write"
	he.readurl = url + "/api/v1/export"
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
	he.buf = bytebufferpool.Get()
	
	for i := 0; i < histApi.ITEM_VALUE_TYPE_MAX; i++ {
		he.sql_buffer[i]=bytebufferpool.Get()
		he.agg_sql_buf[i]=bytebufferpool.Get()
	}
	
	he.quotter = strings.NewReplacer(",","\\,"," ","\\_")

}


func (he VictoriaHist) WriteMetrics (metric *histApi.Metric, log *log.Logger) {
	var buf = he.sql_buffer[metric.Value_type]

	fmt.Fprintf(buf,"item_%d,dbname=%s,hostname=%s,itemname=%s value=",
				metric.Itemid,	
				he.dbname,
				he.quotter.Replace(metric.Host),
				he.quotter.Replace(metric.Item_key) );
	
	switch metric.Value_type {
		case histApi.ITEM_VALUE_TYPE_UINT64:
			fmt.Fprintf(buf,"%d ",metric.Value_int)
		case histApi.ITEM_VALUE_TYPE_FLOAT:
			fmt.Fprintf(buf,"%f ",metric.Value_dbl)
		default:
			log.Panic("Unsupported value type: ", metric.Value_type)
	}

	fmt.Fprintf(buf," %d%09d\n", metric.Sec, metric.Ns) 

	(*he.metrics)[metric.Value_type]++
}

func (he VictoriaHist) WriteTrends(agg_metric *histApi.AggMetric, log *log.Logger) {
	
	var buf = he.agg_sql_buf[agg_metric.Value_type]

	fmt.Fprintf(buf,"trend_%d,dbname=%s,hostname=%s,itemname=%s",
				agg_metric.Itemid,
				he.dbname,
				he.quotter.Replace(agg_metric.Host),
				he.quotter.Replace(agg_metric.Item_key));
	
	switch agg_metric.Value_type {
		case histApi.ITEM_VALUE_TYPE_UINT64:
			fmt.Fprintf(buf," max=%d,min=%d,avg=%d",agg_metric.MaxInt, agg_metric.MinInt, agg_metric.AvgInt)
		case histApi.ITEM_VALUE_TYPE_FLOAT:
			fmt.Fprintf(buf," max=%f,min=%f,avg=%f",agg_metric.Max, agg_metric.Min, agg_metric.Avg)
		default:
			log.Panic("Unsupported value type: ", agg_metric.Value_type)
	}

	fmt.Fprintf(buf,",count=%d %d000000000\n",agg_metric.Count, agg_metric.Time) 

	//log.Print(buf)
	(*he.metrics)[agg_metric.Value_type]++
	
}

//the proper way of using the export - for getting trends and items
//using query range for trends extraction
//http://localhost:8428/api/v1/query_range?query=(trend_227269603_avg{dbname=%22glaber%22}%20or%20trend_227269603_max{dbname=%22glaber%22}%20or%20trend_227269603_min{dbname=%22glaber%22})&start=1619690879&end=1620295679&step=1800s
func (he VictoriaHist) ReadTrends (hr histApi.HistoryRequest, dumpf func(*histApi.AggMetric, *bufio.Writer, int), wr* bufio.Writer, log *log.Logger) []histApi.AggMetric  {

	var buf strings.Builder
	var step uint64
	var maxname, minname, avgname string;
	var  vmax, vmin, vavg []*fastjson.Value;

	if ( 0 == hr.Count ) {
		step = 1800
	} else {
		step =  (hr.End - hr.Start ) / hr.Count
	}

	fmt.Fprintf(&buf, "%s/api/v1/query_range?query=(trend_%d_avg{dbname=\"%s\"},trend_%d_min{dbname=\"%s\"},trend_%d_max{dbname=\"%s\"})&start=%d&end=%d&step=%dms",
		he.baseurl, hr.Itemid, he.dbname,hr.Itemid, he.dbname,hr.Itemid, he.dbname, hr.Start, hr.End, step*1000)

	//log.Print("Sending trends query: ", buf.String());

	resp, err := http.Get(buf.String() )
	defer resp.Body.Close()

	if err != nil {
		log.Print(err)
		return nil
  	} 
		
	body, _ := ioutil.ReadAll(resp.Body)

	if resp.StatusCode < 200 || resp.StatusCode > 299 {
		log.Println("Couldn't read data from VictoriaMetrics on request:",buf.String())	
		log.Println(string(body)) 
		log.Println(resp)   
		return nil

	} 
	
	v,err := he.parser.Parse(string(body))
	
	if ( nil !=err ) {
		log.Print(err)
		return nil
	}
	
	//the response will have matrix of 3 metrics  trend_XXXX_min, _max and  _avg 
	
	
	maxname="trend_"+strconv.FormatInt(int64(hr.Itemid),10)+"_max"
	minname="trend_"+strconv.FormatInt(int64(hr.Itemid),10)+"_min"
	avgname="trend_"+strconv.FormatInt(int64(hr.Itemid),10)+"_avg"

	for _,metric := range v.GetArray("data","result") {
		
		switch (string(metric.GetStringBytes("metric","__name__"))) {
		case maxname:
			vmax = metric.GetArray("values")
		case minname:
			vmin = metric.GetArray("values")
		case avgname:
			vavg = metric.GetArray("values")
		}
	}

	//prep metric common fields
	m := histApi.AggMetric {
		Count: 1,
		Itemid: hr.Itemid,
	}

	m.Value_type = hr.Value_type
			
	//iterating over all the values we assune that trends where filled by the module
	//and trend_XXXX_* metrics has same timing and dimensions 
	for i := range vavg {
	
		m.Time = uint64(vavg[i].GetArray()[0].GetInt64())	
		m.I =  uint32( (m.Time - hr.Start)/step )
			
		//values arrive as strings
		max_s := string(vmax[i].GetArray()[1].GetStringBytes())
		min_s := string(vmin[i].GetArray()[1].GetStringBytes())
		avg_s := string(vavg[i].GetArray()[1].GetStringBytes())

		switch (m.Value_type) {
			case histApi.ITEM_VALUE_TYPE_UINT64:
				max_i,err1 := strconv.Atoi(max_s)
				min_i,err2 := strconv.Atoi(min_s)	
				avg_f,err3 := strconv.ParseFloat(avg_s,64)
					
				if ( nil != err1 || nil != err2 || nil != err3) {
						log.Print(err1,err2,err3)
						return nil
				}
					
				m.AvgInt = uint64(avg_f)
				m.MaxInt = uint64(max_i)	
				m.MinInt = uint64(min_i)	
				
			case histApi.ITEM_VALUE_TYPE_FLOAT:
				max_f,err1 := strconv.ParseFloat(max_s,64)
				min_f,err2 := strconv.ParseFloat(min_s,64)	
				avg_f,err3 := strconv.ParseFloat(avg_s,64)
					
				if ( nil != err1 || nil != err2 || nil != err3) {
					log.Panic(err1,err2,err3)
				}
					
				m.Avg = float64(avg_f)
				m.Max = float64(max_f)	
				m.Min = float64(min_f)	

		}
		
		dumpf(&m,wr,i)			 
	} 
		
	return nil
}

func (he VictoriaHist) ReadAgg (hr histApi.HistoryRequest, dumpf func(*histApi.AggMetric, *bufio.Writer, int), wr* bufio.Writer, log *log.Logger) []histApi.AggMetric  {

	var url strings.Builder
	var step uint64
	var  vmax, vmin, vavg []*fastjson.Value

	if (0 == hr.Start || 0 == hr.End) {
		return nil
	}

	if ( 0 == hr.Count  || (MAX_VMETRICS_VALUES > hr.End - hr.Start) ) {
		step =  (hr.End - hr.Start) / MAX_VMETRICS_VALUES
	} else {
		step = (hr.End-hr.Start)/hr.Count
	}

	if ( 0 == step ) {
		step = 1
	}

	//one more check, if there are more then MAX_VMETRIC_POINTS, then change the step to fit
	if (MAX_VMETRICS_VALUES < (hr.End - hr.Start) / step ) {
		step = (hr.End - hr.Start) / MAX_VMETRICS_VALUES
	}
	
	fmt.Fprintf(&url, "%s/api/v1/query_range?query=(rollup(item_%d_value))&start=%d&end=%d&step=%dms",
		he.baseurl, hr.Itemid, hr.Start, hr.End, step*1000)
	
	//log.Print("Sending agg query", url.String());
	resp, err := http.Get(url.String() )

	if err != nil {
		log.Print(err)
		return nil
  	} 
		
	body, _ := ioutil.ReadAll(resp.Body)
	v,err := he.parser.Parse(string(body))
	
//log.Print("Got result ",hr.Itemid, ":", string(body))

	if ( nil !=err ) {
		log.Print(err)
		return nil
	}
	
	for _,metric := range v.GetArray("data","result") {
					
		switch (string(metric.GetStringBytes("metric","rollup"))) {
			case "max":
				vmax = metric.GetArray("values")
			case "min":
				vmin = metric.GetArray("values")
			case "avg":
				vavg = metric.GetArray("values")
		}
	}

	m := histApi.AggMetric {
		Count: 1,
		Itemid: hr.Itemid,
	}

	m.Value_type = hr.Value_type

	for i := range vavg {
		
		m.Time = uint64(vavg[i].GetArray()[0].GetInt64())	
		m.I =  uint32( (m.Time - hr.Start)/step )
			
		//values arrive as strings
		max_s := string(vmax[i].GetArray()[1].GetStringBytes())
		min_s := string(vmin[i].GetArray()[1].GetStringBytes())
		avg_s := string(vavg[i].GetArray()[1].GetStringBytes())

		switch (m.Value_type) {
			case histApi.ITEM_VALUE_TYPE_UINT64:

				max_i,err1 := strconv.Atoi(max_s)
				min_i,err2 := strconv.Atoi(min_s)	
				avg_f,err3 := strconv.ParseFloat(avg_s,64)
					
				if ( nil != err1 || nil != err2 || nil != err3) {
					log.Panic(err1,err2,err3)
				}
					
				m.AvgInt = uint64(avg_f)
				m.MaxInt = uint64(max_i)	
				m.MinInt = uint64(min_i)	
				
			case histApi.ITEM_VALUE_TYPE_FLOAT:
				max_f,err1 := strconv.ParseFloat(max_s,64)
				min_f,err2 := strconv.ParseFloat(min_s,64)	
				avg_f,err3 := strconv.ParseFloat(avg_s,64)
					
				if ( nil != err1 || nil != err2 || nil != err3) {
					log.Panic(err1,err2,err3)
				}
					
				m.Avg = float64(avg_f)
				m.Max = float64(max_f)	
				m.Min = float64(min_f)	

		}
		
		dumpf(&m,wr,i)	
	}		 
	
	return nil
}
//curl  http://localhost:8428/api/v1/export -d 'match[]={__name__="item_230817717_value"}'
//curl  http://localhost:8428/api/v1/export -d 'match[]={__name__="item_230817717_value"}' -d start=2021-11-25T05:02:07Z -d end=2021-11-26T05:02:07Z
func (he VictoriaHist) ReadMetrics (hr histApi.HistoryRequest, dumpf func(*histApi.Metric, *bufio.Writer, int), wr *bufio.Writer, log *log.Logger)  {

	var buf *bytebufferpool.ByteBuffer = he.buf
	var fetch_count uint64 

	buf.Reset();
	var now int64 =  time.Now().Unix();
	//log.Print("VMetrics: Reading VM history metrics")
	if (0 == hr.End || hr.End >uint64(now)) {
		hr.End = uint64(now)
		//log.Print("End time is set to", hr.End)
	}

	if (0 == hr.Start ) {
		//if start isn't stated, assume 24hours
		//it will not be set on count fetches
		hr.Start = hr.End - 2 * 86400 
	}
	

	//if end is unset, assuming it's now
	
	//step := int32( (hr.End - hr.Start ) / hr.Count)
	
	//log.Print("Start:",hr.Start, ", end:",hr.End," count:",hr.Count)

	start := time.Unix(int64(hr.Start), 0).Format("2006-01-02T15:04:05Z")
	end := time.Unix(int64(hr.End), 0).Format("2006-01-02T15:04:05Z")

	fmt.Fprintf(buf,"%s?match[]={__name__=\"item_%d_value\"}&start=%s&end=%s",
	he.readurl, hr.Itemid, start, end)
	//log.Printf("Vmetrics url is %q",he.readurl)
	//log.Print("Vmetrics: Sending items query: ",he.readurl,  buf.String());

	resp, err := http.Get(buf.String() )

	if err != nil {
		log.Print(err)
  	} else {
		
		body, _ := ioutil.ReadAll(resp.Body)

		if resp.StatusCode < 200 || resp.StatusCode > 299 {
			log.Println("Couldn't read data from VictoriaMetrics on request:",buf.String())	
			log.Println(string(body)) 
			log.Println(resp)   
	  	} else {
			
			//log.Print("VMetrics response:", hr.Itemid, string(body)) 
			//log.Print("Parsing responce");
			
			//todo - as soon as we have some data, create parsing here and returning it as metrics
			v,err := he.parser.Parse(string(body))
			
			if ( nil !=err ) {
				if (len(body) > 0 )	{
					log.Print(err)
				}
			} else {
				//responce to query range is matrix, so using the first metric
				//log.Print("Vmetrics: parsing the response");
				
				m := histApi.Metric {
					Itemid: hr.Itemid,
					Value_type :hr.Value_type,
				}

				vals := v.GetArray("values")
				timestamps := v.GetArray("timestamps")
				
				for i, _ := range vals {
					m.Sec = uint64(timestamps[i].GetInt64()/1000)	
					//log.Print("Parsing metric, value type is", hr.Value_type)
				
					switch hr.Value_type {
						
					case  histApi.ITEM_VALUE_TYPE_FLOAT:
						float_v := vals[i].GetFloat64();
						
						m.Value_dbl = float_v
						//log.Print("Timestamp:", m.Sec, ", value:", m.Value_dbl);

					case  histApi.ITEM_VALUE_TYPE_UINT64:
						uint_val := vals[i].GetInt64();
						
						m.Value_int = int64(uint_val)
						//log.Print("Timestamp:", m.Sec, ", value:", m.Value_int);
					default: 
						continue
					}
					fetch_count++
					dumpf(&m,wr,i)
					
					if (hr.Count > 0 && fetch_count > hr.Count) {
						//prevention of extra data retrun in count mode
						return;
					}
				}
			}
			
		}
		defer resp.Body.Close()
  	}

	return
}

func (he VictoriaHist) Flush () int {

	var flushed int

	for i, buffer := range he.sql_buffer {
		if ( buffer.Len() > 0 	&&  ( (*he.metrics)[i] > he.batch || he.lastflush[i] + he.flush < time.Now().Unix() ) ) {
			resp, err := http.Post(he.writeurl, "text/html",strings.NewReader(buffer.String()))
			
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
			resp, err := http.Post(he.writeurl, "text/html",strings.NewReader(buffer.String()))
			
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