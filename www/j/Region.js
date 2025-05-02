import * as THREE from './three.module.js';
export class RegionInfoPopup {
    constructor(scene, viewController) {
        this.scene = scene;
        this.viewController = viewController;
        this.div = this.createDiv();
        this.hoverLine = null;
    }

    createDiv() {
        const div = document.createElement('div');
        div.id = 'regionInfoPopup';
        div.className = 'hidden';
        document.body.appendChild(div);
        return div;
    }

    show(region, screenX, screenY) {
        if (!region) {
            this.hide();
            return;
        }
        this.div.style.left = `${screenX + 10}px`;
        this.div.style.top = `${screenY + 10}px`;
        this.div.classList.remove('hidden');
        this.div.classList.add('visible');
        this.div.innerHTML = `
            <b>${region.data.name}</b><br>
            Population: ${region.data.p ?? 'Unknown'}<br>
            Owner: ${region.owner_nick ?? 'Unknown'}
        `;
    }

    hide() {
        this.div.classList.add('hidden');
        this.div.classList.remove('visible');
        this.removeHoverLine();
    }

    updateLine(screenX, screenY, lat, lon, alt) {
        this.removeHoverLine();
        const currentView = this.viewController.getCurrentView();
        const wpBox = currentView?.screenToWorldPosition?.(screenX, screenY);
        const wpRgn = currentView?.latLonAltToWorldPosition?.(lat, lon, alt ?? 1.0);

        if (wpBox && wpRgn) {
            const geometry = new THREE.BufferGeometry().setFromPoints([wpBox, wpRgn]);
            const material = new THREE.LineBasicMaterial({ color: 0xffff00 });
            this.hoverLine = new THREE.Line(geometry, material);
            this.scene.add(this.hoverLine);
        }
    }

    removeHoverLine() {
        if (this.hoverLine) {
            this.scene.remove(this.hoverLine);
            this.hoverLine.geometry.dispose();
            this.hoverLine.material.dispose();
            this.hoverLine = null;
        }
    }
}