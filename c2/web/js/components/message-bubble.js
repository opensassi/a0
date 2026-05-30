import Store from '../store.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="msg" part="message">
  <div class="msg-header">
    <span class="msg-role" part="role"></span>
    <span class="msg-time" part="time"></span>
    <button class="collapse-btn" part="collapse" style="display:none">&#9660;</button>
  </div>
  <div class="msg-content" part="content"></div>
</div>
`;

class MessageBubble extends HTMLElement {
    static get observedAttributes() { return ['role', 'content', 'time', 'collapsed', 'tool-name']; }

    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
            this._contentEl = this.shadowRoot.querySelector('.msg-content');
            this._roleEl = this.shadowRoot.querySelector('.msg-role');
            this._timeEl = this.shadowRoot.querySelector('.msg-time');
            this._collapseBtn = this.shadowRoot.querySelector('.collapse-btn');

            this._collapseBtn.addEventListener('click', () => {
                const isCollapsed = this._contentEl.style.display !== 'none';
                this._contentEl.style.display = isCollapsed ? 'none' : 'block';
                this._collapseBtn.innerHTML = isCollapsed ? '&#9654;' : '&#9660;';
            });
        }
        this._render();
    }

    attributeChangedCallback() { this._render(); }

    _render() {
        if (!this.shadowRoot) return;
        const role = this.getAttribute('role') || 'unknown';
        const content = this.getAttribute('content') || '';
        const time = this.getAttribute('time') || '';
        const toolName = this.getAttribute('tool-name') || '';

        this._roleEl.textContent = role === 'tool' ? `tool: ${toolName}` : role;
        this._timeEl.textContent = time;

        if (role === 'system' || role === 'reasoning' || role === 'tool') {
            const s = Store.get('settings');
            const shouldCollapse =
                (role === 'system' && s.collapseSystem) ||
                (role === 'reasoning' && s.collapseReasoning) ||
                (role === 'tool' && s.collapseToolResults);
            this._contentEl.style.display = shouldCollapse ? 'none' : 'block';
            this._collapseBtn.style.display = 'block';
            this._collapseBtn.innerHTML = shouldCollapse ? '&#9654;' : '&#9660;';
        } else {
            this._contentEl.style.display = 'block';
            this._collapseBtn.style.display = 'none';
        }

        this._contentEl.textContent = content;
        this.className = `msg msg-${role}`;
    }
}

customElements.define('message-bubble', MessageBubble);
export default MessageBubble;
