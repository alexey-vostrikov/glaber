package main

import (
        "errors"
        "fmt"
        "log/syslog"
        "os"
        "flag"
)

func main() {
        var sendUDP string
	var progName string
	var message string
		
	flag.StringVar(&sendUDP,  "s","127.0.0.1:514","UDP send address")
	flag.StringVar(&progName, "p","glaber","program name")
	flag.StringVar(&message,  "m","test message","message to send")

        flag.Parse();

        syslog_pointer, err := syslog.Dial("udp", sendUDP, syslog.LOG_WARNING|syslog.LOG_LOCAL7, "test")
        
		if err != nil {
                err_exception := errors.New("Can't connect to syslog server")
                fmt.Println(err_exception)
                os.Exit(1)
        } else {
                syslog_pointer.Warning(message)
        }
}