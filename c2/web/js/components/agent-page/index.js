import Store from '../../store.js';
import { fetchAgent } from '../../api.js';
import './../conversation-view/index.js';
import './../prompt-banner/index.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="page" id="page-agent">
  <a href="/" data-nav class="back-link">&larr; Back to Dashboard</a>
  <div id="agent-info" class="agent-info">
    <h2 id="agent-title">Agent</h2>
    <div id="agent-details" class="agent-details"></div>
  </div>
  <conversation-view id="conversation"></conversation-view>
  <prompt-banner></prompt-banner>
</div>
`;

class AgentPage extends HTMLElement {
    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
            this._conv = this.shadowRoot.getElementById('conversation');
            this._title = this.shadowRoot.getElementById('agent-title');
            this._details = this.shadowRoot.getElementById('agent-details');

            this.shadowRoot.querySelector('[data-nav]').addEventListener('click', e => {
                e.preventDefault();
                window.dispatchEvent(new CustomEvent('navigate', { detail: '/' }));
            });
        }
        this._load();
    }

    async _load() {
        const uuid = this.getAttribute('uuid');
        if (!uuid) return;

        this._conv.setAttribute('session', uuid);
        this._title.textContent = `Agent: ${uuid.substring(0, 20)}...`;

        try {
            const info = await fetchAgent(uuid);
            this._details.innerHTML = `
                <span>PID: ${info.pid}</span>
                <span>State: <span class="agent-state ${info.state}">${info.state}</span></span>
                <span>Host: ${info.hostname}</span>
                <span>Project: ${info.b1Workdir}</span>
            `;
        } catch (e) {
            this._details.textContent = 'Agent information unavailable';
        }
    }
}

customElements.define('agent-page', AgentPage);
export default AgentPage;
