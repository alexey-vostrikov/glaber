## Process this file with automake to produce Makefile.in

EXTRA_DIST = .	
	
BUILD_TIME=`date +%H:%M:%S`
BUILD_DATE=`date +"%b %_d %Y"`
GOOS=`go env GOOS`
GOARCH=`go env GOARCH`


all: build

build:
	go build -o bin cmd/history/glb_hist_clickhouse.go

clean:
	go clean ./...
	rm -f bin/glb_hist_clickhouse

install:
	go install cmd/history/glb_hist_clickhouse
	