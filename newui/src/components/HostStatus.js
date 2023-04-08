import React from "react";
import { useTranslation } from 'react-i18next';
import { HOST_STATUS_MONITORED, HOST_MAINTENANCE_STATUS_ON, HOST_STATUS_NOT_MONITORED } from '../common/constants'

function HostStatus({status, maintenance}){
    const { t } = useTranslation();
    let text = 'Unknown';
    let cName = 'gray';

    switch (parseInt(status, 10)) {
        case HOST_STATUS_MONITORED:
            if (parseInt(maintenance, 10) == HOST_MAINTENANCE_STATUS_ON) {
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
    return <span className={cName}>{t(text)}</span>;
}

export default HostStatus;