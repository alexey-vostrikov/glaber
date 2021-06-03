package main

import (
	"fmt"
	"gopkg.in/mcuadros/go-syslog.v2"
	"os"
	"flag"
	"regexp"
//	"encoding/json"
)

//a nice way to checkout the template and make sure it works:
//https://regex101.com/r/oJ4vP8/1
var templates = map[string]string {
	"nginx_combined":  `^(?P<ip>\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}) (?P<domain>[^ ]+) \- \[(?P<datetime>\d{2}\/[a-zA-Z]{3}\/\d{4}:\d{2}:\d{2}:\d{2} (?:\+|\-)\d{4})\] "(?P<method>\w+) (?P<url>[^ ]+) (?P<proto>[^ ]+)" (?P<status>\d+) (?P<bytes>\d+) "(?P<referer>[^\"]*)" "(?P<agent>[^\"]*)"(?:\s+(?P<http_x_forwarded_for>[^ ]+))?$`,
	"json": "",
}

var dateformats = map[string]string {
	"nginx_combined": "%d/%b/%Y:%H:%M:%S %z",
	"json": "",
}


//format regexp are taken from here: https://docs.fluentd.org/parser/nginx
func main() {
	var listenUDP,listenTCP,format string;
	var compiled_pattern * regexp.Regexp
	
	flag.StringVar(&listenUDP,"listenUDP","0.0.0.0:514","UDP listen address")
	flag.StringVar(&listenTCP,"listenTCP","0.0.0.0:514","TCP listen address")
	flag.StringVar(&format,"format","nginx_combined1","Log format, possible types: nginx, json, syslog")
	flag.Parse()
	
	
	if  _ , t_exist := templates[format]; !t_exist {
		fmt.Fprintf(os.Stderr,"Format %s is unsupported, exiting",format)
		os.Exit(-1)
	}
	
	if ( "json" != format) {
		compiled_pattern = regexp.MustCompile(templates[format])
		fmt.Fprintf(os.Stderr,"Regexp comiled\n");
	}

	
	channel := make(syslog.LogPartsChannel)
	handler := syslog.NewChannelHandler(channel)

	server := syslog.NewServer()
	server.SetFormat(syslog.RFC3164)
	server.SetHandler(handler)
	
	server.ListenUDP(listenUDP)
	server.ListenTCP(listenTCP)

	server.Boot()

	go func(channel syslog.LogPartsChannel, compiled_pattern *regexp.Regexp) {
		
		for logParts := range channel {
			//TODO: change the sample to output log as a correct JSON
			//jsons are left unmodified
			if ( nil != compiled_pattern ) {
				//doing regexp match
				fmt.Fprintf(os.Stderr,"\n\n\n%v",logParts)
				log_str := fmt.Sprintf("%v",logParts["content"]);

				fmt.Fprintf(os.Stderr,"template exists, searching")
				match := compiled_pattern.FindStringSubmatch(log_str)
				if (len(match) > 0 ) {
					fmt.Fprintf(os.Stderr,"parsed result is: %v",match)

					//parsing the content according to the format
					fmt.Printf("\n\n{");
					for i, name := range compiled_pattern.SubexpNames() {
						if i > 0 {
							fmt.Printf(", \"%s\":\"%s\"", name,  match[i])
						} else {
							fmt.Printf("\"%s\":\"%s\"", name, match[i])
						}
					}
					fmt.Printf("}\n");
					//os.Stdout.Flush()
		
				} else {
					fmt.Fprintf(os.Stderr,"\n\nPattern not matched to log %s",log_str)
				}
			
			} else {
				fmt.Println(logParts["content"])
			}
		//	fmt.Fprintf(os.Stderr,"%v",logParts["content"])
		}
	}(channel,compiled_pattern)

	server.Wait()
}