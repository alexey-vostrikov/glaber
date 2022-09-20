package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"gopkg.in/mcuadros/go-syslog.v2"
)
/* 
 go get gopkg.in/mcuadros/go-syslog.v2
*/

/*
echo -n "<165>1 2003-10-11T22:14:15.003Z mymachine.example.com evntslog - \
    ID47 [exampleSDID@32473 iut=\"3\" eventSource=\"Application\" eventID=\"1011\"][examplePriority@32473 class=\"high\"]" | nc -4u -w1 127.0.0.1 10515
*/

func main() {
	var listenUDP string

	flag.StringVar(&listenUDP,"listen","0.0.0.0:514","UDP listen address")
	flag.Parse()
		
	channel := make(syslog.LogPartsChannel)
	
	handler := syslog.NewChannelHandler(channel)

	server := syslog.NewServer()
	server.SetFormat(syslog.Automatic)
	
	server.SetHandler(handler)
	server.ListenUDP(listenUDP)
	server.Boot()

	go process_messages(channel)

	server.Wait()
}

func process_messages(channel syslog.LogPartsChannel) {
	for logParts := range channel {
		
		str, err := json.Marshal(logParts)

		if (nil != err) {
			continue
		}
		
		fmt.Println(string(str))
	}
}

