## Process this file with automake to produce Makefile.in

EXTRA_DIST = .	
	
BUILD_TIME=`date +%H:%M:%S`
BUILD_DATE=`date +"%b %_d %Y"`
GOOS=`go env GOOS`
GOARCH=`go env GOARCH`


all: build

build:
	go build cmd/server/glb_syslog_worker.go
	go build cmd/history/glb_hist_victoria.go
	go build cmd/server/glb_ripe_subscriber.go
	go build cmd/server/glb_netflow_collector.go
	go build exec/glb_syslog_sender.go
clean:
	go clean ./...
	rm glb_syslog_worker
	rm glb_hist_victoria
	rm glb_ripe_subscriber
	rm glb_netflow_collector
	rm glb_syslog_sender

install:
#	go install cmd/history/glb_syslog_worker
#	go install cmd/history/glb_hist_victoria
