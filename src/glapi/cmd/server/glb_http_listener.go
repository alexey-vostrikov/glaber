package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strings"
)

func hello(w http.ResponseWriter, req *http.Request) {
	stationid := req.Header.Get("Stationid");
	params := strings.Split(req.Header.Get("Periodic"), ";");

	output := make(map[string]string)
	output["stationid"] = stationid;
	
	for _, kv := range params {
		keyval := strings.Split(kv, "=");
		if (len(keyval) == 2) {
			output[keyval[0]] = keyval[1]
		}
	}

	ret, _ :=  json.Marshal(output);
	fmt.Fprintf(os.Stdout,"%s\n", ret);
}

func main() {
    http.HandleFunc("/", hello)
 	http.ListenAndServe(":8620", nil)
}