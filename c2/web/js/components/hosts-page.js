import Store from '../store.js';
import { connectHost, disconnectHost } from '../sse.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="page">
  <h2>Manage Hosts</h2>
  <div id="host-list"></div>
  <div class="add-host">
    <h3>Add Host</h3>
    <input type="text" id="new-name" placeholder="Name" />
    <input type="text" id="new-url" placeholder="URL (e.g. https://c2.example.com)" />
    <button id="add-btn">Add</button>
  </div>
  <div class="terminal-launch">
    <h3>Terminal</h3>
    <input type="text" id="terminal-cwd" placeholder="Working directory (default: current dir)" />
    <button id="launch-terminal-btn">Launch Terminal</button>
  </div>
</div>
`;

class HostsPage extends HTMLElement {
    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
            this._list = this.shadowRoot.getElementById('host-list');
            this._nameInput = this.shadowRoot.getElementById('new-name');
            this._urlInput = this.shadowRoot.getElementById('new-url');

            this.shadowRoot.getElementById('add-btn').addEventListener('click', () => this._add());
            this.shadowRoot.getElementById('launch-terminal-btn').addEventListener('click', () => this._launchTerminal());
            this.shadowRoot.getElementById('terminal-cwd').addEventListener('keydown', e => {
                if (e.key === 'Enter') this._launchTerminal();
            });
            this._unsub = Store.on('hosts', () => this._render());
        }
        this._render();
    }

    disconnectedCallback() {
        if (this._unsub) this._unsub();
    }

    _render() {
        const hosts = Store.get('hosts');
        this._list.innerHTML = '';
        hosts.forEach(h => {
            const card = document.createElement('div');
            card.className = `host-card ${h.connected ? 'connected' : 'disconnected'}`;
            card.innerHTML = `
                <div class="host-card-header">
                    <span class="status-dot ${h.connected ? 'green' : 'red'}"></span>
                    <strong>${h.name}</strong>
                    <span class="host-url">${h.url || 'localhost'}</span>
                </div>
                <div class="host-card-body">
                    ${h.connected ? `<span>${h.b1s.length} b1 instances</span>` : '<span class="error">Disconnected</span>'}
                </div>
                <div class="host-card-actions">
                    ${h.id !== 'local' ? `<button class="remove-btn" data-id="${h.id}">Remove</button>` : ''}
                    ${h.connected ? `<button class="disconnect-btn" data-id="${h.id}">Disconnect</button>`
                                  : `<button class="connect-btn" data-id="${h.id}">Connect</button>`}
                </div>
            `;

            card.querySelector('.remove-btn')?.addEventListener('click', () => this._remove(h.id));
            card.querySelector('.disconnect-btn')?.addEventListener('click', () => disconnectHost(h.id));
            card.querySelector('.connect-btn')?.addEventListener('click', () => {
                connectHost(h.id, h.url ? `${h.url}/api/events` : '/api/events');
            });

            this._list.appendChild(card);
        });
    }

    _add() {
        const name = this._nameInput.value.trim();
        const url = this._urlInput.value.trim();
        if (!name) return;
        const id = 'host-' + Date.now();
        Store.update('hosts', hosts => [...hosts, { id, name, url, connected: false, b1s: [], agents: [] }]);
        Store.saveHosts();
        this._nameInput.value = '';
        this._urlInput.value = '';
        if (url) {
            connectHost(id, `${url}/api/events`);
        }
    }

    _remove(id) {
        disconnectHost(id);
        Store.update('hosts', hosts => hosts.filter(h => h.id !== id));
        Store.saveHosts();
    }

    _launchTerminal() {
        const cwd = this.shadowRoot.getElementById('terminal-cwd').value.trim() || '.';
        window.dispatchEvent(new CustomEvent('navigate', {
            detail: '/terminal#cwd=' + encodeURIComponent(cwd) + '&contextType=host'
        }));
    }
}

customElements.define('hosts-page', HostsPage);
export default HostsPage;
