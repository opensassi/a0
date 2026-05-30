import Store from '../store.js';
import { fetchStats } from '../api.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="stats-grid">
  <div class="stat-card">
    <span class="stat-label">B1 Supervisors</span>
    <span class="stat-value" id="stat-b1s">0</span>
  </div>
  <div class="stat-card">
    <span class="stat-label">A0 Agents</span>
    <span class="stat-value" id="stat-agents">0</span>
  </div>
  <div class="stat-card">
    <span class="stat-label">Crashed</span>
    <span class="stat-value crashed" id="stat-crashed">0</span>
  </div>
  <div class="stat-card">
    <span class="stat-label">Pending Prompts</span>
    <span class="stat-value prompt" id="stat-prompts">0</span>
  </div>
</div>
`;

class StatsCards extends HTMLElement {
    connectedCallback() {
        this.attachShadow({ mode: 'open' });
        this.shadowRoot.appendChild(template.content.cloneNode(true));

        this._unsub1 = Store.on('stats', s => this._update(s));
        this._unsub2 = Store.on('pendingPrompts', p => {
            this.shadowRoot.getElementById('stat-prompts').textContent = p.length;
        });
        this._refresh();
    }

    disconnectedCallback() {
        if (this._unsub1) this._unsub1();
        if (this._unsub2) this._unsub2();
    }

    async _refresh() {
        try {
            const stats = await fetchStats();
            Store.set('stats', stats);
        } catch (e) {}
    }

    _update(stats) {
        this.shadowRoot.getElementById('stat-b1s').textContent = stats.totalB1s || 0;
        this.shadowRoot.getElementById('stat-agents').textContent = stats.totalAgents || 0;
        this.shadowRoot.getElementById('stat-crashed').textContent = stats.crashedCount || 0;
    }
}

customElements.define('stats-cards', StatsCards);
export default StatsCards;
