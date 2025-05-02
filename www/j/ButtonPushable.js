export class ButtonPushable {
    constructor(div_id, initstate, labels) {
        this.pushed = initstate;
        this.div_id = div_id;
        this.btn= null;
        this.iconSpan=null;
        this.labels=labels;
    }
    initializeUI(){
        this.btn=document.getElementById(this.div_id);
        if (this.btn == null){
            console.log("ButtonPushable: div_id not found: ", this.div_id);
            return;
        }
        this.iconSpan=this.btn.querySelector('span.icon');
        if (this.iconSpan == null){
            console.log("ButtonPushable: iconSpan not found: ", this.div_id);
        }
        this.updateUI();
        this.btn.offsetHeight;
    }
    isPushed(){ return this.pushed; }
    setPushed(pushState) { this.pushed=pushState; }
    updateUI(){
        if (this.btn == null) return;
        if (this.pushed) {
            this.iconSpan.innerHTML=this.labels[1];
            this.btn.classList.remove('unpushed');
            this.btn.classList.add('pushed');
        } else {
            this.iconSpan.innerHTML=this.labels[0];
            this.btn.classList.remove('pushed');
            this.btn.classList.add('unpushed');
        }
        if (this.iconSpan != null) {
            this.iconSpan.style.opacity = this.pushed ? 1 : 0.5;
        }
        this.btn.offsetHeight;
    }
}