package main

import (
  "strings"
  "fmt"
  "os"
  "encoding/csv"
  "encoding/json"
  "github.com/Sirupsen/logrus"
  "strconv"
  "net/http"
  "flag"
  "bufio"
)

type metric_t struct {
  Host      string `json:"host"`
  Item_key  string `json:"item_key"`
  Itemid    uint64 `json:"itemid"`
  Time_sec  uint64 `json:"time_sec"`
  Time_ns   uint32 `json:"time_ns"`
  Value_type uint8 `json:"value_type"`
  Value_int int64   `json:"value_int"`
  Value_dbl float64 `json:"value_dbl"`
  Value_str string `json:"value_str"`
}

//since we populate the fields to ext lib, they must start from capital letter, but we'll use hints to make compiler know 
//which fields to use for the json
type Read_request_t struct {
  Itemid  uint64   `json:"itemid"`
  Start   uint64   `json:"start"`
  End     uint64   `json:"end"`
  Count   uint64   `json:"count"`
}

var metrics_per_query int 
var clickhosue_url string 
var db_name string
var history_insert_query string
var disable_ns bool 
var save_names bool 
var mode_writer bool

var log = logrus.New()

func read_from_history(req_str []byte) []byte {

    var request Read_request_t
    
    err:=json.Unmarshal(req_str, &request)
    if ( nil != err ) {
      log.Warn(err)
      return nil
    }

    query :="SELECT  toUInt32(clock) clock,value,value_dbl,value_str"
    if ! disable_ns { 
      query += ",ns"
    }
    query = fmt.Sprintf("%s FROM %s.history_buffer WHERE itemid=%d ",query, db_name, request.Itemid )
    if  1 == request.End-request.Start {
             query = query + " AND clock =  " + strconv.Itoa(int(request.End))
    } else {
      if (0 < request.Start) {
          query = query + " AND clock > " + strconv.Itoa(int(request.Start))
      }
      if (0 < request.End ) {
        query = query +  " AND clock <= " +strconv.Itoa(int(request.End))
      }
    }
    query += " ORDER BY clock DESC "
    if (0 < request.Count) {
	    query += "LIMIT " + strconv.Itoa(int(request.Count))
	  }

    query +=" format JSON "

    //executing the query
    fmt.Println(query)
    resp, err := http.Post(clickhosue_url, "text/html", strings.NewReader(query))
    if err != nil {
      log.Fatal(err)
    } else {
      log.Info(resp)
    }
    defer resp.Body.Close()
    
    
    return nil

}

func upload_to_clickhosue(metrics_chan chan metric_t) {
   var metrics int 
   
   metric_buf := make( []*metric_t,metrics_per_query );
   
   for {
    new_metric := <-metrics_chan
    metric_buf[metrics] = &new_metric
    metrics++
    
    if  metrics == metrics_per_query {
    
      var query string=history_insert_query;
    
      for idx, metric  := range metric_buf {
        var vals string
       
        if idx > 0  {
            query +=",\n"
        }

        if !disable_ns && save_names {
            vals = fmt.Sprintf("(CAST(%d as date) ,%d,%d,%d,%f,'%s',%d,'%s','%s')",
                metric.Time_sec, metric.Itemid, metric.Time_sec, metric.Value_int, metric.Value_dbl, metric.Value_str, metric.Time_ns, metric.Host, metric.Item_key)
        } else if disable_ns && save_names {
            vals = fmt.Sprintf("(CAST(%d as date) ,%d,%d,%d,%f,'%s','%s','%s')",
                  metric.Time_sec, metric.Itemid, metric.Time_sec, metric.Value_int, metric.Value_dbl, metric.Value_str, metric.Host, metric.Item_key)
        } else if !disable_ns && !save_names {
            vals = fmt.Sprintf("(CAST(%d as date) ,%d,%d,%d,%f,'%s',%d)",
                metric.Time_sec,metric.Itemid, metric.Time_sec, metric.Value_int, metric.Value_dbl, metric.Value_str, metric.Time_ns);
        } else if disable_ns && !save_names {
            vals = fmt.Sprintf("(CAST(%d as date) ,%d,%d,%d,%f,'%s')",
              metric.Time_sec, metric.Itemid, metric.Time_sec, metric.Value_int, metric.Value_dbl, metric.Value_str);
        }
      
        metric_buf[idx]=nil
        query += vals
      
      }
    //  fmt.Fprintf(os.Stderr, query)

      resp, err := http.Post(clickhosue_url, "text/html", strings.NewReader(query))
      if err != nil {
        log.Fatal(err)
      } else {
        log.Info(resp)
      }
      defer resp.Body.Close()
      
      metrics = 0;
    } 
  } 

}

func main() {

  flag.StringVar(&clickhosue_url,"url","http://localhost:8123?user=default&password=huyabbix","url to access clickhouse via http/https inteface")
  flag.StringVar(&db_name,"dbname","zabbix","database name to upload metrics")
  flag.IntVar(&metrics_per_query,"batch",1000,"Number of metrics to group before uploading to DB ")
  flag.BoolVar(&disable_ns,"disable_ns",false, "disable writing of nanoseconds")
  flag.BoolVar(&save_names,"save_names",false, "Write host and metric names to the database")
  flag.BoolVar(&mode_writer,"writer",false, "Work as history writer")
  flag.Parse()
  
  log.Out = os.Stderr

  r := csv.NewReader(os.Stdin)
  r.Comma = ';'

  history_insert_query = fmt.Sprintf("INSERT INTO %s.history_buffer (day,itemid,clock,value,value_dbl,value_str",db_name );
  
  reader := bufio.NewReader(os.Stdin)

  if (mode_writer) {
   
    for {
      request, err := reader.ReadBytes('\n')
      if ( nil !=err ) {
        log.Warn(err)
        os.Exit(-1)
      }
      response := read_from_history(request)
      if (nil != response) {
        fmt.Print(response )
      }
    }
    os.Exit(0)
  }
  
  if  ! disable_ns {
      history_insert_query += ", ns"
  }
  if  save_names {
      history_insert_query += ",hostname, itemname"
  }
 
  history_insert_query = history_insert_query + ") VALUES "
  metric_chan:=make(chan metric_t,2)
  
  log.Info("Result sql insert query will be ",history_insert_query)
  
  go upload_to_clickhosue(metric_chan)

  for {
    var metric metric_t 

    request, err := reader.ReadBytes('\n')
    if  nil != err {
      log.Warn(err);
      os.Exit(-1)
    }
    if len(request)  > 1 {
      err:=json.Unmarshal(request, &metric)
      if ( nil != err ) {
        log.Warn(err)
        log.Warn(string(request))
        os.Exit(18)  
      }
    }

    metric_chan <- metric
  }

}