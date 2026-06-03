import Store from '../../store.js';
import { fetchAgent, fetchMessages, sendMessage } from '../../api.js';
import './../conversation-view/index.js';
import './../message-bubble/index.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="page" id="page-interact">
  <a href="/" data-nav class="back-link">&larr; Back to Dashboard</a>
  <div id="agent-header" class="agent-info">
    <h2 id="agent-title">Agent Monitor</h2>
    <div id="agent-details" class="agent-details"></div>
  </div>
  <div id="interact-layout" class="interact-layout">
    <div id="interact-main" class="interact-main">
      <conversation-view id="conversation"></conversation-view>
    </div>
  </div>
  <div id="interact-input" class="interact-input">
    <textarea id="msg-input" placeholder="Type a message to send to the agent..." rows="3"></textarea>
    <div class="interact-buttons">
      <button id="btn-send-queue" class="btn btn-primary">Send (next turn)</button>
      <button id="btn-send-abort" class="btn btn-danger">Send &amp; Interrupt</button>
    </div>
  </div>
</div>
`;

class InteractPage extends HTMLElement {
    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
            this._conv = this.shadowRoot.getElementById('conversation');
            this._title = this.shadowRoot.getElementById('agent-title');
            this._details = this.shadowRoot.getElementById('agent-details');
            this._input = this.shadowRoot.getElementById('msg-input');
            this._sendQueue = this.shadowRoot.getElementById('btn-send-queue');
            this._sendAbort = this.shadowRoot.getElementById('btn-send-abort');

            this.shadowRoot.querySelector('[data-nav]').addEventListener('click', e => {
                e.preventDefault();
                window.dispatchEvent(new CustomEvent('navigate', { detail: '/' }));
            });

            this._sendQueue.addEventListener('click', () => this._send('queue'));
            this._sendAbort.addEventListener('click', () => this._send('abort'));
            this._input.addEventListener('keydown', e => {
                if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
                    e.preventDefault();
                    this._send('queue');
                }
            });

            this._unsubStream = Store.on('stream_chunk', data => this._onStreamChunk(data));
            this._unsubMsg = Store.on('new_message', data => this._onNewMessage(data));
        }
        this._load();
    }

    disconnectedCallback() {
        if (this._unsubStream) this._unsubStream();
        if (this._unsubMsg) this._unsubMsg();
    }

    async _load() {
        const uuid = this.getAttribute('uuid');
        if (!uuid) return;

        this._uuid = uuid;
        this._conv.setAttribute('session', uuid);
        this._title.textContent = `Agent Monitor: ${uuid.substring(0, 20)}...`;

        try {
            const info = await fetchAgent(uuid);
            this._details.innerHTML = `
                <span>PID: ${info.pid}</span>
                <span>State: <span class="agent-state ${info.state}">${info.state}</span></span>
                <span>Host: ${info.hostname}</span>
                <span>Project: ${info.b1Workdir}</span>
                <span>Session: <code>${uuid}</code></span>
            `;
        } catch (e) {
            this._details.innerHTML = `<span>Session: <code>${uuid}</code></span>`;
        }
    }

    async _send(mode) {
        const text = this._input.value.trim();
        if (!text || !this._uuid) return;

        this._input.value = '';
        this._input.disabled = true;

        try {
            const resp = await sendMessage(this._uuid, {
                role: 'user',
                content: text,
                mode: mode
            });
            if (resp.status === 'ok') {
                this._conv.appendMessage({
                    role: 'user',
                    content: text,
                    created_at: Math.floor(Date.now() / 1000)
                });
            }
        } catch (e) {
            console.error('Failed to send message:', e);
        }

        this._input.disabled = false;
        this._input.focus();
    }

    _onStreamChunk(data) {
        const conv = this._conv;
        if (conv && conv.appendStreamChunk) {
            conv.appendStreamChunk(data);
        }
    }

    _onNewMessage(data) {
        const conv = this._conv;
        if (conv && data.session === this._uuid && conv.appendMessage) {
            conv.appendMessage(data);
        }
    }
}

customElements.define('interact-page', InteractPage);
export default InteractPage;
