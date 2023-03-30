import React from "react";
import ReactDOM from "react-dom/client";
import HostStatus from "./HostStatus";

document.addEventListener('DOMContentLoaded', function () {
    const hostStatuses = document.getElementsByTagName('glb-hoststatus');

    for (let i = 0; i < hostStatuses.length; i++) {
        try {
            let root = ReactDOM.createRoot(hostStatuses[i]);
            root.render(<HostStatus {...(hostStatuses[i].dataset)} />);
        } catch (error) {
            console.log(error);
        }
    }
});

