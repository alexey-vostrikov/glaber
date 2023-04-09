import {LitElement, html, css, PropertyValues} from 'lit';
import {customElement, property, state} from 'lit/decorators.js';
import { translate } from "lit-translate";


const HOST_STATUS_MONITORED: Number = 0;
const HOST_STATUS_NOT_MONITORED: Number = 1;
const HOST_MAINTENANCE_STATUS_ON: Number = 1;

@customElement("glb-host-status")
export class HostStatus extends LitElement {
    @property({ type: Number }) status: Number = 0;
    @property({ type: Number }) maintenance: Number = 0;

    static styles = css`
      .enabled { color: var( --enabled-color, #429e47) }
      .disabled { color: var( --disabled-color, #e33734) }
      .maintenance { color: var( --maintenance-color, #f24f1d) }
      .unknown { color: var( --unknown-color, #768d99) }
    `;

    render() {
        let statusKey = 'unknown';

        switch (this.status) {
            case HOST_STATUS_MONITORED:
                if (this.maintenance == HOST_MAINTENANCE_STATUS_ON) {
                    statusKey = 'maintenance';
                } else {
                    statusKey = 'enabled';
                }
                break;
            case HOST_STATUS_NOT_MONITORED:
                statusKey = 'disabled';
                break;
        }

        return html`<span class="${statusKey}">${translate('hoststatus.' + statusKey)}</span>`;
    }
}