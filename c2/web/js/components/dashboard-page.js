import Store from '../store.js';
import { fetchStatus, fetchPending } from '../api.js';
import { connectHost } from '../sse.js';
import './stats-cards.js';
import './host-list.js';
import './event-log.js';
import './prompt-banner.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="page dashboard">
  <h2>Dashboard</h2>
  <stats-cards></stats-cards>
  <div class="dashboard-grid">
    <div class="dashboard-main">
      <host-list></host-list>
    </div>
    <div class="dashboard-sidebar">
      <event-log id="event-log"></event-log>
    </div>
  </div>
  <prompt-banner id="prompt-banner"></prompt-banner>
</div>
`;

class DashboardPage extends HTMLElement {
    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
            this._log = this.shadowRoot.getElementById('event-log');

            // Listen for SSE events to push to log
            this._unsubs = [
                Store.on('b1_connected', d => this._log?.push('b1_connected', d)),
                Store.on('b1_disconnected', d => this._log?.push('b1_disconnected', d)),
                Store.on('agent_update', d => this._log?.push('agent_update', d)),
                Store.on('user_prompt', d => this._log?.push('user_prompt', d)),
            ];
        }
        this._init();
    }

    disconnectedCallback() {
        if (this._unsubs) this._unsubs.forEach(u => u());
    }

    async _init() {
        // Fetch initial state
        try {
            const status = await fetchStatus();
            const pending = await fetchPending();

            // Aggregate into host
            const host = Store.get('hosts')[0];
            host.b1s = status.map(b1 => ({
                pid: b1.pid,
                workdir: b1.workdir,
                hostname: b1.hostname,
                agents: (b1.agents || []).map(a => a.pid),
            }));
            host.agents = status.reduce((a, b1) => a + (b1.agents || []).length, 0);
            Store.set('hosts', [host]);
            Store.set('pendingPrompts', pending || []);
        } catch (e) {}

        // Connect SSE
        connectHost('local', '/api/events');
    }
}

customElements.define('dashboard-page', DashboardPage);
export default DashboardPage;
