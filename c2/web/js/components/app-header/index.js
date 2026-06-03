import Store from '../../store.js';

const template = document.createElement('template');
template.innerHTML = `
<header>
  <nav>
    <a href="/" id="nav-dashboard">Dashboard</a>
    <a href="/hosts" id="nav-hosts">Hosts</a>
    <a href="/projects" id="nav-projects">Projects</a>
    <a href="/settings" id="nav-settings">Settings</a>
  </nav>
  <span id="prompt-badge" class="badge hidden"></span>
</header>
`;

class AppHeader extends HTMLElement {
    static get observedAttributes() { return ['prompts']; }

    connectedCallback() {
        this.attachShadow({ mode: 'open' });
        this.shadowRoot.appendChild(template.content.cloneNode(true));
        this.shadowRoot.querySelectorAll('nav a').forEach(a => {
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
