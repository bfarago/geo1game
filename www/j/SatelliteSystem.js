import * as THREE from './three.module.js';

export class SatelliteSystem {
    constructor(scene, camera) {
        this.visible = true;
        this.scene = scene;
        this.camera = camera;
        this.satellites = [];
        this.numSats = 8000;
        this.satGroup = new THREE.Group();
        scene.add(this.satGroup);
        // --- Sprite canvas for satellites (nicer sprite) ---
        const spriteCanvas = document.createElement('canvas');
        spriteCanvas.width = 64;
        spriteCanvas.height = 64;
        const ctx = spriteCanvas.getContext('2d');
        ctx.clearRect(0, 0, 64, 64);

        // Draw a simple satellite: cross with antenna
        ctx.strokeStyle = 'white';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(10, 32);
        ctx.lineTo(54, 32);
        ctx.moveTo(32, 10);
        ctx.lineTo(32, 54);
        ctx.stroke();

        // Draw a small antenna dish
        ctx.beginPath();
        ctx.arc(48, 16, 6, 0, Math.PI * 2);
        ctx.stroke();
        ctx.fillStyle = 'white';
        ctx.fill();

        // Finally create the texture
        this.spriteTexture = new THREE.CanvasTexture(spriteCanvas);
        this.initSatellites();
    }

    createSatelliteModel() {
        const group = new THREE.Group();
        group.scale.set(0.05, 0.05, 0.05);

        // Body: metallic cylinder
        const bodyGeometry = new THREE.CylinderGeometry(0.005, 0.005, 0.015, 8);
        const bodyMaterial = new THREE.MeshStandardMaterial({
            color: 0xcccccc,
            metalness: 0.9,
            roughness: 0.02
        });
        const bodyMesh = new THREE.Mesh(bodyGeometry, bodyMaterial);
        group.add(bodyMesh);

        // Panels: reflective metallic, horizontal "wings" attached to the body
        const panelGeometry = new THREE.PlaneGeometry(0.02, 0.008);
        const panelMaterial = new THREE.MeshStandardMaterial({
            color: 0x6666ff,
            metalness: 1.0,
            roughness: 0.1,
            side: THREE.DoubleSide   // <- Ez a kulcs!
        });

        // Left panel (left of the cylinder)
        const leftPanel = new THREE.Mesh(panelGeometry, panelMaterial);
        leftPanel.position.set(-0.015, 0, 0); // Move to the left of the cylinder
        leftPanel.rotation.x = Math.PI / 2;  // Rotate so it's perpendicular to body
        group.add(leftPanel);

        // Right panel (right of the cylinder)
        const rightPanel = new THREE.Mesh(panelGeometry, panelMaterial);
        rightPanel.position.set(0.015, 0, 0); // Move to the right of the cylinder
        rightPanel.rotation.x = Math.PI / 2;
        group.add(rightPanel);

        return group;
    }

    initSatellites() {
        const geometry = new THREE.SphereGeometry(0.001, 4, 4);
        const material = new THREE.MeshBasicMaterial({ color: 0xffcc00 });

        for (let i = 0; i < this.numSats; i++) {
            const mesh = new THREE.Mesh(geometry, material);
            let orbitRadius , inclination, scale, styp;
            if (i % 3 === 0) {
                // satellites
                const scale = THREE.MathUtils.randFloat(0.5, 0.9);
                mesh.scale.set(scale, scale, scale);
                styp=1;
            }else{
                // junks
                scale=THREE.MathUtils.randFloat(0.001, 0.01);
                mesh.scale.set(scale, scale, scale);
                styp=0;
            }
            if (i % 10 === 0) {
                orbitRadius = THREE.MathUtils.randFloat(1.1, 1.16);
                inclination = THREE.MathUtils.degToRad(THREE.MathUtils.randFloat(89, 91));
            } else {
                orbitRadius = THREE.MathUtils.randFloat(1.2, 1.9);
                inclination = THREE.MathUtils.degToRad(THREE.MathUtils.randFloat(0, 30));
            }
            const raan = Math.random() * Math.PI * 2; // lunch time
            const speed = -0.005 / Math.sqrt(orbitRadius);
            const phase = Math.random() * Math.PI * 2;
            const sat = {
                mesh,
                orbitRadius,
                inclination, raan,
                speed,
                phase,
                theta: phase,
                styp
            };
            // --- Sprite for closeup ---
            const spriteMat = new THREE.SpriteMaterial({ map: this.spriteTexture, color: 0xffffff });
            const sprite = new THREE.Sprite(spriteMat);
            sprite.scale.set(0.02, 0.02, 1);
            sprite.visible = false;
            this.satGroup.add(sprite);
            sat.sprite = sprite;
            if (styp){
                // --- 3D model for close satellites ---
                const model = this.createSatelliteModel();
                model.visible = false;
                this.satGroup.add(model);
                sat.model = model;
            }
            this.satellites.push(sat);
            this.satGroup.add(mesh);
        }
    }

    update(delta) {
        for (let i = 0; i < this.satellites.length; i++) {
            const sat = this.satellites[i];
            sat.theta += sat.speed * delta;
            const r = sat.orbitRadius;
            const x0 = r * Math.cos(sat.theta);
            const y0 = r * Math.sin(sat.theta) * Math.sin(sat.inclination);
            const z0 = r * Math.sin(sat.theta) * Math.cos(sat.inclination);
            const x = x0 * Math.cos(sat.raan) - z0 * Math.sin(sat.raan);
            const z = x0 * Math.sin(sat.raan) + z0 * Math.cos(sat.raan);
            sat.mesh.position.set(x, y0, z);
            sat.sprite.position.set(x, y0, z);
            if (sat.model) sat.model.position.set(x, y0, z);
            const distance = this.camera.position.distanceTo(sat.mesh.position);
            if (distance < 0.5) {
                sat.mesh.visible = false;
                sat.sprite.visible = false;
                if (sat.model ) {
                    sat.model.visible = true;
                    sat.model.position.copy(sat.mesh.position);
                }
            } else {
                sat.mesh.visible = true;
                sat.sprite.visible = false;
                if (sat.model) sat.model.visible = false;
            }
        }
    }

    setVisible(visible) {
        if (visible){
            if (!this.visible)  return;
        }
        this.satGroup.visible = visible;
    }
    getVisible() {
        return this.satGroup.visible;
    }
    toggle(){
        this.visible = !this.visible;
        this.satGroup.visible = this.visible;
    }
}
