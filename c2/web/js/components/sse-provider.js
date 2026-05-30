import Store from '../store.js';

const template = document.createElement('template');
template.innerHTML = `<div style="display:none"></div>`;

class SseProvider extends HTMLElement {
    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));
        }

        // Forward SSE events to Store for component consumption
        Store.on('b1_connected', d => this._dispatch('b1_connected', d));
        Store.on('b1_disconnected', d => this._dispatch('b1_disconnected', d));
        Store.on('agent_update', d => this._dispatch('agent_update', d));
        Store.on('user_prompt', d => this._dispatch('user_prompt', d));
        Store.on('prompt_resolved', d => this._dispatch('prompt_resolved', d));
    }

    _dispatch(type, data) {
        this.dispatchEvent(new CustomEvent(type, { bubbles: true, composed: true, detail: data }));
        // Also push to event log if it exists
        const log = document.querySelector('event-log');
        if (log && log.push) log.push(type, data);
    }
}

customElements.define('sse-provider', SseProvider);
export default SseProvider;
