import Store from '../store.js';

const template = document.createElement('template');
template.innerHTML = `
<div class="page">
  <h2>Settings</h2>
  <div class="settings-group">
    <h3>Conversation Display</h3>
    <label><input type="checkbox" id="collapse-system"> Collapse system prompts</label>
    <label><input type="checkbox" id="collapse-reasoning"> Collapse reasoning</label>
    <label><input type="checkbox" id="collapse-tool"> Collapse tool results</label>
    <label>
      Max messages in view:
      <input type="number" id="max-messages" min="50" max="1000" step="50" />
    </label>
    <label><input type="checkbox" id="auto-scroll"> Auto-scroll on new messages</label>
  </div>
</div>
`;

class SettingsPage extends HTMLElement {
    connectedCallback() {
        if (!this.shadowRoot) {
            this.attachShadow({ mode: 'open' });
            this.shadowRoot.appendChild(template.content.cloneNode(true));

            const s = Store.get('settings');
            this._bind('collapse-system', 'collapseSystem', s, 'change');
            this._bind('collapse-reasoning', 'collapseReasoning', s, 'change');
            this._bind('collapse-tool', 'collapseToolResults', s, 'change');
            this._bind('auto-scroll', 'autoScroll', s, 'change');
            this._bindNum('max-messages', 'maxMessages', s, 'change');
        }
    }

    _bind(id, key, defaults, event) {
        const el = this.shadowRoot.getElementById(id);
        if (el.type === 'checkbox') {
            el.checked = defaults[key];
            el.addEventListener(event, () => {
                Store.update('settings', s => ({ ...s, [key]: el.checked }));
                Store.saveSettings();
            });
        }
    }

    _bindNum(id, key, defaults, event) {
        const el = this.shadowRoot.getElementById(id);
        el.value = defaults[key];
        el.addEventListener(event, () => {
            Store.update('settings', s => ({ ...s, [key]: parseInt(el.value) || 200 }));
            Store.saveSettings();
        });
    }
}

customElements.define('settings-page', SettingsPage);
export default SettingsPage;
