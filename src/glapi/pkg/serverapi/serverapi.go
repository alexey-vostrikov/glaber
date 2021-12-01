package ServerApi

import (
	"encoding/json"
	"bufio"
	"fmt"
//	"log"
)

type ServerResponse struct {
	Host       string 	`json:"host"`
	Key   	   string 	`json:"key"`
	Value  	   string 	`json:"value,omitempty"`
	Time_sec   uint64	`json:"time,omitempty"`
	Time_ns    uint64	`json:"time_ns,omitempty"`
}

func SendMetricToServer(resp *ServerResponse, writer *bufio.Writer ) {
	res, err :=json.Marshal(resp);
	if (nil != err) {
		return;
	}
	fmt.Fprintln(writer, string(res));
	//fmt.Fprintln(writer);
	//fmt.Fprintln(writer, "Hello world");
	//log.Println(string(res))
	writer.Flush()

}