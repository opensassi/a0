import Store from './store.js';

let _eventSources = {};

export function connectHost(hostId, url) {
    if (_eventSources[hostId]) _eventSources[hostId].close();

    const es = new EventSource(url || '/api/events');

    es.onopen = () => {
        Store.update('hosts', hosts => hosts.map(h =>
            h.id === hostId ? { ...h, connected: true } : h
        ));
    };

    es.onerror = () => {
        Store.update('hosts', hosts => hosts.map(h =>
            h.id === hostId ? { ...h, connected: false } : h
        ));
    };

    es.addEventListener('b1_connected', e => {
        const data = JSON.parse(e.data);
        Store.update('hosts', hosts => hosts.map(h => {
            if (h.id !== hostId) return h;
            const b1s = [...h.b1s];
            if (!b1s.find(b => b.pid === data.pid)) {
                b1s.push({ pid: data.pid, workdir: data.workdir, hostname: data.hostname, agents: [] });
            }
            return { ...h, b1s };
        }));
    });

    es.addEventListener('b1_disconnected', e => {
        const data = JSON.parse(e.data);
        Store.update('hosts', hosts => hosts.map(h => {
            if (h.id !== hostId) return h;
            return { ...h, b1s: h.b1s.filter(b => b.pid !== data.pid) };
        }));
    });

    es.addEventListener('agent_update', e => {
        const data = JSON.parse(e.data);
        Store.update('hosts', hosts => hosts.map(h => {
            if (h.id !== hostId) return h;
            return {
                ...h,
                b1s: h.b1s.map(b =>
                    b.pid === data.pid ? { ...b, agents: data.agents || [] } : b
                )
            };
        }));
    });

    es.addEventListener('user_prompt', e => {
        const data = JSON.parse(e.data);
        Store.update('pendingPrompts', prompts => [...prompts, data]);
        Store._notify('newPrompt', data);
    });

    es.addEventListener('prompt_resolved', e => {
        const data = JSON.parse(e.data);
        Store.update('pendingPrompts', prompts =>
            prompts.filter(p => p.toolCallId !== data.toolCallId)
        );
    });

    es.addEventListener('prompt_dismissed', e => {
        const data = JSON.parse(e.data);
        Store.update('pendingPrompts', prompts =>
            prompts.filter(p => p.toolCallId !== data.toolCallId)
        );
    });

    _eventSources[hostId] = es;
    return es;
}

export function disconnectHost(hostId) {
    if (_eventSources[hostId]) {
        _eventSources[hostId].close();
        delete _eventSources[hostId];
    }
}

export function disconnectAll() {
    Object.keys(_eventSources).forEach(disconnectHost);
}
