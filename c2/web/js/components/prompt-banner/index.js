import Store from '../../store.js';
import { sendMessage, dismissPrompt } from '../../api.js';

const template = document.createElement('template');
template.innerHTML = `
<div id="banner" class="prompt-banner hidden" part="banner">
  <div class="prompt-info">
    <strong>Prompt from <span id="prompt-session"></span>:</strong>
    <p id="prompt-text"></p>
  </div>
  <div class="prompt-input-row">
    <input type="text" id="prompt-input" placeholder="Type response..." />
    <button id="prompt-send">Send</button>
    <button id="prompt-dismiss">Dismiss</button>
  </div>
</div>
`;

class PromptBanner extends HTMLElement {
    constructor() {
        super();
        this._currentPrompt = null;
    }

    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
            this._banner = this.shadowRoot.getElementById('banner');
            this._sessionEl = this.shadowRoot.getElementById('prompt-session');
            this._textEl = this.shadowRoot.getElementById('prompt-text');
            this._input = this.shadowRoot.getElementById('prompt-input');
            this._sendBtn = this.shadowRoot.getElementById('prompt-send');
            this._dismissBtn = this.shadowRoot.getElementById('prompt-dismiss');

            this._sendBtn.addEventListener('click', () => this._send());
            this._input.addEventListener('keydown', e => { if (e.key === 'Enter') this._send(); });
            this._dismissBtn.addEventListener('click', () => this._dismiss());

            this._unsub = Store.on('newPrompt', data => this._show(data));
        }
    }

    disconnectedCallback() {
        if (this._unsub) this._unsub();
    }

    _show(data) {
        this._currentPrompt = data;
        this._sessionEl.textContent = data.session;
        this._textEl.textContent = data.prompt;
        this._input.value = '';
        this._banner.classList.remove('hidden');
        this._input.focus();
    }

    async _send() {
        if (!this._currentPrompt) return;
        const response = this._input.value;
        try {
            await sendMessage(this._currentPrompt.session, {
                role: 'tool',
                tool_call_id: this._currentPrompt.toolCallId,
                content: response,
            });
        } catch (e) {
            console.error('send failed', e);
        }
        this._hide();
    }

    async _dismiss() {
        if (!this._currentPrompt) return;
        try {
            await dismissPrompt(this._currentPrompt.session, this._currentPrompt.toolCallId);
        } catch (e) {
            console.error('dismiss failed', e);
        }
        this._hide();
    }

    _hide() {
        this._banner.classList.add('hidden');
        this._currentPrompt = null;
    }
}

customElements.define('prompt-banner', PromptBanner);
export default PromptBanner;
