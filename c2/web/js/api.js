const BASE = '';

async function _get(path) {
    const res = await fetch(BASE + path);
    if (!res.ok) throw new Error(`GET ${path} ${res.status}`);
    return res.json();
}

async function _post(path, body) {
    const res = await fetch(BASE + path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
    });
    if (!res.ok) throw new Error(`POST ${path} ${res.status}`);
    return res.json();
}

async function _del(path) {
    const res = await fetch(BASE + path, { method: 'DELETE' });
    if (!res.ok) throw new Error(`DELETE ${path} ${res.status}`);
    return res.json();
}

export async function fetchStatus() {
    return _get('/api/status');
}

export async function fetchStats() {
    return _get('/api/stats');
}

export async function fetchPending() {
    return _get('/api/events/pending');
}

export async function fetchB1(pid) {
    return _get(`/api/b1/${pid}`);
}

export async function fetchB1Agents(pid) {
    return _get(`/api/b1/${pid}/agents`);
}

export async function fetchAgent(uuid) {
    return _get(`/api/agent/${uuid}`);
}

export async function fetchMessages(uuid, { limit = 50, before } = {}) {
    let path = `/api/agent/${uuid}/messages?limit=${limit}`;
    if (before) path += `&before=${before}`;
    return _get(path);
}

export async function sendMessage(uuid, msg) {
    return _post(`/api/agent/${uuid}/messages`, msg);
}

export async function dismissPrompt(uuid, toolCallId) {
    return _del(`/api/agent/${uuid}/prompt/${toolCallId}`);
}

export async function ping(body = {}) {
    return _post('/api/ping', body);
}
