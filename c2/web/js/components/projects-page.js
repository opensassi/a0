import Store from '../store.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="page">
  <h2>Projects</h2>
  <div id="project-list"></div>
</div>
`;

class ProjectsPage extends HTMLElement {
    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
            this._list = this.shadowRoot.getElementById('project-list');
            this._unsub = Store.on('hosts', () => this._render());
        }
        this._render();
    }

    disconnectedCallback() {
        if (this._unsub) this._unsub();
    }

    _render() {
        const hosts = Store.get('hosts');
        this._list.innerHTML = '';
        hosts.forEach(h => {
            (h.b1s || []).forEach(b1 => {
                const card = document.createElement('div');
                card.className = 'project-card';
                card.innerHTML = `
                    <div class="project-header">
                        <strong>${b1.workdir}</strong>
                        <span class="host-tag">${h.name}</span>
                    </div>
                    <div class="project-body">
                        <span>PID: ${b1.pid}</span>
                        <span>Agents: ${(b1.agents || []).length}</span>
                    </div>
                    <div class="project-agents">
                        ${(b1.agents || []).map(a => `
                            <div class="agent-row ${a.state}">
                                <span>a0 (${a.pid})</span>
                                <span class="agent-session">${(a.session || '').substring(0, 20)}</span>
                                <span class="agent-state ${a.state}">${a.state}</span>
                                <a href="/agent/${a.session}" data-nav class="agent-link">View</a>
                            </div>
                        `).join('')}
                    </div>
                `;
                card.querySelectorAll('[data-nav]').forEach(a => {
                    a.addEventListener('click', e => {
                        e.preventDefault();
                        window.dispatchEvent(new CustomEvent('navigate', { detail: a.getAttribute('href') }));
                    });
                });
                this._list.appendChild(card);
            });
        });
    }
}

customElements.define('projects-page', ProjectsPage);
export default ProjectsPage;
