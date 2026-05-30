import Store from '../store.js';

const template = document.createElement('template');
template.innerHTML = `
<header>
  <nav>
    <a href="/" data-nav>Dashboard</a>
    <a href="/hosts" data-nav>Hosts</a>
    <a href="/projects" data-nav>Projects</a>
    <a href="/settings" data-nav>Settings</a>
  </nav>
  <span id="prompt-badge" class="badge hidden"></span>
</header>
`;

class AppHeader extends HTMLElement {
    static get observedAttributes() { return ['prompts']; }

    connectedCallback() {
        this.attachShadow({ mode: 'open' });
        this.shadowRoot.appendChild(template.content.cloneNode(true));
        this.shadowRoot.querySelectorAll('[data-nav]').forEach(a => {
            a.addEventListener('click', e => {
                e.preventDefault();
                window.dispatchEvent(new CustomEvent('navigate', { detail: a.getAttribute('href') }));
            });
        });
    }

    attributeChangedCallback(name, oldVal, newVal) {
        if (name === 'prompts') {
            const badge = this.shadowRoot.getElementById('prompt-badge');
            const count = parseInt(newVal) || 0;
            if (count > 0) {
                badge.textContent = `!${count}`;
                badge.classList.remove('hidden');
            } else {
                badge.classList.add('hidden');
            }
        }
    }
}

customElements.define('app-header', AppHeader);
export default AppHeader;
