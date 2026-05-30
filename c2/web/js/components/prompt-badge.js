import Store from '../store.js';

const template = document.createElement('template');
template.innerHTML = `
<button id="badge" class="prompt-badge hidden" part="badge">
  <span id="count">0</span> pending
</button>
`;

class PromptBadge extends HTMLElement {
    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
            this._badge = this.shadowRoot.getElementById('badge');
            this._count = this.shadowRoot.getElementById('count');

            this._badge.addEventListener('click', () => {
                window.dispatchEvent(new CustomEvent('navigate', { detail: '/' }));
            });

            this._unsub = Store.on('pendingPrompts', p => {
                const n = p.length;
                this._count.textContent = n;
                this._badge.classList.toggle('hidden', n === 0);
            });
        }
        this._badge.classList.toggle('hidden', Store.get('pendingPrompts').length === 0);
    }

    disconnectedCallback() {
        if (this._unsub) this._unsub();
    }
}

customElements.define('prompt-badge', PromptBadge);
export default PromptBadge;
