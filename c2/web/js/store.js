const Store = {
    _state: {
        hosts: [{ id: 'local', name: 'localhost', url: '', connected: false, b1s: [] }],
        stats: { totalB1s: 0, totalAgents: 0, crashedCount: 0 },
        pendingPrompts: [],
        events: [],
        settings: {
            collapseSystem: true,
            collapseReasoning: true,
            collapseToolResults: true,
            maxMessages: 200,
            autoScroll: true,
        },
    },
    _listeners: {},

    init() {
        try {
            const saved = localStorage.getItem('c2-hosts');
            if (saved) this._state.hosts = JSON.parse(saved);
        } catch (e) {}
        try {
            const saved = localStorage.getItem('c2-settings');
            if (saved) this._state.settings = { ...this._state.settings, ...JSON.parse(saved) };
        } catch (e) {}
    },

    get(key) {
        return this._state[key];
    },

    set(key, value) {
        this._state[key] = value;
        this._notify(key, value);
    },

    update(key, fn) {
        this._state[key] = fn(this._state[key]);
        this._notify(key, this._state[key]);
    },

    on(key, fn) {
        if (!this._listeners[key]) this._listeners[key] = [];
        this._listeners[key].push(fn);
        return () => { this._listeners[key] = this._listeners[key].filter(f => f !== fn); };
    },

    _notify(key, value) {
        (this._listeners[key] || []).forEach(fn => fn(value));
    },

    saveHosts() {
        localStorage.setItem('c2-hosts', JSON.stringify(this._state.hosts));
    },

    saveSettings() {
        localStorage.setItem('c2-settings', JSON.stringify(this._state.settings));
    },
};

Store.init();
export default Store;
