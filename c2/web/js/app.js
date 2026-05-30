import Store from './store.js';
import { connectHost } from './sse.js';
import { fetchStatus, fetchPending, fetchStats } from './api.js';

// Import all components to register them
import './components/app-shell.js';
import './components/app-header.js';
import './components/prompt-badge.js';
import './components/dashboard-page.js';
import './components/stats-cards.js';
import './components/host-list.js';
import './components/event-log.js';
import './components/hosts-page.js';
import './components/projects-page.js';
import './components/agent-page.js';
import './components/conversation-view.js';
import './components/message-bubble.js';
import './components/prompt-banner.js';
import './components/settings-page.js';
import './components/sse-provider.js';

let appShell = null;
let currentPage = null;
let currentCleanup = null;

const routes = [
    { pattern: '/', Component: 'dashboard-page' },
    { pattern: '/hosts', Component: 'hosts-page' },
    { pattern: '/projects', Component: 'projects-page' },
    { pattern: '/settings', Component: 'settings-page' },
    { pattern: '/agent/:uuid', Component: 'agent-page', paramKey: 'uuid' },
];

function matchRoute(path) {
    for (const route of routes) {
        if (route.pattern === path) return { route, params: {} };
        if (route.paramKey) {
            const prefix = route.pattern.replace(`:${route.paramKey}`, '');
            if (path.startsWith(prefix)) {
                const uuid = path.substring(prefix.length);
                if (uuid) return { route, params: { [route.paramKey]: uuid } };
            }
        }
    }
    return { route: routes[0], params: {} }; // default to dashboard
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

    initRouter();
}

// Also support inline dashboard when JS modules fail
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', bootstrap);
} else {
    bootstrap();
}
