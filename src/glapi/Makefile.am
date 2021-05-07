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

clean:
	go clean ./...
	rm glb_hist_clickhouse
	rm glb_hist_victoria

install:
	go install cmd/history/glb_hist_clickhouse
	go install cmd/history/glb_hist_victoria
	