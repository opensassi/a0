import Store from '../store.js';

const template = document.createElement('template');
template.innerHTML = `
<app-header></app-header>
<main id="main-content"></main>
`;

class AppShell extends HTMLElement {
    connectedCallback() {
        this.attachShadow({ mode: 'open' });
        this.shadowRoot.appendChild(template.content.cloneNode(true));

        this._unsub = Store.on('pendingPrompts', () => {
            this.shadowRoot.querySelector('app-header')?.setAttribute(
                'prompts', Store.get('pendingPrompts').length
            );
        });
    }

    disconnectedCallback() {
        if (this._unsub) this._unsub();
    }

    get mainContent() {
        return this.shadowRoot.getElementById('main-content');
    }
}

customElements.define('app-shell', AppShell);
export default AppShell;
