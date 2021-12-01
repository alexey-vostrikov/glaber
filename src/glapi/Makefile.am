## Process this file with automake to produce Makefile.in

EXTRA_DIST = .	
	
BUILD_TIME=`date +%H:%M:%S`
BUILD_DATE=`date +"%b %_d %Y"`
GOOS=`go env GOOS`
GOARCH=`go env GOARCH`


all: build

build:
	go build cmd/history/glb_hist_clickhouse.go
	go build cmd/history/glb_hist_victoria.go
	go build cmd/server/glb_log_listener.go
	go build cmd/server/glb_ripe_subscriber.go
clean:
	go clean ./...
	rm glb_hist_clickhouse
	rm glb_hist_victoria
	rm glb_log_listener
	rm glb_ripe_subscriber

install:
#	go install cmd/history/glb_hist_clickhouse
#	go install cmd/history/glb_hist_victoria
#	go install cmd/server/glb_log_listener
