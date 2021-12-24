package netflow9

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"unsafe"
	//"errors"
	//"log"
)

// Field names
const (
	_                           = iota // 0
	FIELD_IN_BYTES                     // 1
	FIELD_IN_PKTS                      // 2
	FIELD_FLOWS                        // 3
	FIELD_PROTOCOL                     // 4
	FIELD_SRC_TOS                      // 5
	FIELD_TCP_FLAGS                    // 6
	FIELD_L4_SRC_PORT                  // 7
	FIELD_IPV4_SRC_ADDR                // 8
	FIELD_SRC_MASK                     // 9
	FIELD_INPUT_SNMP                   // 10
	FIELD_L4_DST_PORT                  // 11
	FIELD_IPV4_DST_ADDR                // 12
	FIELD_DST_MASK                     // 13
	FIELD_OUTPUT_SNMP                  // 14
	FIELD_IPV4_NEXT_HOP                // 15
	FIELD_SRC_AS                       // 16
	FIELD_DST_AS                       // 17
	FIELD_BGP_IPV4_NEXT_HOP            // 18
	FIELD_MUL_DST_PKTS                 // 19
	FIELD_MUL_DST_BYTES                // 20
	FIELD_LAST_SWITCHED                // 21
	FIELD_FIRST_SWITCHED               // 22
	FIELD_OUT_BYTES                    // 23
	FIELD_OUT_PKTS                     // 24
	FIELD_MIN_PKT_LNGTH                // 25
	FIELD_MAX_PKT_LNGTH                // 26
	FIELD_IPV6_SRC_ADDR                // 27
	FIELD_IPV6_DST_ADDR                // 28
	FIELD_IPV6_SRC_MASK                // 29
	FIELD_IPV6_DST_MASK                // 30
	FIELD_IPV6_FLOW_LABEL              // 31
	FIELD_ICMP_TYPE                    // 32
	FIELD_MUL_IGMP_TYPE                // 33
	FIELD_SAMPLING_INTERVAL            // 34
	FIELD_SAMPLING_ALGORITHM           // 35
	FIELD_FLOW_ACTIVE_TIMEOUT          // 36
	FIELD_FLOW_INACTIVE_TIMEOUT        // 37
	FIELD_ENGINE_TYPE                  // 38
	FIELD_ENGINE_ID                    // 39
	FIELD_TOTAL_BYTES_EXP              // 40
	FIELD_TOTAL_PKTS_EXP               // 41
	FIELD_TOTAL_FLOWS_EXP              // 42
	FIELD_IPV4_SRC_PREFIX              // 43
	FIELD_IPV4_DST_PREFIX              // 44
	FIELD_FLOW_SAMPLER_ID              // 45
	FIELD_DIRECTION                    // 46
	FIELD_FORWARDING_STATUS            // 47
	FIELD_DST_AS_PEER                  // 48
	FIELD_SRC_AS_PEER                  // 49

	FIELD_BGP_NEXT_AS = 128
	FIELD_BGP_PREV_AS = 129
	FIELD_INGRESS_VRF_ID
	FIELD_EGRESS_VRF_ID
)

const (
	TYPE_MASK_NUMERIC = 1<<FIELD_IN_BYTES | 1<<FIELD_IN_PKTS | 1<<FIELD_PROTOCOL | 1<<FIELD_L4_SRC_PORT | 1<<FIELD_L4_DST_PORT | 1<<FIELD_SRC_AS | 1<<FIELD_DST_AS
	TYPE_MASK_IP4     = 1<<FIELD_IPV4_SRC_ADDR | 1<<FIELD_IPV4_DST_ADDR | 1<<FIELD_BGP_IPV4_NEXT_HOP
)

var FieldNames = map[int]string{
	1:   "IN_BYTES",
	2:   "IN_PKTS",
	3:   "FLOWS",
	4:   "PROTOCOL",
	5:   "SRC_TOS",
	6:   "TCP_FLAGS",
	7:   "L4_SRC_PORT",
	8:   "IPV4_SRC_ADDR",
	9:   "SRC_MASK",
	10:  "INPUT_SNMP",
	11:  "L4_DST_PORT",
	12:  "IPV4_DST_ADDR",
	13:  "DST_MASK",
	14:  "OUTPUT_SNMP",
	15:  "IPV4_NEXT_HOP",
	16:  "SRC_AS",
	17:  "DST_AS",
	18:  "BGP_IPV4_NEXT_HOP",
	19:  "MUL_DST_PKTS",
	20:  "MUL_DST_BYTES",
	21:  "LAST_SWITCHED",
	22:  "FIRST_SWITCHED",
	23:  "OUT_BYTES",
	24:  "OUT_PKTS",
	25:  "MIN_PKT_LNGTH",
	26:  "MAX_PKT_LNGTH",
	27:  "IPV6_SRC_ADDR",
	28:  "IPV6_DST_ADDR",
	29:  "IPV6_SRC_MASK",
	30:  "IPV6_DST_MASK",
	31:  "IPV6_FLOW_LABEL",
	32:  "ICMP_TYPE",
	33:  "MUL_IGMP_TYPE",
	34:  "SAMPLING_INTERVAL",
	35:  "SAMPLING_ALGORITHM",
	36:  "FLOW_ACTIVE_TIMEOUT",
	37:  "FLOW_INACTIVE_TIMEOUT",
	38:  "ENGINE_TYPE",
	39:  "ENGINE_ID",
	40:  "TOTAL_BYTES_EXP",
	41:  "TOTAL_PKTS_EXP",
	42:  "TOTAL_FLOWS_EXP",
	44:  "IPV4_SRC_PREFIX",
	45:  "IPV4_DST_PREFIX",
	48:  "FLOW_SAMPLER_ID",
	61:  "DIRECTION",
	89:  "FORWARDING_STATUS",
	128: "DST_AS_PEER",
	129: "SRC_AS_PEER",

	234: "INGRESS_VRF_ID",
	235: "EGRESS_VRF_ID",
}

var cacheTemplates = map[uint32]map[uint16]NetflowDataTemplate{}

//
// --- NetFlow v9 Template
//
type NetflowTemplateHeader struct {
	TemplateId uint16
	FieldsNum  uint16
}

func (nth *NetflowTemplateHeader) Size() int {
	return int(unsafe.Sizeof(*nth))
}

type NetflowTemplateField struct {
	FieldType   uint16
	FieldLength uint16
}

type NetflowDataTemplate struct {
	Header NetflowTemplateHeader
	Fields []NetflowTemplateField
}

//
//
//
type NetflowFlowsetHeader struct {
	FlowsetId     uint16
	FlowsetLength uint16
}

func (fsh *NetflowFlowsetHeader) Size() int {
	return int(unsafe.Sizeof(*fsh))
}

type NetflowDataFlowset struct {
	Header NetflowFlowsetHeader
	Flow   []map[uint16][]byte
}

type NetflowHeader struct {
	Version   uint16
	Count     uint16
	Uptime    uint32
	Timestamp uint32
	Sequence  uint32
	SourceId  uint32
}

func (nh *NetflowHeader) Size() int {
	return int(unsafe.Sizeof(*nh))
}

type NetflowPacket struct {
	Header   NetflowHeader
	Flowsets []*NetflowDataFlowset
}

func NewNetflowPacket(agentId uint32, data []byte) (*NetflowPacket, error) {
	var err error
	nfPacket := &NetflowPacket{}

	// minimal size validations.
	// TODO: nh.Size uses unsafe to calc memory usage
	// it is can be platform dependend. Tests should cover this case
	nfHeaderSize := nfPacket.Header.Size()

	if len(data) < nfHeaderSize {
		return nil, fmt.Errorf("Datagram too short. Got %d bytes instead of %d", len(data), nfHeaderSize)
	}

	// cut header from flowset
	dataHeader, dataFlowset := data[:nfHeaderSize], data[nfHeaderSize:]

	// write Netflow Packet Header
	buf := bytes.NewReader(dataHeader)
	if err = binary.Read(buf, binary.BigEndian, &nfPacket.Header); err != nil {
		return nil, err
	}

	// decode flowset
	// given agentId is used to search for proper DataTemplate
	if nfPacket.Flowsets, err = DecodeFlowset(agentId, dataFlowset); err != nil {
		return nil, err
	}

	return nfPacket, nil
}

func SetTemplate(agentId uint32, flowsetHeader NetflowFlowsetHeader, data []byte) error {
	templateHeader := NetflowTemplateHeader{}
	templateHeaderSize := templateHeader.Size()

	if len(data) < templateHeaderSize {
		return fmt.Errorf("Template too short. Got %d bytes instead of %d", len(data), templateHeaderSize)
	}
	dataTemplateHeader, data := data[:templateHeaderSize], data[templateHeaderSize:]

	buf := bytes.NewReader(dataTemplateHeader)
	err := binary.Read(buf, binary.BigEndian, &templateHeader)
	if err != nil {
		return err
	}

	template := NetflowDataTemplate{
		Header: templateHeader,
	}

	// parse template fields
	for len(data) > 0 {
		var dataField []byte

		templateField := NetflowTemplateField{}
		dataField, data = data[:4], data[4:]

		buf = bytes.NewReader(dataField)
		err = binary.Read(buf, binary.BigEndian, &templateField)
		if err != nil {
			return err
		}

		template.Fields = append(template.Fields, templateField)
	}

	// put template to cache
	if _, ok := cacheTemplates[agentId]; !ok {
		cacheTemplates[agentId] = make(map[uint16]NetflowDataTemplate)
	}
	cacheTemplates[agentId][templateHeader.TemplateId] = template

	return nil
}

func DecodeFlowset(agentId uint32, data []byte) ([]*NetflowDataFlowset, error) {
	var packetFlowsets []*NetflowDataFlowset

	for len(data) > 0 {
		flowset := &NetflowDataFlowset{}

		if len(data) < flowset.Header.Size() {
			return packetFlowsets, fmt.Errorf("Flowset header too short. Got %d bytes instead of %d", len(data), flowset.Header.Size())
		}
		dataHeader := data[:flowset.Header.Size()]

		buf := bytes.NewReader(dataHeader)
		err := binary.Read(buf, binary.BigEndian, &flowset.Header)
		if err != nil {
			return packetFlowsets, err
		}

		if len(data) != int(flowset.Header.FlowsetLength) {
			return packetFlowsets, fmt.Errorf("Flowset too different. Got %d bytes instead of %d", len(data), flowset.Header.FlowsetLength)
		}

		dataFlowset := data[flowset.Header.Size():flowset.Header.FlowsetLength]
		data = data[flowset.Header.FlowsetLength:]

		if flowset.Header.FlowsetId == 0 {
			SetTemplate(agentId, flowset.Header, dataFlowset)
		} else {
			offset := 0
			if _, ok := cacheTemplates[agentId]; !ok {
				continue
			} else if _, ok = cacheTemplates[agentId][flowset.Header.FlowsetId]; !ok {
				// unknown flowset
				// template with flowset id is not collected into cache
				continue
			}
			flowsetTemplate := cacheTemplates[agentId][flowset.Header.FlowsetId]

			// padding is possible at tail of datagramm because of alignment.
			// so we SHOULD expect some (less then 4) zerobytes at the end
			for len(dataFlowset)-offset > 4 {
				dataRecords := make(map[uint16][]byte)
				for _, field := range flowsetTemplate.Fields {
					dataRecords[field.FieldType] = dataFlowset[offset : offset+int(field.FieldLength)]
					offset += int(field.FieldLength)
				}

				flowset.Flow = append(flowset.Flow, dataRecords)
			}
		}
		packetFlowsets = append(packetFlowsets, flowset)
	}
	return packetFlowsets, nil
}
