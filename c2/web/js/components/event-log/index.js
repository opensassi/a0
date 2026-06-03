const template = document.createElement('template');
template.innerHTML = `
<div class="event-log">
  <h3>Event Log</h3>
  <div id="log-container" class="log-container"></div>
</div>
`;

class EventLog extends HTMLElement {
    constructor() {
        super();
        this._maxEvents = 100;
        this._events = [];
    }

    connectedCallback() {
        this.attachShadow({ mode: 'open' });
        this.shadowRoot.appendChild(template.content.cloneNode(this));
        this._container = this.shadowRoot.getElementById('log-container');
    }

    push(type, data) {
        const entry = { type, data, time: new Date().toLocaleTimeString(), seq: this._events.length };
        this._events.push(entry);
        if (this._events.length > this._maxEvents) this._events.shift();

        const el = document.createElement('div');
        el.className = `log-entry log-${type}`;
        el.id = `log-entry-${entry.seq}`;
        el.innerHTML = `<span class="log-time">${entry.time}</span> <span class="log-type">${type}</span> <span class="log-summary">${this._summary(entry)}</span>`;
        this._container.appendChild(el);
        this._container.scrollTop = this._container.scrollHeight;

        while (this._container.children.length > this._maxEvents) {
            this._container.removeChild(this._container.firstChild);
        }
    }

    _summary(entry) {
        const d = entry.data;
        switch (entry.type) {
            case 'b1_connected': return `b1 ${d.pid} connected (${d.workdir})`;
            case 'b1_disconnected': return `b1 ${d.pid} disconnected`;
            case 'agent_update': return `b1 ${d.pid} updated: ${(d.agents || []).length} agents`;
            case 'user_prompt': return `Prompt from ${d.session}: ${(d.prompt || '').substring(0, 60)}`;
            case 'prompt_resolved': return `Prompt resolved for ${d.session}`;
            default: return JSON.stringify(d).substring(0, 80);
        }
    }
}

customElements.define('event-log', EventLog);
export default EventLog;
