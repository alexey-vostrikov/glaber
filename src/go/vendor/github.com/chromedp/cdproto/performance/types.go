package performance

// Code generated by cdproto-gen. DO NOT EDIT.

import (
	"fmt"

	"github.com/mailru/easyjson"
	"github.com/mailru/easyjson/jlexer"
	"github.com/mailru/easyjson/jwriter"
)

// Metric run-time execution metric.
//
// See: https://chromedevtools.github.io/devtools-protocol/tot/Performance#type-Metric
type Metric struct {
	Name  string  `json:"name"`  // Metric name.
	Value float64 `json:"value"` // Metric value.
}

// EnableTimeDomain time domain to use for collecting and reporting duration
// metrics.
//
// See: https://chromedevtools.github.io/devtools-protocol/tot/Performance#method-enable
type EnableTimeDomain string

// String returns the EnableTimeDomain as string value.
func (t EnableTimeDomain) String() string {
	return string(t)
}

// EnableTimeDomain values.
const (
	EnableTimeDomainTimeTicks   EnableTimeDomain = "timeTicks"
	EnableTimeDomainThreadTicks EnableTimeDomain = "threadTicks"
)

// MarshalEasyJSON satisfies easyjson.Marshaler.
func (t EnableTimeDomain) MarshalEasyJSON(out *jwriter.Writer) {
	out.String(string(t))
}

// MarshalJSON satisfies json.Marshaler.
func (t EnableTimeDomain) MarshalJSON() ([]byte, error) {
	return easyjson.Marshal(t)
}

// UnmarshalEasyJSON satisfies easyjson.Unmarshaler.
func (t *EnableTimeDomain) UnmarshalEasyJSON(in *jlexer.Lexer) {
	v := in.String()
	switch EnableTimeDomain(v) {
	case EnableTimeDomainTimeTicks:
		*t = EnableTimeDomainTimeTicks
	case EnableTimeDomainThreadTicks:
		*t = EnableTimeDomainThreadTicks

	default:
		in.AddError(fmt.Errorf("unknown EnableTimeDomain value: %v", v))
	}
}

// UnmarshalJSON satisfies json.Unmarshaler.
func (t *EnableTimeDomain) UnmarshalJSON(buf []byte) error {
	return easyjson.Unmarshal(buf, t)
}
