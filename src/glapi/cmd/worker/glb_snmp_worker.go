
package main

import (
	"bufio"
	"fmt"
	"os"
	"time"

	"gitlab.com/mikler/gosnmpquerier"
)

// Example of execution
//	go run examples/snmpqueries/snmpqueries.go
// The programs wait to a json input from command line
//	{"command":"get", "destination":"bbr1-chel1", "community":"isread", "oids":["IF-MIB::ifIndex.849"]}

const (
	CONTENTION = 4
)

func readLinesFromStdin(inputLines chan string) {
	reader := bufio.NewReader(os.Stdin)
	for {
		line, err := reader.ReadString('\n')
		if err != nil {
			close(inputLines)
			return
		}
		inputLines <- line
	}
}
//translates string oids to numerical representation using snmptranslate util
//also caches the translations to speed up execution
//snmptranslate -Le -m all -On  IF-MIB::ifIndex.849
//.1.3.6.1.2.1.2.2.1.1.849
func translateOID(str string) string {

}

func hasLetters(str string) {
	
}
func readQueriesFromStdin(input chan gosnmpquerier.Query) {

	inputLines := make(chan string, 10)
	go readLinesFromStdin(inputLines)

	queryId := 0
	for line := range inputLines {
		query, err := gosnmpquerier.FromJson(line)

		//parsing oids to translate strings to digital asn tree
		for i, oid := range query.Oids {
			fmt.Println("Parsing oid ", oid)
			
			if (hasLetters(oid)) {
				
				fmt.Println("Translating oid with letters", oid)
				parts := strings.SplitN(oid, ".", 2)
				
				if sizeof(parts) == 2 {
					translated := translateOID(parts[0])
					fmt.Sprintf(query.Oids[i],"%s,%s",trasnslated,parts[1])
					fmt.Println("Resulting oid is", query.Oids[i])
				}
			} 
		}
		if err != nil {
			fmt.Println("Invalid line:", line, err)
		} else {
			query.Id = queryId
			input <- *query
			queryId += 1
		}
	}
	close(input)
}

func printResults(processed chan gosnmpquerier.Query) {
	for query := range processed {
		fmt.Printf("Result %+v", query)
	}
}

func main() {
	querier := gosnmpquerier.NewAsyncQuerier(CONTENTION, 3, 3*time.Second)

	go readQueriesFromStdin(querier.Input)

	printResults(querier.Output)
}