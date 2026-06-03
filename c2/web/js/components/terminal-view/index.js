const terminalTemplate = document.createElement('template');
terminalTemplate.innerHTML = `
  <style>
    @import url('/lib/xterm/xterm.css');
    :host {
      display: flex;
      flex-direction: column;
      height: 100%;
      background: #1e1e1e;
    }
    .toolbar {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 4px 14px;
      padding-left: 20px;
      background: #2d2d2d;
      color: #ccc;
      font-size: 12px;
      font-family: monospace;
      min-height: 24px;
      flex-shrink: 0;
    }
    .toolbar .title {
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      flex-shrink: 1;
    }
    .toolbar .status {
      margin-left: auto;
      flex-shrink: 0;
      color: #888;
    }
    .toolbar .status.active { color: #4caf50; }
    .toolbar .status.ended { color: #ff9800; }
    .terminal-container {
      flex: 1;
      min-height: 0;
    }
    .terminal-container .xterm { height: 100%; }
    /* xterm.js creates a hidden textarea for input — hide it inside shadow DOM */
    .xterm-helper-textarea {
      position: absolute !important;
      left: -9999em !important;
      top: 0 !important;
      width: 0 !important;
      height: 0 !important;
      opacity: 0 !important;
      overflow: hidden !important;
      z-index: -10 !important;
    }
    .xterm-screen { z-index: 1; }
  </style>
  <div class="toolbar">
    <span class="title" id="terminal-title">Terminal</span>
    <span class="status" id="terminal-status">connecting...</span>
  </div>
  <div class="terminal-container" id="terminal"></div>
`;

class TerminalView extends HTMLElement {
    constructor() {
        super();
        this.attachShadow({ mode: 'open' });
        this.shadowRoot.appendChild(terminalTemplate.content.cloneNode(true));
        this.streamId = null;
        this.terminalId = null;
        this.inputBuffer = '';
        this.inputTimer = null;
        this._onStreamChunk = null;
        this._onStreamEnd = null;
        this._cwd = '';
        this._contextType = '';
        this._contextId = '';
    }

    connectedCallback() {
        // Read launch params from URL hash (e.g. /terminal#cwd=/path&contextType=host&contextId=abc)
        const cwd = this.getAttribute('cwd') || '.';
        const contextType = this.getAttribute('contextType') || 'host';
        const contextId = this.getAttribute('contextId') || '';
        // Also check hash params (set by navigators)
        if (window.location.hash) {
            try {
                const hash = window.location.hash.replace(/^#/, '');
                for (const part of hash.split('&')) {
                    const [k, v] = part.split('=');
                    if (k === 'cwd' && v) {
                        let decoded = decodeURIComponent(v);
                        // Normalize: strip ./ prefix for absolute-looking paths
                        if (decoded.startsWith('./') && decoded.length > 2) {
                            decoded = decoded.substring(2);
                        }
                        this._cwd = decoded;
                    }
                    if (k === 'contextType' && v) { this._contextType = decodeURIComponent(v); }
                    if (k === 'contextId' && v) { this._contextId = decodeURIComponent(v); }
                }
            } catch(e) {}
        }
        const finalCwd = this._cwd || cwd;
        const finalContextType = this._contextType || contextType;
        const finalContextId = this._contextId || contextId;

        const container = this.shadowRoot.getElementById('terminal');
        const statusEl = this.shadowRoot.getElementById('terminal-status');
        const titleEl = this.shadowRoot.getElementById('terminal-title');
        titleEl.textContent = finalContextId ? 'Terminal – ' + finalContextId : 'Terminal – ' + finalCwd;

        // Initialize xterm.js
        this.term = new Terminal({
            cursorBlink: true,
            fontSize: 14,
            fontFamily: 'Menlo, Monaco, "Courier New", monospace',
            theme: { background: '#1e1e1e', foreground: '#d4d4d4' }
        });
        this.fitAddon = new FitAddon.FitAddon();
        this.term.loadAddon(this.fitAddon);
        this.term.open(container);
        this.fitAddon.fit();

        // Resize observer
        this._resizeObserver = new ResizeObserver(() => {
            try { this.fitAddon.fit(); } catch(e) {}
        });
        this._resizeObserver.observe(this.shadowRoot.querySelector('.terminal-container'));

        // xterm input handler: accumulate with 50ms debounce
        this.term.onData(data => {
            this.inputBuffer += data;
            clearTimeout(this.inputTimer);
            this.inputTimer = setTimeout(() => this.flushInput(), 50);
        });

        // Open terminal via API and poll DB for readiness
        statusEl.textContent = 'launching...';
        statusEl.className = 'status';
        const body = { cwd: finalCwd, contextType: finalContextType };
        if (finalContextId) body.contextId = finalContextId;
        fetch('/api/terminal/open', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        })
        .then(r => r.json())
        .then(data => {
            this.terminalId = data.terminalId;
            statusEl.textContent = 'waiting...';
            // Poll the DB until the stream is created (a0 writes terminalId into stream record)
            const poll = () => {
                if (this.streamId) return;
                fetch('/api/terminal/status/' + this.terminalId)
                    .then(r => r.json())
                    .then(d => {
                        if (d.status === 'ready' && d.streamId) {
                            this.streamId = d.streamId;
                            statusEl.textContent = 'active';
                            statusEl.className = 'status active';
                            this._startChunkPolling();
                        } else {
                            setTimeout(poll, 500);
                        }
                    })
                    .catch(() => setTimeout(poll, 500));
            };
            setTimeout(poll, 500);
        })
        .catch(err => {
            statusEl.textContent = 'error: ' + err.message;
            this.term.write('\r\n\x1b[31mFailed to open terminal: ' + err.message + '\x1b[0m\r\n');
        });

        // Listen for stream events on the global SSE provider
        this._onStreamChunk = (e) => {
            if (this.streamId && e.detail.streamId === this.streamId) {
                if (e.detail.direction === 'stdout') {
                    this.term.write(e.detail.data);
                }
                if (e.detail.seq !== undefined) {
                    this._lastChunkSeq = Math.max(this._lastChunkSeq, e.detail.seq);
                }
            }
        };

        this._onStreamEnd = (e) => {
            if (this.streamId && e.detail.streamId === this.streamId) {
                statusEl.textContent = 'exited: ' + e.detail.exitCode;
                statusEl.className = 'status ended';
                this.term.write('\r\n\x1b[33mProcess exited with code ' + e.detail.exitCode + '\x1b[0m\r\n');
                this._stopChunkPolling();
            }
        };

        window.addEventListener('stream_chunk', this._onStreamChunk);
        window.addEventListener('stream_end', this._onStreamEnd);
    }

    _startChunkPolling() {
        this._lastChunkSeq = -1;
        this._pollChunksInterval = setInterval(() => {
            if (!this.streamId) return this._stopChunkPolling();
            fetch('/api/stream/' + this.streamId + '/chunks')
                .then(r => r.json())
                .then(chunks => {
                    for (const c of (chunks || [])) {
                        if (c.seq > this._lastChunkSeq && c.direction === 'stdout') {
                            this.term.write(c.data);
                            this._lastChunkSeq = c.seq;
                        }
                    }
                })
                .catch(() => {});
        }, 500);
    }

    _stopChunkPolling() {
        if (this._pollChunksInterval) {
            clearInterval(this._pollChunksInterval);
            this._pollChunksInterval = null;
        }
    }

    disconnectedCallback() {
        this._stopChunkPolling();
        if (this._resizeObserver) this._resizeObserver.disconnect();
        if (this.term) this.term.dispose();
        if (this._onStreamChunk) window.removeEventListener('stream_chunk', this._onStreamChunk);
        if (this._onStreamEnd) window.removeEventListener('stream_end', this._onStreamEnd);
    }

    flushInput() {
        if (!this.inputBuffer || !this.streamId) return;
        const data = this.inputBuffer;
        this.inputBuffer = '';
        fetch('/api/stream/' + this.streamId + '/input', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ data })
        }).catch(() => {});
    }
}

customElements.define('terminal-view', TerminalView);
