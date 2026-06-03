import Store from '../../store.js';
import { fetchMessages } from '../../api.js';
import './../message-bubble/index.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="conv" part="container">
  <div class="conv-controls">
    <label>Collapse: <input type="checkbox" id="collapse-system" checked> system</label>
    <label><input type="checkbox" id="collapse-reasoning" checked> reasoning</label>
    <label><input type="checkbox" id="collapse-tool" checked> tool</label>
    <span id="conv-status"></span>
  </div>
  <div class="conv-messages" id="msg-container" part="messages"></div>
</div>
`;

class ConversationView extends HTMLElement {
    static get observedAttributes() { return ['session']; }

    constructor() {
        super();
        this._messages = [];
        this._maxMessages = 200;
        this._loading = false;
        this._hasMore = true;
    }

    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
            this._container = this.shadowRoot.getElementById('msg-container');
            this._status = this.shadowRoot.getElementById('conv-status');

            this.shadowRoot.getElementById('collapse-system').addEventListener('change', e => {
                Store.update('settings', s => ({ ...s, collapseSystem: e.target.checked }));
                Store.saveSettings();
            });
            this.shadowRoot.getElementById('collapse-reasoning').addEventListener('change', e => {
                Store.update('settings', s => ({ ...s, collapseReasoning: e.target.checked }));
                Store.saveSettings();
            });
            this.shadowRoot.getElementById('collapse-tool').addEventListener('change', e => {
                Store.update('settings', s => ({ ...s, collapseToolResults: e.target.checked }));
                Store.saveSettings();
            });

            this._container.addEventListener('scroll', () => {
                if (this._container.scrollTop < 100 && !this._loading && this._hasMore) {
                    this._loadMore();
                }
            });
        }
        this._load();
    }

    attributeChangedCallback(name, oldVal, newVal) {
        if (name === 'session' && oldVal !== newVal) {
            this._messages = [];
            this._hasMore = true;
            this._load();
        }
    }

    async _load() {
        const session = this.getAttribute('session');
        if (!session) return;
        this._loading = true;
        this._status.textContent = 'Loading...';
        try {
            const msgs = await fetchMessages(session, { limit: 50 });
            this._messages = msgs || [];
            this._hasMore = (msgs || []).length >= 50;
            this._render();
            this._scrollBottom();
        } catch (e) {
            this._status.textContent = 'Error loading messages';
        }
        this._loading = false;
    }

    async _loadMore() {
        if (this._loading || !this._hasMore || this._messages.length === 0) return;
        this._loading = true;
        try {
            const before = this._messages[0].id;
            const msgs = await fetchMessages(this.getAttribute('session'), { limit: 50, before });
            if (msgs && msgs.length > 0) {
                this._messages = [...msgs, ...this._messages];
                this._hasMore = msgs.length >= 50;
                const prevScroll = this._container.scrollHeight;
                this._render();
                // Preserve scroll position
                this._container.scrollTop = this._container.scrollHeight - prevScroll;
            } else {
                this._hasMore = false;
            }
        } catch (e) {}
        this._loading = false;
    }

    appendMessage(msg) {
        this._messages.push(msg);
        if (this._messages.length > this._maxMessages) {
            this._messages.splice(0, 50);
        }
        const el = this._buildBubble(msg);
        this._container.appendChild(el);
        if (this._autoScroll) this._scrollBottom();
    }

    _render() {
        this._container.innerHTML = '';
        if (this._hasMore && this._messages.length > 0) {
            const loadMore = document.createElement('div');
            loadMore.className = 'load-more';
            loadMore.id = 'load-more-btn';
            loadMore.textContent = '\u2191 Load older messages';
            this._container.appendChild(loadMore);
        }
        this._messages.forEach(m => this._container.appendChild(this._buildBubble(m)));
        this._status.textContent = `${this._messages.length} messages`;
    }

    _buildBubble(msg) {
        const el = document.createElement('message-bubble');
        el.setAttribute('role', msg.role || 'unknown');
        el.setAttribute('content', msg.content || '');
        el.setAttribute('time', msg.created_at ? new Date(msg.created_at * 1000).toLocaleTimeString() : '');
        if (msg.role === 'tool') {
            el.setAttribute('tool-name', msg.name || '');
        }
        return el;
    }

    _scrollBottom() {
        requestAnimationFrame(() => {
            this._container.scrollTop = this._container.scrollHeight;
        });
    }

    get _autoScroll() {
        const s = Store.get('settings');
        return s.autoScroll;
    }
}

customElements.define('conversation-view', ConversationView);
export default ConversationView;
