package main

import (
	"encoding/binary"
	"compress/gzip"
    "encoding/csv"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"net/http"
	"io"
	"strconv"
	"sort"
	"glaber.io/glapi/pkg/netflow9"
	"encoding/json"
	"bufio"
	"bytes"
)

type NetflowDatagramm struct {
	AgentId uint32
	Buffer  []byte
}

type Flow struct {
	AgentIp   uint32	`json:"-"`
	Agent	  string
	AgentIf   uint32
	SrcPrefix uint32	`json:"-"`
	SrcNet	  string
	DstPrefix uint32	`json:"-"`
	DstNet	  string
	SrcIp     uint32	`json:"-"`
	Src		  string
	Dst		  string
	DstIp     uint32	`json:"-"`
	SrcMask   byte		`json:"-"`
	DstMask   byte		`json:"-"`
	SrcPort   uint16
	DstPort   uint16
	Bytes     uint64
	Pkts      uint32
	Protocol  byte
	
	SrcAS	  uint32
	DstAS     uint32
	SrcCountry *string
	DstCountry *string
	SrcOrg	*string
	DstOrg	*string
}

type ASNRecord struct {
	StartIP uint32
	EndIP	uint32
	AS		uint32
	Country string
	Org	string
}

type ServerResponse struct {
	Host       string 	`json:"host"`
	Key   	   string 	`json:"key"`
	Value  	   string 	`json:"value,omitempty"`
	Time_sec   uint64	`json:"time,omitempty"`
	Time_ns    uint64	`json:"time_ns,omitempty"`
}

/*
var (
	IS74Prefixes             = []net.IPNet{}
	extendedFlowSpecPrefixes = []net.IPNet{}
	stats                    = map[uint32]*RRDDB{}
	perIpStats               = map[uint32]*RRDDB{}
)
*/
func ip2int(ip net.IP) uint32 {
	if len(ip) == 16 {
		return binary.BigEndian.Uint32(ip[12:16])
	}
	return binary.BigEndian.Uint32(ip)
}

func int2ip(nn uint32) net.IP {
	ip := make(net.IP, 4)
	binary.BigEndian.PutUint32(ip, nn)
	return ip
}

func dumpFlows(chFlows chan Flow, ASNs []ASNRecord, writer *bufio.Writer) {

	for flow := range chFlows {
		
		var dstnet bytes.Buffer
		var srcnet bytes.Buffer

		dstASN := findASNbyIP(flow.DstIp ,ASNs[:])
		if ( nil == dstASN) {
			continue
		}
		flow.DstAS = dstASN.AS
		flow.DstCountry = &dstASN.Country
		flow.DstOrg = &dstASN.Org

		srcASN := findASNbyIP(flow.SrcIp ,ASNs[:])
		if ( nil == srcASN) {
			continue
		}
		
		flow.SrcAS = srcASN.AS
		flow.SrcCountry = &srcASN.Country
		flow.SrcOrg = &srcASN.Org

		fmt.Fprintf(&dstnet,"%s/%d",(int2ip(flow.DstPrefix)).String(),flow.DstMask);
		fmt.Fprintf(&srcnet,"%s/%d",(int2ip(flow.SrcPrefix)).String(),flow.SrcMask);
		flow.DstNet = dstnet.String();
		flow.SrcNet = srcnet.String();
		flow.Agent = (int2ip(flow.AgentIp)).String()

	//	log.Print("Got flow ", int2ip(flow.SrcIp),"/",flow.SrcMask,"(", int2ip(flow.SrcPrefix),"/",flow.SrcMask,") ->", 
	//						   int2ip(flow.DstIp),"/",flow.DstMask,"(", int2ip(flow.DstPrefix),"/",flow.DstMask,"); AS: ", srcASN.AS, "->", dstASN.AS, 
	//		"; Country: ", srcASN.Country, "->", dstASN.Country, "; Org: '", srcASN.Ident, "' -> '", dstASN.Ident, "'")
	res, err :=json.Marshal(flow);
	if (nil != err) {
		return;
	}
	fmt.Fprintln(writer, string(res));
	writer.Flush()
		
	}
}

func downloadASNFile(filepath string) (err error) {

	out, err := os.Create(filepath)
	if err != nil  {
	  return err
	}
	defer out.Close()
  
	resp, err := http.Get("https://iptoasn.com/data/ip2asn-v4-u32.tsv.gz")
	if err != nil {
	  return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
	  return fmt.Errorf("bad status: %s", resp.Status)
	}
  
	_, err = io.Copy(out, resp.Body)
	if err != nil  {
	  return err
	}
	log.Print("ASN file has been downloaded")
	return nil
}

func readASNfile(path string, ASN *[]ASNRecord) {
	log.Print("Reading the ASN file");
	log.Print("Asn array size is ", len(*ASN))

	f, err := os.Open(path)
    if err != nil {
        log.Fatal(err)
    }
    defer f.Close()

    gr, err := gzip.NewReader(f)
    if err != nil {
        log.Fatal(err)
    }
    defer gr.Close()

    cr := csv.NewReader(gr)

	cr.FieldsPerRecord = -1
	cr.LazyQuotes = true
	cr.Comma = '\t'

	for {
		rec, err := cr.Read()
    	if err != nil {
			
			if err == io.EOF {
                err = nil
				return
            }
			
			log.Fatal(err)
    	}
		var record ASNRecord
		
		as, _ := strconv.Atoi(rec[2])
		if (as > 0) {
			start_ip , _ := strconv.Atoi(rec[0])
			record.StartIP = uint32(start_ip)
			end_ip, _ := strconv.Atoi(rec[1])
			record.EndIP = uint32(end_ip)
			record.AS = uint32(as) 
			record.Country = rec[3]
			record.Org = rec[4]
	
			*ASN = append(*ASN, record)
		}
	}
}

func findASNbyIP(ip uint32, ASNs []ASNRecord) *ASNRecord {

	idx := sort.Search(len(ASNs), func(i int) bool { 
				//log.Printf("Search checking index %d Start is %d, ip is %d, end is %d", i,  ASNs[i].StartIP ,ip,  ASNs[i].EndIP)
				return ASNs[i].EndIP >= ip })
	if (idx < len(ASNs)) {
		return &ASNs[idx]
	} else {
		return nil
	}
}

func parseNetflow(buf chan NetflowDatagramm, chFlows chan Flow) {
	for data := range buf {
		pData, err := netflow9.NewNetflowPacket(data.AgentId, data.Buffer)
		if err != nil {
			fmt.Println(err)
			continue
		}

		for _, flowset := range pData.Flowsets {
			for _, flow := range flowset.Flow {
				var srcIp net.IP
				var srcMask uint8
				var dstIp net.IP
				var dstMask uint8

				var flowData = &Flow{
					AgentIp: data.AgentId,
				}

				for recordType, recordValue := range flow {
					switch recordType {
					case netflow9.FIELD_IPV4_SRC_ADDR:
						srcIp = net.IPv4(recordValue[0], recordValue[1], recordValue[2], recordValue[3])
						flowData.SrcIp = binary.BigEndian.Uint32(recordValue)
					case netflow9.FIELD_SRC_MASK:
						srcMask = recordValue[0]
						flowData.SrcMask = recordValue[0]
					case netflow9.FIELD_IPV4_DST_ADDR:
						dstIp = net.IPv4(recordValue[0], recordValue[1], recordValue[2], recordValue[3])
						flowData.DstIp = binary.BigEndian.Uint32(recordValue)
					case netflow9.FIELD_DST_MASK:
						dstMask = recordValue[0]
						flowData.DstMask = recordValue[0]
					case netflow9.FIELD_IN_BYTES:
						if len(recordValue) == 4 {
							flowData.Bytes = uint64(binary.BigEndian.Uint32(recordValue))
						} else {
							flowData.Bytes = binary.BigEndian.Uint64(recordValue)
						}
					case netflow9.FIELD_IN_PKTS:
						flowData.Pkts = binary.BigEndian.Uint32(recordValue)
					case netflow9.FIELD_INPUT_SNMP:
						flowData.AgentIf = binary.BigEndian.Uint32(recordValue)
					case netflow9.FIELD_L4_SRC_PORT:
						flowData.SrcPort = binary.BigEndian.Uint16(recordValue)
					case netflow9.FIELD_L4_DST_PORT:
						flowData.DstPort = binary.BigEndian.Uint16(recordValue)
					case netflow9.FIELD_PROTOCOL:
						flowData.Protocol = recordValue[0]
					}
				}
				if len(srcIp) == 0 {
					fmt.Println("Zero src ip, skip packet")
					continue
				}

		
				flowData.SrcPrefix = ip2int(srcIp) & (4294967295 << (32 - srcMask))
				flowData.DstPrefix = ip2int(dstIp) & (4294967295 << (32 - dstMask))

				chFlows <- *flowData
				continue
			}
		}
	}
}


func main() {
	srcIp := flag.String("src_ip", "0.0.0.0", "Source IP (receving socket for conntrackd)")
	srcPort := flag.Int("src_port", 2055, "Source port (receving socket for conntrackd)")
	asnfile := flag.String("asn_file", "ip2asn.gz", "IP 2 ASN with addrs in uint32 file (will be auto - downloaded from https://iptoasn.com/data/ip2asn-v4-u32.tsv.gz")
	var ASNs []ASNRecord

	flag.Parse()

	downloadASNFile(*asnfile) 
	readASNfile(*asnfile, &ASNs);

	chFlowsDispatcher := make(chan Flow, 0)

	writer := bufio.NewWriter(os.Stdout)

	go dumpFlows(chFlowsDispatcher, ASNs[:], writer)

	addr := net.UDPAddr{
		IP:   net.ParseIP(*srcIp),
		Port: *srcPort,
	}

	sock, err := net.ListenUDP("udp", &addr)
	if err != nil {
		fmt.Print(err)
		log.Fatal(err)
	}
	defer sock.Close()

	parsers := make(map[uint32]chan NetflowDatagramm)

	for {
		buf := make([]byte, 1500)
		
		n, src_addr, err := sock.ReadFromUDP(buf)
		if err != nil {
			fmt.Print(err)
			log.Fatal(src_addr, err)
		}
		agentId := ip2int(src_addr.IP)
		//log.Print("Got udp packet from agent ",src_addr.IP)
		
		if _, ok := parsers[agentId]; !ok {
			log.Print("Running new parser for agent:",src_addr.IP)
			parsers[agentId] = make(chan NetflowDatagramm, 0)
			go parseNetflow(parsers[agentId], chFlowsDispatcher)

		}

		nfDgramm := NetflowDatagramm{AgentId: agentId, Buffer: buf[:n]}
		parsers[agentId] <- nfDgramm
	}
}
