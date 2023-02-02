/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

package serverconnector

import (
	"crypto/md5"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"net/url"
	"reflect"
	"strings"
	"time"
	"unicode/utf8"

	"git.zabbix.com/ap/plugin-support/log"
	"git.zabbix.com/ap/plugin-support/plugin"
	"zabbix.com/internal/agent"
	"zabbix.com/internal/agent/resultcache"
	"zabbix.com/internal/agent/scheduler"
	"zabbix.com/internal/monitor"
	"zabbix.com/pkg/glexpr"
	"zabbix.com/pkg/tls"
	"zabbix.com/pkg/version"
	"zabbix.com/pkg/zbxcomms"
)

const defaultAgentPort = 10050

type Connector struct {
	clientID                   uint64
	input                      chan interface{}
	addresses                  []string
	hostname                   string
	session                    string
	configRevision             uint64
	localAddr                  net.Addr
	lastActiveCheckErrors      []error
	lastActiveHbErrors         []error
	firstActiveChecksRefreshed bool
	resultCache                resultcache.ResultCache
	taskManager                scheduler.Scheduler
	options                    *agent.AgentOptions
	tlsConfig                  *tls.Config
}

type activeChecksRequest struct {
	Request        string `json:"request"`
	Host           string `json:"host"`
	Version        string `json:"version"`
	Session        string `json:"session"`
	ConfigRevision uint64 `json:"config_revision"`
	HostMetadata   string `json:"host_metadata,omitempty"`
	HostInterface  string `json:"interface,omitempty"`
	ListenIP       string `json:"ip,omitempty"`
	ListenPort     int    `json:"port,omitempty"`
}

type activeChecksResponse struct {
	Response       string               `json:"response"`
	Info           string               `json:"info"`
	ConfigRevision uint64               `json:"config_revision,omitempty"`
	Data           []*plugin.Request    `json:"data"`
	Expressions    []*glexpr.Expression `json:"regexp"`
}

type agentDataResponse struct {
	Response string `json:"response"`
	Info     string `json:"info"`
}

type heartbeatMessage struct {
	Request            string `json:"request"`
	Host               string `json:"host"`
	HeartbeatFrequency int    `json:"heartbeat_freq"`
}

// ParseServerActive validates address list of zabbix Server or Proxy for ActiveCheck
func ParseServerActive() ([][]string, error) {

	if 0 == len(strings.TrimSpace(agent.Options.ServerActive)) {
		return [][]string{}, nil
	}

	var checkAddr string
	clusters := strings.Split(agent.Options.ServerActive, ",")

	addrs := make([][]string, len(clusters))

	for i := 0; i < len(clusters); i++ {
		addresses := strings.Split(clusters[i], ";")

		for j := 0; j < len(addresses); j++ {
			addresses[j] = strings.TrimSpace(addresses[j])
			u := url.URL{Host: addresses[j]}
			ip := net.ParseIP(addresses[j])
			if nil == ip && 0 == len(strings.TrimSpace(u.Hostname())) {
				return nil, fmt.Errorf("address \"%s\": empty value", addresses[j])
			}

			if nil != ip {
				checkAddr = net.JoinHostPort(addresses[j], "10051")
			} else if 0 == len(u.Port()) {
				checkAddr = net.JoinHostPort(u.Hostname(), "10051")
			} else {
				checkAddr = addresses[j]
			}

			if h, p, err := net.SplitHostPort(checkAddr); err != nil {
				return nil, fmt.Errorf("address \"%s\": %s", addresses[j], err)
			} else {
				addresses[j] = net.JoinHostPort(strings.TrimSpace(h), strings.TrimSpace(p))
			}

			for k := 0; k < len(addrs); k++ {
				for l := 0; l < len(addrs[k]); l++ {
					if addrs[k][l] == addresses[j] {
						return nil, fmt.Errorf("address \"%s\" specified more than once", addresses[j])
					}
				}
			}

			addrs[i] = append(addrs[i], addresses[j])
		}
	}

	return addrs, nil
}

func (c *Connector) refreshActiveChecks() {
	var err error

	a := activeChecksRequest{
		Request:        "active checks",
		Host:           c.hostname,
		Version:        version.Short(),
		Session:        c.session,
		ConfigRevision: c.configRevision,
	}

	log.Debugf("[%d] In refreshActiveChecks() from %s", c.clientID, c.addresses)
	defer log.Debugf("[%d] End of refreshActiveChecks() from %s", c.clientID, c.addresses)

	if a.HostInterface, err = processConfigItem(c.taskManager, time.Duration(c.options.Timeout)*time.Second, "HostInterface",
		c.options.HostInterface, c.options.HostInterfaceItem, agent.HostInterfaceLen, agent.LocalChecksClientID); err != nil {
		log.Errf("cannot get host interface: %s", err)
		return
	}

	if a.HostMetadata, err = processConfigItem(c.taskManager, time.Duration(c.options.Timeout)*time.Second, "HostMetadata",
		c.options.HostMetadata, c.options.HostMetadataItem, agent.HostMetadataLen, agent.LocalChecksClientID); err != nil {
		log.Errf("cannot get host metadata: %s", err)
		return
	}

	if len(c.options.ListenIP) > 0 {
		if i := strings.IndexByte(c.options.ListenIP, ','); i != -1 {
			a.ListenIP = c.options.ListenIP[:i]
		} else {
			a.ListenIP = c.options.ListenIP
		}
	}

	if c.options.ListenPort != defaultAgentPort {
		a.ListenPort = c.options.ListenPort
	}

	request, err := json.Marshal(&a)
	if err != nil {
		log.Errf("[%d] cannot create active checks request to [%s]: %s", c.clientID, c.addresses[0], err)
		return
	}

	data, errs, errRead := zbxcomms.Exchange(&c.addresses, &c.localAddr, time.Second*time.Duration(c.options.Timeout),
		time.Second*time.Duration(c.options.Timeout), request, c.tlsConfig)

	if errs != nil {
		// server is unaware if configuration is actually delivered and saves session
		if errRead != nil {
			c.configRevision = 0
		}

		if !reflect.DeepEqual(errs, c.lastActiveCheckErrors) {
			for i := 0; i < len(errs); i++ {
				log.Warningf("[%d] %s", c.clientID, errs[i])
			}
			log.Warningf("[%d] active check configuration update from host [%s] started to fail", c.clientID,
				c.hostname)
			c.lastActiveCheckErrors = errs
		}
		return
	}

	if c.lastActiveCheckErrors != nil {
		log.Warningf("[%d] active check configuration update from [%s] is working again", c.clientID, c.addresses[0])
		c.lastActiveCheckErrors = nil
	}

	var response activeChecksResponse

	err = json.Unmarshal(data, &response)
	if err != nil {
		log.Errf("[%d] cannot parse list of active checks from [%s]: %s", c.clientID, c.addresses[0], err)
		return
	}

	if response.Response != "success" {
		if len(response.Info) != 0 {
			log.Errf("[%d] no active checks on server [%s]: %s", c.clientID, c.addresses[0], response.Info)
		} else {
			log.Errf("[%d] no active checks on server [%s]", c.clientID, c.addresses[0])
		}
		c.taskManager.UpdateTasks(c.clientID, c.resultCache.(plugin.ResultWriter), c.firstActiveChecksRefreshed,
			[]*glexpr.Expression{}, []*plugin.Request{})
		c.firstActiveChecksRefreshed = true
		return
	}

	if response.Data == nil {
		if c.configRevision == 0 {
			log.Errf("[%d] cannot parse list of active checks from [%s]: data array is missing", c.clientID,
				c.addresses[0])
		}
		return
	}

	c.configRevision = response.ConfigRevision

	for i := 0; i < len(response.Data); i++ {
		if len(response.Data[i].Key) == 0 {
			if response.Data[i].Itemid == 0 {
				log.Errf("[%d] cannot parse list of active checks from [%s]: key is missing",
					c.clientID, c.addresses[0])
				return
			}

			log.Errf("[%d] cannot parse list of active checks from [%s]: key is missing for itemid '%d'",
				c.clientID, c.addresses[0], response.Data[i].Itemid)
			return
		}

		if response.Data[i].Itemid == 0 {
			log.Errf("[%d] cannot parse list of active checks from [%s]: itemid is missing for key '%s'",
				c.clientID, c.addresses[0], response.Data[i].Key)
			return
		}

		if len(response.Data[i].Delay) == 0 {
			log.Errf("[%d] cannot parse list of active checks from [%s]: delay is missing for itemid '%d'",
				c.clientID, c.addresses[0], response.Data[i].Itemid)
			return
		}

		if response.Data[i].LastLogsize == nil {
			log.Errf("[%d] cannot parse list of active checks from [%s]: lastlogsize is missing for itemid '%d'",
				c.clientID, c.addresses[0], response.Data[i].Itemid)
			return
		}

		if response.Data[i].Mtime == nil {
			log.Errf("[%d] cannot parse list of active checks from [%s]: mtime is missing for itemid '%d'",
				c.clientID, c.addresses[0], response.Data[i].Itemid)
			return
		}
	}

	for i := 0; i < len(response.Expressions); i++ {
		if len(response.Expressions[i].Name) == 0 {
			log.Errf(`[%d] cannot parse list of active checks from [%s]: cannot retrieve value of tag "name"`,
				c.clientID, c.addresses[0])
			return
		}

		if len(response.Expressions[i].Body) == 0 {
			log.Errf(`[%d] cannot parse list of active checks from [%s]: cannot retrieve value of tag "expression"`,
				c.clientID, c.addresses[0])
			return
		}

		if response.Expressions[i].Type == nil {
			log.Errf(`[%d] cannot parse list of active checks from [%s]: cannot retrieve value of tag "expression_type"`,
				c.clientID, c.addresses[0])
			return
		}

		if response.Expressions[i].Delimiter == nil {
			log.Errf(`[%d] cannot parse list of active checks from [%s]: cannot retrieve value of tag "exp_delimiter"`,
				c.clientID, c.addresses[0])
			return
		}

		if len(*response.Expressions[i].Delimiter) > 1 {
			log.Errf(`[%d] cannot parse list of active checks from [%s]: invalid tag "exp_delimiter" value "%s"`,
				c.clientID, c.addresses[0], *response.Expressions[i].Delimiter)
			return
		}

		if response.Expressions[i].Mode == nil {
			log.Errf(`[%d] cannot parse list of active checks from [%s]: cannot retrieve value of tag "case_sensitive"`,
				c.clientID, c.addresses[0])
			return
		}
	}

	c.taskManager.UpdateTasks(c.clientID, c.resultCache.(plugin.ResultWriter), c.firstActiveChecksRefreshed,
		response.Expressions, response.Data)
	c.firstActiveChecksRefreshed = true
}

func (c *Connector) sendHeartbeatMsg() {
	var err error

	h := heartbeatMessage{
		Request:            "active check heartbeat",
		HeartbeatFrequency: c.options.HeartbeatFrequency,
		Host:               c.hostname,
	}

	log.Debugf("[%d] In sendHeartbeatMsg() from %s", c.clientID, c.addresses)
	defer log.Debugf("[%d] End of sendHeartBeatMsg() from %s", c.clientID, c.addresses)

	request, err := json.Marshal(&h)
	if err != nil {
		log.Errf("[%d] cannot create heartbeat message to [%s]: %s", c.clientID, c.addresses[0], err)
		return
	}

	_, errs, _ := zbxcomms.Exchange(&c.addresses, &c.localAddr, time.Second*time.Duration(c.options.Timeout),
		time.Second*time.Duration(c.options.Timeout), request, c.tlsConfig, true)

	if errs != nil {
		if !reflect.DeepEqual(errs, c.lastActiveHbErrors) {
			for i := 0; i < len(errs); i++ {
				log.Warningf("[%d] %s", c.clientID, errs[i])
			}
			log.Warningf("[%d] sending of heartbeat message for [%s] started to fail", c.clientID,
				c.hostname)
			c.lastActiveHbErrors = errs
		}
		return
	}

	if c.lastActiveHbErrors != nil {
		log.Warningf("[%d] sending of heartbeat message to [%s] is working again", c.clientID, c.addresses[0])
		c.lastActiveHbErrors = nil
	}
}

func (c *Connector) run() {
	var lastRefresh time.Time
	var lastFlush time.Time
	var lastHeartbeat time.Time

	defer log.PanicHook()
	log.Debugf("[%d] starting server connector for %s", c.clientID, c.addresses)

	ticker := time.NewTicker(time.Second)
run:
	for {
		select {
		case <-ticker.C:
			now := time.Now()
			if now.Sub(lastFlush) >= time.Second*time.Duration(c.options.BufferSend) {
				c.resultCache.Upload(nil)
				lastFlush = now
			}
			if now.Sub(lastRefresh) > time.Second*time.Duration(c.options.RefreshActiveChecks) {
				c.refreshActiveChecks()
				lastRefresh = time.Now()
			}
			if now.Sub(lastHeartbeat) > time.Second*time.Duration(c.options.HeartbeatFrequency) {
				c.sendHeartbeatMsg()
				lastHeartbeat = time.Now()
			}
		case u := <-c.input:
			if u == nil {
				break run
			}
			switch v := u.(type) {
			case *agent.AgentOptions:
				c.updateOptions(v)
				// TODO: when runtime configuration reload is implemented the result cache active
				// connection properties must be updated too
			}
		}
	}
	log.Debugf("[%d] server connector has been stopped", c.clientID)
	monitor.Unregister(monitor.Input)
}

func (c *Connector) updateOptions(options *agent.AgentOptions) {
	c.options = options
	c.localAddr = &net.TCPAddr{IP: net.ParseIP(agent.Options.SourceIP), Port: 0}
}

func newToken() string {
	h := md5.New()
	_ = binary.Write(h, binary.LittleEndian, time.Now().UnixNano())

	return hex.EncodeToString(h.Sum(nil))
}

func New(taskManager scheduler.Scheduler, addresses []string, hostname string,
	options *agent.AgentOptions) (connector *Connector, err error) {
	c := &Connector{
		taskManager: taskManager,
		addresses:   addresses,
		hostname:    hostname,
		input:       make(chan interface{}, 10),
		clientID:    agent.NewClientID(),
		session:     newToken(),
	}

	c.updateOptions(options)
	if c.tlsConfig, err = agent.GetTLSConfig(c.options); err != nil {
		return
	}

	ac := &activeConnection{
		addresses: addresses,
		hostname:  hostname,
		localAddr: c.localAddr,
		tlsConfig: c.tlsConfig,
		timeout:   options.Timeout,
		session:   c.session,
	}
	c.resultCache = resultcache.New(&agent.Options, c.clientID, ac)

	return c, nil
}

func (c *Connector) Start() {
	c.resultCache.Start()
	monitor.Register(monitor.Input)
	go c.run()
}

func (c *Connector) StopConnector() {
	c.input <- nil
}

func (c *Connector) StopCache() {
	c.resultCache.Stop()
}

func (c *Connector) UpdateOptions() {
	c.input <- &agent.Options
}

func processConfigItem(taskManager scheduler.Scheduler, timeout time.Duration, name, value, item string, length int, clientID uint64) (string, error) {
	if len(item) > 0 {
		if len(value) > 0 {
			log.Warningf("both \"%s\" and \"%sItem\" configuration parameter defined, using \"%s\"", name, name, name)
			return value, nil
		}

		var err error
		value, err = taskManager.PerformTask(item, timeout, clientID)
		if err != nil {
			return "", err
		}

		if !utf8.ValidString(value) {
			return "", fmt.Errorf("value is not a UTF-8 string")
		}

		if utf8.RuneCountInString(value) > length {
			log.Warningf("the returned value of \"%s\" item specified by \"%sItem\" configuration parameter"+
				" is too long, using first %d characters", item, name, length)

			return agent.CutAfterN(value, length), nil
		}
	}

	return value, nil
}

func (c *Connector) ClientID() uint64 {
	return c.clientID
}
