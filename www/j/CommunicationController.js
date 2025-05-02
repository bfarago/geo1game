const INITIAL_RECONNECT_DELAY_MS = 2000;
const MAX_RECONNECT_DELAY_MS = 30000;
const MAX_RECONNECT_ATTEMPTS = 5;
const RECONNECT_DELAY_FACTOR = 1.5;
const ERROR_PAUSE_DURATION_MS = 30000;

export class CommunicationController {
    constructor(url) {
        this.url = url;
        this.socket = null;
        this.reconnectDelay = INITIAL_RECONNECT_DELAY_MS;
        this.manualClose = false;

        this.messageHandlers = [];
        this.closeHandlers = [];
        this.communicationStateChangeHandlers = [];

        this.connectionState = {
            status: 'idle',
            reconnectAttempts: 0,
            ping: null,
            lastError: null,
            lastConnectTime: null,
            lastDisconnectTime: null,
        };

        this.transitionTimeout = null;

        this.transitionTo('connecting');
        this.boundDisconnect = this.disconnect.bind(this);
        window.addEventListener('beforeunload', this.boundDisconnect);
    }

    transitionTo(state) {
        if (this.transitionTimeout) {
            clearTimeout(this.transitionTimeout);
            this.transitionTimeout = null;
        }

        this.connectionState.status = state;
        this.notifyStateChange();

        switch (state) {
            case 'idle':
                // Do nothing
                break;
            case 'alive':
                this.connectionState.reconnectAttempts = 0;
                this.reconnectDelay = INITIAL_RECONNECT_DELAY_MS;
                break;

            case 'connecting':
                this.connect();
                break;

            case 'open':
                this.connectionState.reconnectAttempts = 0;
                this.reconnectDelay = INITIAL_RECONNECT_DELAY_MS;
                break;

            case 'reconnecting':
                if (this.connectionState.reconnectAttempts <= MAX_RECONNECT_ATTEMPTS) {
                    this.transitionTimeout = setTimeout(() => {
                        this.transitionTo('connecting');
                    }, this.reconnectDelay);
                    this.reconnectDelay = Math.min(this.reconnectDelay * RECONNECT_DELAY_FACTOR, MAX_RECONNECT_DELAY_MS);
                } else {
                    this.transitionTo('error_pause');
                }
                break;

            case 'error_pause':
                this.transitionTimeout = setTimeout(() => {
                    this.connectionState.reconnectAttempts = 0;
                    this.reconnectDelay = INITIAL_RECONNECT_DELAY_MS;
                    this.transitionTo('connecting');
                }, ERROR_PAUSE_DURATION_MS);
                break;

            case 'closed':
                // Do nothing
                break;
        }
    }
    async disconnect() {
        if (this.isOpen()) {
            this.send({ type: 'disconnect' });
            await new Promise(r => setTimeout(r, 100));
        }
    }
    connect() {
        console.log("[Comm] Connecting to", this.url);
        this.socket = new WebSocket(this.url);

        this.socket.onopen = () => {
            console.log("[Comm] Connection established");
            this.connectionState.lastConnectTime = Date.now();
            this.transitionTo('open');
            this.pingTimeout = setTimeout(() => {
                if (this.connectionState.status === 'open') {
                    console.warn("[Comm] No ping received, transitioning to idle");
                    this.transitionTo('idle');
                }
            }, 15000); // wait 15 seconds
            this.sendHello();
        };

        this.socket.onmessage = (event) => {
            const message = JSON.parse(event.data);
            if (message.type === 'ping') {
                if (this.pingTimeout) {
                    clearTimeout(this.pingTimeout);
                    this.pingTimeout = null;
                }
                this.sendPong();
                return;
            }
            this.messageHandlers.forEach(cb => cb(message));
        };

        this.socket.onerror = (event) => {
            console.error("[Comm] Socket error", event);
            this.connectionState.lastError = event;
            // Error handling deferred to onclose
        };

        this.socket.onclose = (event) => {
            console.warn("[Comm] Socket closed", event);
            this.connectionState.lastDisconnectTime = Date.now();

            if (this.pingTimeout) {
                clearTimeout(this.pingTimeout);
                this.pingTimeout = null;
            }

            if (!this.manualClose) {
                this.connectionState.reconnectAttempts += 1;
                this.transitionTo('reconnecting');
            } else {
                this.transitionTo('closed');
            }

            this.closeHandlers.forEach(cb => cb(event));
        };
    }

    isOpen() {
        return this.socket && this.socket.readyState === WebSocket.OPEN;
    }

    send(data) {
        if (this.isOpen()) {
            this.socket.send(JSON.stringify(data));
        } else {
            console.warn("[Comm] Cannot send, socket not open");
        }
    }
    sendHello() {
        if (this.isOpen()) {
            this.send({ type: 'hello' });
        }
    }
    sendPong() {
        if (this.isOpen()) {
            const pingStart = Date.now();
            this.send({ type: 'pong' });
            const pingEnd = Date.now();
            this.connectionState.ping = pingEnd - pingStart;
            if (this.connectionState.status !== 'alive') {
                this.transitionTo('alive');
            } else {
                this.notifyStateChange();
            }
        }
    }
    sendRefresh() {
        if (this.isOpen()) {
            this.send({ type: 'refresh' });
        }
    }
    close() {
        this.manualClose = true;
        if (this.socket) {
            this.socket.close();
        }
    }

    onMessage(cb) {
        this.messageHandlers.push(cb);
    }

    onClose(cb) {
        this.closeHandlers.push(cb);
    }

    onCommunicationStateChanged(cb) {
        this.communicationStateChangeHandlers.push(cb);
    }

    notifyStateChange() {
        this.communicationStateChangeHandlers.forEach(cb => cb(this.connectionState));
    }
}