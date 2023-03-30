const HOST_STATUS_MONITORED = 0;
const HOST_STATUS_NOT_MONITORED = 1;
const HOST_MAINTENANCE_STATUS_OFF = 0;
const HOST_MAINTENANCE_STATUS_ON = 1;

function HostStatus(props){
    let text = 'Unknown';
    let cName = 'gray';

    switch (parseInt(props.status, 10)) {
        case HOST_STATUS_MONITORED:
            if (parseInt(props.maintenance, 10) == HOST_MAINTENANCE_STATUS_ON) {
                text = 'In maintenance';
                cName = 'orange';
            } else {
                text = 'Enabled';
                cName = 'green';
            }
            break;
        case HOST_STATUS_NOT_MONITORED:
            text = 'Disabled';
            cName = 'red';
            break;
    }
    return <span className={cName}>{text}</span>;
}

export default HostStatus;