package main

import (
  "fmt"
  "regexp"
)

var myExp = regexp.MustCompile(`(?P<first>\d+)\.(\d+).(?P<second>\d+)`)

func main() {
  fmt.Printf("%+v", myExp.FindStringSubmatch("1234.5678.9"))

  match := myExp.FindStringSubmatch("1234.5678.9")
    for i, name := range myExp.SubexpNames() {
        fmt.Printf("'%s'\t %d -> %s\n", name, i, match[i])
    }
    //fmt.Printf("by name: %s %s\n", match["first"], match["second"])
}