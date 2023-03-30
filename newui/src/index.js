import React from "react";
import ReactDOM from "react-dom";
import HostStatus from "./HostStatus";

document.addEventListener('DOMContentLoaded', function () {
    const hostStatuses = document.getElementsByTagName('glb-hoststatus');

    for (let i = 0; i < hostStatuses.length; i++) {
        try {
            ReactDOM.render(<HostStatus {...(hostStatuses[i].dataset)} />, hostStatuses[i]);
        } catch (error) {
            console.log(error);
        }
    }
});

