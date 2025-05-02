import * as THREE from './three.module.js';
import { ViewMode } from './ViewMode.js';

export class TerrainView extends ViewMode {
    constructor(scene, camera, light, globalambient, controls, renderer) {
        super("terrain");
        this.scene = scene;
        this.camera = camera;
        this.light = light;
        this.globalambient = globalambient;
        this.controls = controls;
        this.renderer = renderer;
        this.terrain = null;
        this.crosshair = null;
        this.cloudLayer = null;
        this.cloudLayerVisible = true;
        this.userMarkerGroup = null;
    }

    init() {
        if (!this.terrain){
            const widthSeg = 360*2;
            const heightSeg = 180*2;
            const geometry = new THREE.PlaneGeometry(2, 1, widthSeg - 1, heightSeg - 1);
            const material = new THREE.MeshStandardMaterial({
                map: this.biomTexture,
                displacementMap: this.elevTexture,
                displacementScale: 0.05,
                displacementBias: -0.03 
            });

            this.terrain = new THREE.Mesh(geometry, material);
            this.terrain.position.y = -0.5;
            this.terrain.rotation.x = -Math.PI / 2;
            this.scene.add(this.terrain);
            const crosshairSize = 0.01;
            const crosshairGeo = new THREE.BufferGeometry().setFromPoints([
                new THREE.Vector3(-crosshairSize, 0.001, 0),
                new THREE.Vector3(crosshairSize, 0.001, 0),
                new THREE.Vector3(0, 0.001, -crosshairSize),
                new THREE.Vector3(0, 0.001, crosshairSize),
                new THREE.Vector3(0, -0.1, 0), // alsó pont (pl. tengerszint alatt)
                new THREE.Vector3(0, 0.1, 0)   // felső pont (pl. hegycsúcs felett)
            ]);
            const crosshairMat = new THREE.LineBasicMaterial({ color: 0xff0000 });
            this.crosshair = new THREE.LineSegments(crosshairGeo, crosshairMat);
            this.scene.add(this.crosshair);
            //Cloud layer
            const cloudGeometry = new THREE.PlaneGeometry(2, 1, 1, 1); // Ugyanolyan méret, mint a domborzat
            const cloudMaterial = new THREE.MeshStandardMaterial({
                map: this.cloudTexture,
                transparent: true,
                opacity: 0.7,  // finomabb hatás
                depthWrite: false,
            });

            this.cloudLayer = new THREE.Mesh(cloudGeometry, cloudMaterial);
            this.cloudLayer.rotation.x = -Math.PI / 2;
            this.cloudLayer.position.y = -0.45; // domborzat felett legyen
            this.cloudLayer.renderOrder = 1; // felülre kényszerítve
            this.scene.add(this.cloudLayer);
        }
        this.scene.background = new THREE.Color(0x87ceeb); // világoskék ég
        this.scene.fog = new THREE.Fog(0x87ceeb, 15, 50);   // horizontba olvadás

        this.terrain.receiveShadow = true;
        this.terrain.castShadow = true;
        this.light.castShadow = true;
        this.light.shadow.mapSize.width = 4096;
        this.light.shadow.mapSize.height = 4096;
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
        this.controls.enableRotate = true;
        this.controls.enableZoom = true;
        this.controls.minDistance = 0.01; // greater than radius
        this.controls.maxDistance = 15;   // max distance to camera
        this.controls.enableDamping = true;
        this.camera.fov=20;
        this.camera.position.set(0, 0.2, 0.12);
        this.camera.lookAt(0, 0.2, 0.1);
        this.camera.updateProjectionMatrix();
    }
    setOption(optionId, value, previous){
        switch(optionId) {
            case "clouds":
                this.cloudLayerVisible = value;
                this.cloudLayer.visible = value;
                break;
        }
    }
    /*
    toggleClouds() {
        setOption('clouds', !this.cloudLayerVisible, this.cloudLayerVisible);
    }*/
    updateCrosshair(lat, lon, alt) {
        if (!this.crosshair) return;
        if (lon > 180) lon -= 360;
        if (lon < -180) lon += 360;
        //lon+= 180;
        const x = (lon + 180) / 360 - 0.5;
        const z = (90 - lat) / 180 - 0.5;

        const planeWidth = 2.0;
        const planeHeight = 1.0;
        const terrainX = x * planeWidth;
        const terrainZ = z * planeHeight;

        this.crosshair.position.set(terrainX, -0.48, terrainZ);
    }
    dbclick(raycaster) {
        const intersects = raycaster.intersectObject(this.terrain); // plain a THREE.Mesh-ed
        if (intersects.length > 0) {
            const point = intersects[0].point;
            const lon = point.x / 2.0 * 360;
            const lat = 90 - (point.z / 1.0 + 0.5) * 180;
            console.log("Clicked at:", lat.toFixed(4), lon.toFixed(4));
            if (this.crosshairCallback) {
                this.crosshairCallback(lat, -lon+180, 1.05);
            }
        }
    }
    show() {
        this.terrain.visible = true;
        if (this.cloudLayer) this.cloudLayer.visible = true;
        if (this.crosshair) this.crosshair.visible = true;
        if (this.userMarkerGroup) this.userMarkerGroup.visible = true;
        //isLandscapeMode = true;
    }
    hide() {
        this.terrain.visible = false;
        if (this.cloudLayer) this.cloudLayer.visible = false;
        if (this.crosshair) this.crosshair.visible = false;
        if (this.userMarkerGroup) this.userMarkerGroup.visible = false;
       //isLandscapeMode = false;
    }
    update(delta) {
        this.starState.starTheta += 0.01;
        const starTheta = this.starState.starTheta;
        const radius = 10;
        const y = 5.0; // fix magasság, mindig a plain felett
        const x = radius * Math.cos(starTheta);
        const z = radius * Math.sin(starTheta);
        this.light.position.set(x, y, z);
        this.light.lookAt(0, 0, 0);
    }

    dispose() {
        this.scene.remove(this.terrain);
        this.terrain.geometry.dispose();
        this.terrain.material.map.dispose();
        this.terrain.material.displacementMap.dispose();
        this.terrain.material.dispose();
        if (this.userMarkerGroup) {
            this.scene.remove(this.userMarkerGroup);
            this.userMarkerGroup.clear();
            this.userMarkerGroup = null;
        }
    }
    updateUserMarkers(userMarkers, user_id) {
        if (!this.userMarkerGroup) {
            this.userMarkerGroup = new THREE.Group();
            this.scene.add(this.userMarkerGroup);
        }
        this.userMarkerGroup.clear();
        for (const u of userMarkers) {
            const isSelf = u.id == user_id;
            const lat = parseFloat(u.lat);
            const lon = parseFloat(u.lon);
            const x = (lon + 180) / 360 * 2 - 1;
            const z = (90 - lat) / 180 * 1 - 0.5;
            const dot = new THREE.Mesh(
                new THREE.SphereGeometry(0.005, 4, 4),
                new THREE.MeshBasicMaterial({ color: isSelf ? 0x00ff00 : 0xff0000  })
            );
            dot.position.set(x, -0.48, z);
            this.userMarkerGroup.add(dot);
        }
    }
}
