import Store from './store.js';
import { connectHost } from './sse.js';
import { fetchStatus, fetchPending, fetchStats } from './api.js';

// Import all components to register them
import './components/app-shell/index.js';
import './components/app-header/index.js';
import './components/prompt-badge/index.js';
import './components/dashboard-page/index.js';
import './components/stats-cards/index.js';
import './components/host-list/index.js';
import './components/event-log/index.js';
import './components/hosts-page/index.js';
import './components/projects-page/index.js';
import './components/agent-page/index.js';
import './components/conversation-view/index.js';
import './components/message-bubble/index.js';
import './components/prompt-banner/index.js';
import './components/settings-page/index.js';
import './components/sse-provider/index.js';
import './components/terminal-view/index.js';
import './components/interact-page/index.js';

window.sanitizeId = function(raw) {
    return String(raw).toLowerCase()
        .replace(/[^a-z0-9]+/g, '-')
        .replace(/^-|-$/g, '')
        .substring(0, 24);
};

window.selectById = function(id) {
    function find(root) {
        if (root.getElementById) {
            const el = root.getElementById(id);
            if (el) return el;
        }
        const nodes = root.querySelectorAll ? root.querySelectorAll('*') : [];
        for (const node of nodes) {
            if (node.shadowRoot) {
                const found = find(node.shadowRoot);
                if (found) return found;
            }
        }
        return null;
    }
    return find(document);
};

let appShell = null;
let currentPage = null;
let currentCleanup = null;

const routes = [
    { pattern: '/', Component: 'dashboard-page' },
    { pattern: '/hosts', Component: 'hosts-page' },
    { pattern: '/projects', Component: 'projects-page' },
    { pattern: '/settings', Component: 'settings-page' },
    { pattern: '/terminal', Component: 'terminal-view' },
    { pattern: '/agent/:uuid/interact', Component: 'interact-page', paramKey: 'uuid' },
    { pattern: '/agent/:uuid', Component: 'agent-page', paramKey: 'uuid' },
];

function matchRoute(path) {
    const clean = path.split('#')[0];
    const segs = clean.split('/').filter(Boolean);

    for (const route of routes) {
        const patSegs = route.pattern.split('/').filter(Boolean);

        if (!route.paramKey) {
            if (route.pattern === clean) return { route, params: {} };
            continue;
        }

        if (segs.length !== patSegs.length) continue;

        const params = {};
        let match = true;
        for (let i = 0; i < patSegs.length; i++) {
            if (patSegs[i] === `:${route.paramKey}`) {
                params[route.paramKey] = segs[i];
            } else if (patSegs[i] !== segs[i]) {
                match = false;
                break;
            }
        }

        if (match) return { route, params };
    }

    return { route: routes[0], params: {} };
}

function renderPage(path) {
    const { route, params } = matchRoute(path);
    const main = appShell?.mainContent;
    if (!main) return;

    if (currentCleanup) currentCleanup();
    main.innerHTML = '';

    const el = document.createElement(route.Component);
    if (params) {
        Object.entries(params).forEach(([k, v]) => el.setAttribute(k, v));
    }
    main.appendChild(el);
    currentPage = el;
    currentCleanup = () => main.innerHTML = '';
}

function initRouter() {
    renderPage(window.location.pathname);

    window.addEventListener('navigate', e => {
        const path = e.detail;
        window.history.pushState({}, '', path);
        renderPage(path);
    });

    window.addEventListener('popstate', () => {
        renderPage(window.location.pathname);
    });

    document.addEventListener('click', e => {
        const a = e.target.closest('[data-nav]');
        if (a) {
            e.preventDefault();
            window.dispatchEvent(new CustomEvent('navigate', { detail: a.getAttribute('href') }));
        }
    });
}

// Initialize everything
function bootstrap() {
    appShell = document.createElement('app-shell');
    document.body.appendChild(appShell);
    document.body.appendChild(document.createElement('sse-provider'));

    // Establish SSE before rendering so terminal_ready events aren't missed
    connectHost('local', '/api/events');

    initRouter();
}

// Also support inline dashboard when JS modules fail
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', bootstrap);
} else {
    bootstrap();
}
