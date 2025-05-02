import * as THREE from './three.module.js';

export class ChatClient {
    constructor(communicationController, camera, user_id) {
        this.cc = communicationController;
        this._isChatting = false;
        this._user_id = user_id;
        this.camera = camera;
        this.chatPanel = null;
        this.chatInputContainer = null;
        this.chatInput = null;
        this.chatButton = null;
        this._clickLocked = false;
        this.chatLog = null;
        this.onStopChat = null;
    }
    init() {
        this.startChat = this.startChat.bind(this);
        this.stopChat = this.stopChat.bind(this);
        this.handleEvent = this.handleEvent.bind(this);
        this.onBnClick = this.onBnClick.bind(this);
        //this.onBnDown = this.onBnDown.bind(this);
        this.chatPanel = document.getElementById('chatPanel');
        this.chatInputContainer = document.getElementById('chatInputContainer');
        //this.chatInputSubContainer = document.getElementById('chatInputSubContainer');
        this.chatInput = document.getElementById('chatInput');
        this.chatButton = document.getElementById('chatButton');
        this.chatLog = document.getElementById('chatLog');
    }
    initializeUI(){
        //this.chatButton.addEventListener('click', this.onBnClick);
        //this.chatButton.addEventListener('mousedown', (e) => this.onBnDown(e));
        //this.chatButton.addEventListener('touchstart', (e) => this.onBnDown(e));
        this.chatButton.addEventListener('mousedown', this.onBnDown.bind(this));
        this.chatButton.addEventListener('touchstart', this.onBnDown.bind(this));
        this.chatInput.addEventListener('blur', (e) => {
            requestAnimationFrame(() => {
                if (!this._clickLocked) {
                    this.stopChat();
                }
            });
        });
        this.chatInput.addEventListener('keydown', this.handleEvent);
    }
    onBnDown(event){
        event.preventDefault(); // hogy ne legyen ghost-click
        if (this._clickLocked) return;
        this._clickLocked = true;
        if (this._isChatting){
            this.stopChat();
        } else {
            this.startChat();
        }
        setTimeout(() => {
            this._clickLocked = false;
        }, 200); // kicsi késleltetés, hogy ne triggereljen kétszer
    }
    onBnClick(event){
        if (this._clickLocked) return;
        this._clickLocked = true;
        if (this._isChatting){
            this.stopChat();
        } else {
            this.startChat();
        }
        requestAnimationFrame(() => {
            this._clickLocked = false;
        });
    }
    handleEvent(e){
        if (e.key === 'Enter') {
            const text = e.target.value.trim();
            if (text && this.cc && this.cc.isOpen()) {
                const direction = new THREE.Vector3();
                this.camera.getWorldDirection(direction);
                const message = {
                    type: "chat_message",
                    message: text,
                    camera_pos: { x: this.camera.position.x, y: this.camera.position.y, z: this.camera.position.z },
                    camera_dir: { x: direction.x, y: direction.y, z: direction.z }
                };
                this.cc.send(message);
                console.log("Sending chat message:", message);
            }
            e.target.value = '';
            this.stopChat();
            e.stopPropagation();
        } else if (e.key === 'Escape') {
            this.stopChat();
            e.stopPropagation();
        }
    }
    isChatting() { return this._isChatting; }
    setUserId(user_id) { this._user_id = user_id; }
    show(visible){
        if (visible) {
            window.scrollTo(0, 0);
            this.chatPanel.style.display = 'block';
            this.chatInputContainer.style.display = 'block';
            this.chatLog.style.opacity = 1;
            this.chatPanel.style.opacity = 1;
            this.chatInput.focus();
        } else {
            this.chatInput.blur();
            this.chatPanel.style.display = 'none';
            //this.chatInputContainer.style.display = 'none';
            //this.chatLog.style.opacity = 0.3
            setTimeout(() => {
                window.scrollTo(0, 0);
            }, 200);
            /*const focusTrapInput = document.getElementById('focusTrapInput');
            if (focusTrapInput) {
                focusTrapInput.focus();
                setTimeout(() => {
                    focusTrapInput.blur(); // <--- AZONNAL "elveszítjük" a fókuszt, így eltűnik a keyboard
                }, 50); // pici késleltetés kellhet (különösen iOS-en)
            }
            */
            /*
            
            const focusTrap = document.getElementById('focusTrap');
            if (focusTrap) {
                focusTrap.focus();
            }
            */
        }
    }
    onNewMsgArived(html, data) {
        if (!this._isChatting){
            this.chatPanel.style.display = 'flex';
            this.chatInputContainer.style.display = 'none';
            this.chatPanel.style.opacity = 0.9;
            this.chatLog.scrollTop = chatLog.scrollHeight;
            setTimeout(() => {
                if (!this._isChatting){       
                    this.chatPanel.style.opacity = 0.5;
                    setTimeout(() => {
                        if (!this._isChatting){
                            this.chatPanel.style.opacity = 1;
                            this.chatPanel.style.display = 'none';
                        }
                    }, 4000);
                }
            }, 1500);
        }
    }
    startChat() {
        this._isChatting = true;
        this.show(true);
    }
    stopChat() {
        this._isChatting = false;
        this.show(false);
        this.notifyOnStopChat();
    }
    setOnStopChat(onStopChat) {
        this.onStopChat = onStopChat;
    }
    notifyOnStopChat() {
        if (this.onStopChat)
        {
            this.onStopChat();
        }
    }
    sendMessage(message) {
        if (this._isChatting) {
            cc.send(message);
        }
    }

    handleServerMessage(data) {
        if (data.message && data.user_id !== undefined) {
            const entry = document.createElement('div');
            if (data.user_id == user_id) {
                entry.className = 'sent-message';
            } else {
                entry.className = 'received-message';
            }
            let nick = data.nick;
            if (nick === undefined) {
                nick = "user"+data.user_id;
            }
            entry.textContent = `${data.nick}: ${data.message}`;
            this.onNewMsgArived(entry, data);
            this.chatLog.appendChild(entry);
            this.chatLog.scrollTop = chatLog.scrollHeight;
            
        }
    }
}