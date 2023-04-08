import React from "react";
import HostAdminMenu from "./HostAdminMenu";
import HostStatus from "./HostStatus";
import HostAvailabilityStatus from "./HostAvailabilityStatus";
import HostLifeTimeIndicator from "./HostLifeTimeIndicator";
import HostProblemStatus from "./HostProblemStatus";
import { Trans } from 'react-i18next';
import {HostTemplateLink, MVCLink} from './Links';
import {GRAPH_FILTER_HOST, ZBX_FLAG_DISCOVERY_CREATED} from '../common/constants';
class HostNav extends React.Component {
    constructor(props) {
        super(props);
        console.log(this.props);
    }

    getHostDiscovery(discoveryStr){
        return JSON.parse(discoveryStr);
    }

    render() {
        let content = '';
        if(this.props.is_template){
            content = this.renderTemplateItems();
        }else{
            content = this.renderHostItems();
        }

        return <ul>{content}</ul>;
    }

    renderTemplateItems() {
        let id = this.props.templateid;
        let name = this.props.name;
        return <>
            <li><HostTemplateLink /></li>
            <li><HostTemplateLink name={name} params={{"form": "update", "templateid": id}}/></li>
        </>;
    }

    renderHostItems() {
        const hostids = [this.props.hostid];
        const dashboards = parseInt(this.props.dashboards, 10) > 0
            ?<li><MVCLink name={<Trans>Dashboards</Trans>} action='host.dashboard.view' params={{'hostid':this.props.hostid}}/></li>
            :<span className='grey'><Trans>Dashboards</Trans></span>;
        const httpTest = parseInt(this.props.httptests, 10) > 0
            ?<li><MVCLink name={<Trans>Web</Trans>} action='web.view' params={{'filter_hostids':hostids, 'filter_show': GRAPH_FILTER_HOST, 'filter_set': '1'}}/></li>
            :<span className='grey'><Trans>Web</Trans></span>;

        const discovery = this.getHostDiscovery(this.props.hostdiscovery);

        let discoveryElem = '';
        if (this.props.flags == ZBX_FLAG_DISCOVERY_CREATED && discovery.ts_delete != 0) {
            discoveryElem = <li><HostLifeTimeIndicator tsDelete={discovery.ts_delete}/></li>;
        }

        return <>
            <li><HostAdminMenu name={this.props.name}/></li>
            <li><HostStatus status={this.props.status} maintenance={this.props.maintenance_status} /></li>
            <li><HostAvailabilityStatus /></li>
            { discoveryElem }
            <li><MVCLink name={<Trans>Latest data</Trans>} action='latest.view' params={{'filter_set':'1', 'hostids': hostids}}  /></li>
            <li><HostProblemStatus /></li>
            <li><MVCLink name={<Trans>Graphs</Trans>} action='charts.view' params={{'filter_hostids':hostids, 'filter_show': GRAPH_FILTER_HOST, 'filter_set': '1'}}/></li>
            { dashboards }
            { httpTest }
        </>;
    }
}

export default HostNav;