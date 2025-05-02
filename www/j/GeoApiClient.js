// API client for the geo service

export function apiURL(program, params) {
    return window.CONFIG['api'][program] + new URLSearchParams(params).toString();
}
export class GeoApiClient {
    constructor(ws) {
        this.ws = ws;
        this.pending = new Map(); // id -> {resolve, reject, timestamp, timeoutId}
        this.counter = 1;

        if (this.ws) {
            this.ws.onmessage = (event) => this._handleMessage(event.data);
        }
    }
    setCommnicationController(cc) {
        this.ws = cc; // TODO: check if Communication controller is a valid ws
        if (cc){
            cc.onCommunicationStateChanged(this._handleCommunicationStateChanged.bind(this));
        }
    }
    _handleCommunicationStateChanged(state) {
        if (!this.ws || this.ws.socket.readyState !== WebSocket.OPEN) {
            console.warn("[GeoApiClient] WebSocket not available, will fallback to HTTP");
        }
    }
    _handleMessage(data) {
        try {
            const msg = JSON.parse(data);
            if (msg.id && this.pending.has(msg.id)) {
                const { resolve, reject, timeoutId } = this.pending.get(msg.id);
                clearTimeout(timeoutId);
                this.pending.delete(msg.id);
                if ('error' in msg) {
                    reject(new Error(msg.error));
                } else {
                    resolve(msg.data);
                }
            }
        } catch (e) {
            console.warn("Invalid WS message", data);
        }
    }

    async request(program, params = {}, options = {}) {
        const useWS = this.ws && this.ws.readyState === WebSocket.OPEN;
        const id = this.counter++;
        const url = apiURL(program, params);

        if (useWS) {
            const payload = { id, type: "request", program, params };
            return new Promise((resolve, reject) => {
                const timeoutId = setTimeout(() => {
                    if (this.pending.has(id)) {
                        this.pending.delete(id);
                        reject(new Error("Timeout"));
                    }
                }, 3000);
                this.pending.set(id, { resolve, reject, timestamp: Date.now(), timeoutId });
                this.ws.send(JSON.stringify(payload));
            });
        } else {
            // fallback to HTTP POST with JSON if WebSocket is not available
            const finalOptions = {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(params),
                ...options
            };
            const res = await fetch(apiURL(program, {}), finalOptions);
            if (!res.ok) throw new Error(`HTTP error ${res.status}`);
            return await res.json();
        }
    }
}

/*
 ez volt kb (és mind a test.php-ben, mind a ws_server.php -ben benne van.)
 const payload = {lat, lon, alt};
 cc.send({type: 'update_user_pos', ...payload });
 vagy
 gca.request('test',{task: 'update_user_pos'},{ method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(payload)
*/