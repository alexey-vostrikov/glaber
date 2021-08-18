//simple checker can check a list of simple services:
//tcp: ssh, ldap, smtp, ftp, http, pop, nntp, imap, tcp, https, telnet
//udp: ntp
//full list might be found here: 
//https://www.zabbix.com/documentation/current/manual/appendix/items/service_check_details
package main

import (
	"glaber.io/glapi/pkg/pollapi"
)

func main () {
	//we will be recieving itemid, then hostname or ip and then the key to check
	
}