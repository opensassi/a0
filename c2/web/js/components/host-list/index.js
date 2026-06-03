import Store from '../../store.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="host-list">
  <h3>Hosts</h3>
  <div id="host-rows"></div>
</div>
`;

class HostList extends HTMLElement {
    connectedCallback() {
        this.attachShadow({ mode: 'open' });
        this.shadowRoot.appendChild(template.content.cloneNode(true));
        this._unsub = Store.on('hosts', hosts => this._render(hosts));
        this._render(Store.get('hosts'));
    }

    disconnectedCallback() {
        if (this._unsub) this._unsub();
    }

    _render(hosts) {
        const container = this.shadowRoot.getElementById('host-rows');
        container.innerHTML = '';
        hosts.forEach(h => {
            const div = document.createElement('div');
            div.className = `host-row ${h.connected ? 'connected' : 'disconnected'}`;
            div.id = `host-row-${sanitizeId(h.name)}`;
            div.innerHTML = `
                <span class="host-indicator ${h.connected ? 'green' : 'red'}"></span>
                <span class="host-name">${h.name}</span>
                <span class="host-count">${h.b1s.length} b1 &middot; ${h.b1s.reduce((total, b) => total + (b.agents || []).length, 0)} a0</span>
            `;
            container.appendChild(div);
        });
    }
}

customElements.define('host-list', HostList);
export default HostList;
