import React from "react";
import ReactHintFactory from 'react-hint';
import { Trans } from 'react-i18next';
import { withTranslation } from 'react-i18next';
import { DateTime, Interval } from "luxon";

const ReactHint = ReactHintFactory(React)

class HostLifeTimeIndicator extends React.Component {

    constructor(props) {
        super(props);
    }

    render() {
        const currentTime = DateTime.now();
        const tsDelete = DateTime.fromSeconds(this.props.tsDelete);
        const interval = Interval.fromDateTimes(tsDelete, currentTime);

        let warningText = "";

        if (currentTime>tsDelete) {
            warningText = this.props.t("The host is not discovered anymore and will be deleted the next time discovery rule is processed.");
        }else{
            warningText = this.props.t(
                'The host is not discovered anymore and will be deleted in {{seconds}} (on {{date}} at {{time}}).',
                {
                    "seconds": interval.length('seconds'),
                    "date": tsDelete.toLocaleString(DateTime.DATE_SHORT),
                    "time": tsDelete.toLocaleString(DateTime.TIME_24_SIMPLE)
                });
        }

        console.log(warningText);


        return <>
            <ReactHint persist="1"
                       events
                       delay={{show: 100}}
                       ref={(ref) => this.instance = ref} />
            <a className='icon-info status-yellow'
               data-rh-at="bottom"
               data-rh={warningText}
            ></a>
        </>;
    }
}

export default withTranslation()(HostLifeTimeIndicator);