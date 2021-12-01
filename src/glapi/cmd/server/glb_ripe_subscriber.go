//ripe notifications subscriber worker for Glaber
package main

import (
	"glaber.io/glapi/pkg/serverapi"
	"net/http"
	"bufio"
	"log"
	"flag"
	"fmt"
	"os"
)

//curl -s "https://ris-live.ripe.net/v1/stream/?format=json&client=as8369-test" -H 'X-RIS-Subscribe: {"path": 8369}'

func main () {
	var live_url,as string
	var response ServerApi.ServerResponse

	flag.StringVar(&live_url,"url", "https://ris-live.ripe.net/v1/stream/?format=json","Ripe live event feed URL")
	flag.StringVar(&as, "as", "6666","AS NUMBER for feed name and filtering")
	flag.StringVar(&response.Host, "host", "test", "Hostname in the Glaber configuration")
	flag.StringVar(&response.Key, "key", "test_key", "Item key in the Glaber configuration")
	flag.Parse()

	filter :=fmt.Sprintf("{\"path\": %s}",as)
	url := fmt.Sprintf("%s&client=as%s-test",live_url, as);
	
	client := http.Client{}
	req , err := http.NewRequest("GET", url, nil)
	
	if err != nil {
		log.Print("Cannot create new request, exiting")	
	}
	
	req.Header.Set("X-RIS-Subscribe", filter);
	resp , err := client.Do(req)
	
	if err != nil {
		log.Print("Couldn't send new request, exiting")	
	}

	reader := bufio.NewReader(resp.Body)
	writer := bufio.NewWriter(os.Stdout)

	for {
    	line, err := reader.ReadBytes('\n')
		if (nil != err) {
			log.Fatal(err)
		}
		response.Value = string(line);
				
		ServerApi.SendMetricToServer(&response, writer)		
	}
}