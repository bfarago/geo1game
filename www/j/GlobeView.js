import * as THREE from './three.module.js';
import { ViewMode } from './ViewMode.js';
import { SatelliteSystem } from './SatelliteSystem.js';

export class GlobeView extends ViewMode {
    constructor(scene, camera, light, globalambient, controls, renderer) {
        super("globe");
        this.selectedRegion = null;
        this.scene = scene;
        this.camera = camera;
        this.light = light;
        this.globalambient = globalambient;
        this.controls = controls;
        this.renderer = renderer;
        this.globe = null;
        this.ambient = null;
        this.crosshair = null;
        this.sateliteSystem = null;
        this.cloudLayerVisible = true;
        this.userMarkerGroup = null;
        this.graticuleGroup = new THREE.Group();
        this.graticuleVisible = false;
        this.atmosphere = null;
        this.regionKeyToIndex = {};
        this.options ={};
        this.initialized=false;
    }

    init() {
        this.light.position.set(3, 2, 2);
        this.light.intensity = 1.8;
        if (!this.ambient) {
            this.ambient = new THREE.AmbientLight(0x334488, 0.2);
            this.scene.add(this.ambient);
            this.scene.add(this.light);
            this.scene.add(this.globalambient);
        }
        if (!this.globe) {
            const texWidth = this.biomTexture?.image?.width || 2048;
            const texHeight = this.biomTexture?.image?.height || 1024;

            const widthSeg = Math.max(32, Math.min(512, texWidth / 2));
            const heightSeg = Math.max(16, Math.min(256, texHeight / 2));
            this.geometry = new THREE.SphereGeometry(1, widthSeg, heightSeg);
            this.material = new THREE.MeshStandardMaterial({
                map: this.biomTexture,
                displacementMap: this.elevTexture,
                displacementScale: 0.05,
                roughness: 1.0,
                metalness: 0.0,
            });
            this.globe = new THREE.Mesh(this.geometry, this.material);
            this.scene.add(this.globe);

            // Cloud layer
            this.cloudLayerVisible = true;
            const cloudGeometry = new THREE.SphereGeometry(1.05, 256, 256);
            const cloudMaterial = new THREE.MeshPhongMaterial({
                map: this.cloudTexture,
                transparent: true,
                opacity: 1.0,
                depthWrite: false
            });
            this.cloudMesh = new THREE.Mesh(cloudGeometry, cloudMaterial);
            this.scene.add(this.cloudMesh);
            this.scene.add(this.globe);
            this.crosshair = new THREE.Mesh(
                new THREE.SphereGeometry(0.01, 8, 8),
                new THREE.MeshBasicMaterial({ color: 0xff0000 })
            );
            this.scene.add(this.crosshair);

            // --- Crosshair bounding box visualization ---
            const boxMaterial = new THREE.LineBasicMaterial({ color: 0x00ff00 });
            const boxGeometry = new THREE.BufferGeometry();
            const boxLine = new THREE.LineSegments(boxGeometry, boxMaterial);
            boxLine.name = "crosshairBox";
            this.crosshairBox = boxLine;
            this.scene.add(boxLine);

            this.sateliteSystem = new SatelliteSystem(this.scene, this.camera);
            this.sateliteSystem.setVisible(true);

            this.createAtmosphere(); // TODO: shader need some inputs, which is not yet initialized here.
        }
        // --- Graticule initialization ---
        if (!this.scene.getObjectByName('graticuleGroup')) {
            this.createGraticule();
            this.graticuleGroup.name = 'graticuleGroup';
            this.scene.add(this.graticuleGroup);
        }
        this.graticuleGroup.visible = this.graticuleVisible;
        
        this.camera.fov = 60;
        this.camera.updateProjectionMatrix();
        this.controls.enableRotate = true;
        this.controls.enableZoom = true;
        this.controls.minDistance = 1.15;  // sugárnál kicsit nagyobb
        this.controls.maxDistance = 30;   // max távolság, ahonnan nézhet
        this.controls.enableDamping = true;
        this.controls.update();
        this.scene.background = new THREE.Color(0x000000);
        this.scene.fog = null;
        this.initialized=true;
    }

    createLabel(text, position) {
        const canvas = document.createElement('canvas');
        canvas.width = 128;
        canvas.height = 32;
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = 'white';
        ctx.font = '20px sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(text, canvas.width / 2, canvas.height / 2);
        const texture = new THREE.CanvasTexture(canvas);
        const material = new THREE.SpriteMaterial({ map: texture, transparent: true });
        const sprite = new THREE.Sprite(material);
        sprite.scale.set(0.1, 0.025, 1);
        sprite.position.copy(position);
        return sprite;
    }

    createGraticule() {
        const material = new THREE.LineBasicMaterial({ color: 0x4444ff });
        const segments = 64;
        const radius = 1.001;

        // Latitude lines
        for (let lat = -60; lat <= 60; lat += 30) {
            const phi = THREE.MathUtils.degToRad(90 - lat);
            const geometry = new THREE.BufferGeometry();
            const points = [];
            for (let i = 0; i <= segments; i++) {
                const theta = 2 * Math.PI * (i / segments);
                const x = radius * Math.sin(phi) * Math.cos(theta);
                const y = radius * Math.cos(phi);
                const z = radius * Math.sin(phi) * Math.sin(theta);
                points.push(new THREE.Vector3(x, y, z));
            }
            geometry.setFromPoints(points);
            this.graticuleGroup.add(new THREE.Line(geometry, material));
        }

        // Longitude lines
        for (let lon = 0; lon < 360; lon += 30) {
            const theta = THREE.MathUtils.degToRad(lon);
            const geometry = new THREE.BufferGeometry();
            const points = [];
            for (let i = -90; i <= 90; i++) {
                const phi = THREE.MathUtils.degToRad(90 - i);
                const x = radius * Math.sin(phi) * Math.cos(theta);
                const y = radius * Math.cos(phi);
                const z = radius * Math.sin(phi) * Math.sin(theta);
                points.push(new THREE.Vector3(x, y, z));
            }
            geometry.setFromPoints(points);
            this.graticuleGroup.add(new THREE.Line(geometry, material));
        }

        // ---- Add labels ----
        const labelRadius = 1.05; // Slightly above globe
        // Labels along Equator (lat = 0)
        for (let lon = 0; lon < 360; lon += 30) {
            const theta = THREE.MathUtils.degToRad(lon);
            const x = labelRadius * Math.cos(theta);
            const y = 0;
            const z = labelRadius * Math.sin(theta);
            const label = this.createLabel(`${lon}°`, new THREE.Vector3(x, y, z));
            this.graticuleGroup.add(label);
        }
        // Labels along Greenwich (lon = 0, so xz-plane, z=0)
        for (let lat = -60; lat <= 60; lat += 30) {
            const phi = THREE.MathUtils.degToRad(90 - lat);
            const x = labelRadius * Math.sin(phi);
            const y = labelRadius * Math.cos(phi);
            const z = 0;
            const label = this.createLabel(`${lat}°`, new THREE.Vector3(x, y, z));
            this.graticuleGroup.add(label);
        }
    }
    setOption(optionId, value, previous){
        const myprevious = this.options[optionId];
        if (myprevious != previous) console.log("the previous state was not in sync");
        // if (value === myprevious) return; // already in tha new state, nothing to do
        this.options[optionId] = value; //booking the state
        if (this.initialized) {
            // quickly update the UI
            switch(optionId){
                case 'star':
                    this.light.visible = value;
                    this.globalambient.visible = !value;
                    break;
                case 'clouds':
                    this.cloudLayerVisible = value;
                    this.cloudMesh.visible = value;
                    break;
                case 'satellites':
                    this.sateliteSystem.setVisible(value);
                    break;
                case 'graticule':
                    this.graticuleVisible = value;
                    if (this.graticuleGroup) {
                        this.graticuleGroup.visible = this.graticuleVisible;
                    }
                    break;
                case 'atmosphere':
                    this.atmosphere.visible=value;
                    break;
            }
        }
    }
    show() {
        if (!this.initialized) return;
        this.globe.visible = true;
        this.cloudMesh.visible = this.options.clouds;
        this.sateliteSystem.setVisible(this.options.satellites);
        this.graticuleVisible = this.options.graticule;
        if (this.graticuleGroup) {
            this.graticuleGroup.visible = this.graticuleVisible;
        }
        if (this.userMarkerGroup) this.userMarkerGroup.visible = true;
        if (this.regionLights) this.regionLights.visible = true;
        if (this.atmosphere) this.atmosphere.visible = this.options.atmosphere;
        if (this.options.star){
            this.light.visible = true;
            this.globalambient.visible = false;
        }else{
            this.light.visible = false;
            this.globalambient.visible = true;
        }
    }
    hide() {
        if (!this.initialized) return;
        this.globe.visible = false;
        this.cloudMesh.visible = false;
        this.sateliteSystem.setVisible(false);
        this.graticuleVisible = false;
        if (this.graticuleGroup) {
            this.graticuleGroup.visible = this.graticuleVisible;
        }
        if (this.userMarkerGroup) this.userMarkerGroup.visible = false;
        if (this.regionLights) this.regionLights.visible = false;
        if (this.atmosphere) this.atmosphere.visible = false;
    }
    update(delta) {
        this.starState.starTheta += 0.002;
        const starTheta = this.starState.starTheta;
        const inclination = this.starState.inclination;
        const cloudRotationSpeed = 0.001;

        const radius = 5;
        const x = radius * Math.cos(starTheta);
        const y = radius * Math.sin(inclination) * Math.sin(starTheta);
        const z = radius * Math.cos(inclination) * Math.sin(starTheta);
        this.light.position.set(x, y, z);
        this.light.lookAt(0, 0, 0);
        if (this.cloudMesh){
            this.cloudMesh.rotation.y += cloudRotationSpeed;
        }
        if (this.sateliteSystem){
            this.sateliteSystem.update(delta);
        }
    }
    renderRegionLights() {
        const positions = [], pollution = [], colors = [];
        const statuses = [];

        if (!this.regions) return;
        this.regionKeyToIndex = {};
        let index = 0;
        for (const key in this.regions) {
            this.regions[key]._bufferIndex = index;
            this.regionKeyToIndex[key] = index++;
            const [lat, lon] = key.split(',').map(Number);
            const { e, p } = this.regions[key];
            const phi = THREE.MathUtils.degToRad(90 - lat);
            const theta = THREE.MathUtils.degToRad(lon);
            const radius = 1.0 + e * 0.021;
            const x = radius * Math.sin(phi) * Math.cos(theta);
            const y = radius * Math.cos(phi);
            const z = radius * Math.sin(phi) * Math.sin(theta);
            positions.push(x, y, z);
            //if (p<1) p=Math.random();
            pollution.push(p/255.0);
            const brightness = Math.min(1.0, p * 2.0);
            colors.push(brightness, brightness * 0.6, 0.2);
            statuses.push(0.0); 
        }

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
        geometry.setAttribute('pollution', new THREE.Float32BufferAttribute(pollution, 1));
        geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
        geometry.setAttribute('status', new THREE.Float32BufferAttribute(statuses, 1));

        const vertexShader = `
        varying vec3 vWorldPosition;
        varying float vPollution;
        attribute float pollution;
        attribute float status;
        varying float vStatus;
        void main() {
          vStatus = status;
          vPollution = pollution;
          vec4 worldPosition = modelMatrix * vec4(position, 1.0);
          vWorldPosition = worldPosition.xyz;
          vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
          //gl_PointSize = 30.0 / -mvPosition.z;
          gl_PointSize = (30.0 + vStatus * 10.0) / -mvPosition.z;
          gl_Position = projectionMatrix * mvPosition;
        }`;

        const fragmentShader = `
        uniform vec3 glowColor;
        uniform vec3 dayColor;
        uniform vec3 lightDirection;
        uniform float time;
        varying vec3 vWorldPosition;
        varying float vPollution;
        varying float vStatus;
        void main() {
            // Use the actual surface normal for directional check
            vec4 finalColor = vec4(1.0);
            float dotNL = dot(vWorldPosition, lightDirection);
            float d = length(gl_PointCoord - vec2(0.5));
            float fade = 1.0 - smoothstep(0.0, 0.5, d);
            if (dotNL > 0.0){
                finalColor = vec4(dayColor, fade*0.8);
            }else{
                float flicker = 0.8 + 0.2 * sin(time * 40.0 + vPollution * 10.0) * vPollution;
                //float flicker = 1.0;
                finalColor = vec4(glowColor, fade * flicker);
            }
            
            if (vStatus > 1.5) {
                gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); // selected = élénk zöld, 100% opacity
            } else if (vStatus > 0.5) {
                gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0); // hover = élénk sárga, 100% opacity
            } else {
                gl_FragColor = finalColor; // normál fények
            }
        }`;

        const glowMaterial = new THREE.ShaderMaterial({
            uniforms: {
                glowColor: { value: new THREE.Color(1.0, 0.7, 0.3) },
                dayColor: { value: new THREE.Color(0.7, 0.2, 0.2) },
                lightDirection: { value: new THREE.Vector3() },
                time: { value: 0.0 }
            },
            vertexShader,
            fragmentShader,
            transparent: true,
            depthWrite: false,
            blending: THREE.AdditiveBlending
        });
        
        this.regionLights = new THREE.Points(geometry, glowMaterial);
        this.regionLights.material.uniformsNeedUpdate = true;
        this.scene.add(this.regionLights);
    }
    updateRegionStatus() {
        if (!this.regionLights) return;
        
        console.log('Hovered:', this.hoveredRegion);
        console.log('Selected:', this.selectedRegion);
        console.log('Region keys:', Object.keys(this.regionKeyToIndex));

        const positions = this.regionLights.geometry.attributes.position;
        const statuses = this.regionLights.geometry.attributes.status;
        
        for (let i = 0; i < positions.count; i++) {
            statuses.setX(i, 0.0);
        }
        if (this.hoveredRegion) {
            const key = `${this.hoveredRegion.lat},${this.hoveredRegion.lon}`;
            const idx = this.regionKeyToIndex[key];
            if (idx !== undefined) {
                statuses.setX(idx, 1.0); // hover
            }
        }
        if (this.selectedRegion) {
            const key = `${this.selectedRegion.lat},${this.selectedRegion.lon}`;
            const idx = this.regionKeyToIndex[key];
            if (idx !== undefined) {
                statuses.setX(idx, 2.0); // selected
            }
        }
        statuses.needsUpdate = true;
        this.regionLights.material.needsUpdate = true;
    }
    updateRegionLights() {
        if (!this.regionLights) return;
        const dayside = this.light.position;
        const nightSide = dayside.clone().normalize().negate();
        this.regionLights.material.uniforms.lightDirection.value.copy(dayside).normalize();
        this.regionLights.material.uniforms.time.value = performance.now() * 0.001;
        if (this.atmosphere){
            this.atmosphere.material.uniforms.nightSide.value.copy(nightSide);
          }
    }
    createAtmosphere() {
        if (this.atmosphere)return; //create only once
        const geometry = new THREE.SphereGeometry(1.05, 128, 64); // kicsit nagyobb mint a bolygó
        
        const vertexShader = `
            varying vec3 vWorldPosition;
            void main() {
            vec4 worldPosition = modelMatrix * vec4(position, 1.0);
            vWorldPosition = normalize(worldPosition.xyz);
            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            }
        `;
        
        const fragmentShader = `
            uniform vec3 glowColor;
            uniform vec3 nightSide;
            varying vec3 vWorldPosition;
        
            void main() {
            float nDot = dot(vWorldPosition, nightSide); // -1 = szemben
            float edgeFactor = dot(vWorldPosition, vec3(0.0, 0.0, 1.0)); // horizont
        
            float intensity = smoothstep(0.2, 0.6, edgeFactor) * smoothstep(-0.3, -0.1, nDot);
            vec3 aurora = mix(glowColor, vec3(1.0, 0.6, 0.2), edgeFactor);
        
            gl_FragColor = vec4(aurora, intensity * 0.5);
            }
        `;
        
        const material = new THREE.ShaderMaterial({
            uniforms: {
            glowColor: { value: new THREE.Color(0.3, 0.6, 1.0) }, // halvány kékes
            nightSide: { value: new THREE.Vector3(0, 0, -1) } // ide kerül a Nap ellentett iránya
            },
            vertexShader,
            fragmentShader,
            side: THREE.BackSide, // belülről látszódjon
            transparent: true,
            blending: THREE.AdditiveBlending,
            depthWrite: false
        });
        
       this.atmosphere = new THREE.Mesh(geometry, material);
       this.scene.add(this.atmosphere);  // TODO: if I add this line, the loading screen stops at 50%, which is preload2, the init() call of this object.
    }
    _calculateLatLon(point) {
        const r = point.length();
        const lat = 90 - (Math.acos(point.y / r) * 180 / Math.PI);
        let lon = 180 + (Math.atan2(point.z, point.x) * 180 / Math.PI);
        if (lon < -180) lon += 360;
        if (lon > 180) lon -= 360;
        return { lat, lon };
    }

    latLonAltToScreen(lat, lon, alt) {
        const radius = alt;
        const phi = THREE.MathUtils.degToRad(90 - lat);
        const theta = THREE.MathUtils.degToRad(lon + 180);

        const x = radius * Math.sin(phi) * Math.cos(theta);
        const y = radius * Math.cos(phi);
        const z = radius * Math.sin(phi) * Math.sin(theta);

        const worldPos = new THREE.Vector3(x, y, z);
        worldPos.project(this.camera);

        return {
            x: (worldPos.x + 1) / 2 * window.innerWidth,
            y: (-worldPos.y + 1) / 2 * window.innerHeight
        };
    }

    latLonAltToWorldPosition(lat, lon, alt) {
        const radius = alt;
        const phi = THREE.MathUtils.degToRad(90 - lat);
        const theta = THREE.MathUtils.degToRad(lon + 180);

        const x = radius * Math.sin(phi) * Math.cos(theta);
        const y = radius * Math.cos(phi);
        const z = radius * Math.sin(phi) * Math.sin(theta);

        return new THREE.Vector3(x, y, z);
    }

    screenToLatLonAlt(x, y) {
        const ndcX = (x / window.innerWidth) * 2 - 1;
        const ndcY = -(y / window.innerHeight) * 2 + 1;
        const ndc = new THREE.Vector3(ndcX, ndcY, 0.5);

        ndc.unproject(this.camera);

        const direction = ndc.sub(this.camera.position).normalize();
        const origin = this.camera.position.clone();

        // Intersection with sphere of radius ~1.0
        const a = direction.dot(direction);
        const b = 2 * origin.dot(direction);
        const c = origin.dot(origin) - 1;

        const discriminant = b * b - 4 * a * c;
        if (discriminant < 0) {
            return null;
        }

        const t = (-b - Math.sqrt(discriminant)) / (2 * a);
        if (t < 0) return null;

        const intersect = origin.add(direction.multiplyScalar(t));
        return this._calculateLatLon(intersect);
    }

    screenToWorldPosition(x, y) {
        const ndcX = (x / window.innerWidth) * 2 - 1;
        const ndcY = -(y / window.innerHeight) * 2 + 1;
        const ndc = new THREE.Vector3(ndcX, ndcY, 0.5);

        ndc.unproject(this.camera);

        const direction = ndc.sub(this.camera.position).normalize();
        const origin = this.camera.position.clone();

        const a = direction.dot(direction);
        const b = 2 * origin.dot(direction);
        const c = origin.dot(origin) - 1;

        const discriminant = b * b - 4 * a * c;
        if (discriminant < 0) {
            return null;
        }

        const t = (-b - Math.sqrt(discriminant)) / (2 * a);
        if (t < 0) return null;

        return origin.add(direction.multiplyScalar(t));
    }
    hoverOverLatLonPoint(latlon) {
        const { lat, lon } = latlon;
        if (this.regions) {
            let closestRegion = null;
            let closestDistanceSq = Infinity;
            for (const key in this.regions) {
                const [rLat, rLon] = key.split(',').map(Number);
                const dLat = lat - rLat;
                const dLon = lon - rLon;
                const distanceSq = dLat * dLat + dLon * dLon;
                if (distanceSq < closestDistanceSq) {
                    closestDistanceSq = distanceSq;
                    closestRegion = { lat: rLat, lon: rLon, data: this.regions[key] };
                }
            }
            if (this.onRegionHover) {
                const maxAllowedDistanceSq = 300.0; // threshold for considering a region "close enough"
                let newSelectedRegion = null;
                if (closestRegion && closestDistanceSq < maxAllowedDistanceSq) {
                    newSelectedRegion = closestRegion;
                }
                const oldKey = this.hoveredRegion ? `${this.hoveredRegion.lat},${this.hoveredRegion.lon}` : null;
                const newKey = newSelectedRegion ? `${newSelectedRegion.lat},${newSelectedRegion.lon}` : null;
                if (oldKey !== newKey) {
                    if (oldKey !== null) {
                        let index = this.hoveredRegion.data._bufferIndex;
                        this.regionLights.geometry.attributes.status.setX(index,0.0);
                    }
                    if (newKey !== null) {
                        let index = newSelectedRegion.data._bufferIndex;
                        
                        this.regionLights.geometry.attributes.status.setX(index,1.0);
                    }
                    this.hoveredRegion = newSelectedRegion;
                    //this.updateRegionStatus();
 //                   statuses.needsUpdate = true;
                    this.regionLights.geometry.attributes.status.needsUpdate = true;
                    this.regionLights.material.needsUpdate = true;
                    this.onRegionHover(newSelectedRegion);
                }
            }
        }
    }
    clickOnLatLonPoint(latlon) {
        const { lat, lon } = latlon;
        if (this.regions) {
            let closestRegion = null;
            let closestDistanceSq = Infinity;
            for (const key in this.regions) {
                const [rLat, rLon] = key.split(',').map(Number);
                const dLat = lat - rLat;
                const dLon = lon - rLon;
                const distanceSq = dLat * dLat + dLon * dLon;
                if (distanceSq < closestDistanceSq) {
                    closestDistanceSq = distanceSq;
                    closestRegion = { lat: rLat, lon: rLon, data: this.regions[key] };
                }
            }
            if (this.onRegionHover) {
                const maxAllowedDistanceSq = 250.0; // threshold for considering a region "close enough"
                let newSelectedRegion = null;
                if (closestRegion && closestDistanceSq < maxAllowedDistanceSq) {
                    newSelectedRegion = closestRegion;
                }
                const oldKey = this.selectedRegion ? `${this.selectedRegion.lat},${this.selectedRegion.lon}` : null;
                const newKey = newSelectedRegion ? `${newSelectedRegion.lat},${newSelectedRegion.lon}` : null;
                if (oldKey !== newKey) {
                    this.hoveredRegion = newSelectedRegion;
                    this.updateRegionStatus();
                    this.onRegionClick(newSelectedRegion);
                }
            }
        }
    }
    handleHover(raycaster) {
        if (!this.onRegionHover) return;
        const intersects = raycaster.intersectObject(this.globe);
        if (intersects.length > 0) {
            const point = intersects[0].point;
            const { lat, lon } = this._calculateLatLon(point);
            this.hoverOverLatLonPoint({ lat, lon });
        }
    }

    handleClick(raycaster) {
        if (!this.onRegionClick) return;
        const intersects = raycaster.intersectObject(this.globe);
        if (intersects.length > 0) {
            const point = intersects[0].point;
            const { lat, lon } = this._calculateLatLon(point);
            this.onRegionClick({ lat, lon });
        }
    }

    handleDoubleClick(raycaster) {
        const intersects = raycaster.intersectObject(this.globe);
        if (intersects.length > 0) {
            const point = intersects[0].point;
            const { lat, lon } = this._calculateLatLon(point);
            const alt = 1.05;
            if (this.crosshairCallback) {
                this.crosshairCallback(lat, lon, alt);
            }
        }
    }
    dispose() {
        this.scene.remove(this.cloudMesh);
        this.cloudMesh.geometry.dispose();
        this.cloudMesh.material.map.dispose();
        this.cloudMesh.material.dispose();
        this.scene.remove(this.ambient);
        this.ambient.dispose();
        this.scene.remove(this.globe);
        this.globe.geometry.dispose();
        this.globe.material.dispose();
        if (this.userMarkerGroup) {
            this.scene.remove(this.userMarkerGroup);
            this.userMarkerGroup.clear();
            this.userMarkerGroup = null;
        }
    }
    updateCrosshair(lat, lon, alt) {
        if (!this.crosshair) return;
        const radius = alt;
        const phi = (90 - lat) * Math.PI / 180;
        const theta = (lon + 180) * Math.PI / 180;

        const x = radius * Math.sin(phi) * Math.cos(theta);
        const y = radius * Math.cos(phi);
        const z = radius * Math.sin(phi) * Math.sin(theta);

        this.crosshair.position.set(x, y, z);

        // --- Update crosshair bounding box visualization ---
        if (!this.crosshairBox) return;

        const angularDistance = 500 / 6371; // Approx 500 km in radians on unit sphere
        const half = angularDistance / Math.SQRT2;

        const center = new THREE.Vector3(
            Math.sin(phi) * Math.cos(theta),
            Math.cos(phi),
            Math.sin(phi) * Math.sin(theta)
        );

        // Orthonormal tangent frame
        const up = center.clone().normalize();
        const east = new THREE.Vector3(-Math.sin(theta), 0, Math.cos(theta)).normalize();
        // Compute corrected north
        const correctedNorth = new THREE.Vector3().crossVectors(east, up).normalize();

        const r1 = alt + 0.005;
        const r2 = 0.95;

        const p1 = center.clone().addScaledVector(correctedNorth,  half).addScaledVector(east, -half).normalize();
        const p2 = center.clone().addScaledVector(correctedNorth,  half).addScaledVector(east,  half).normalize();
        const p3 = center.clone().addScaledVector(correctedNorth, -half).addScaledVector(east,  half).normalize();
        const p4 = center.clone().addScaledVector(correctedNorth, -half).addScaledVector(east, -half).normalize();

        const verts = [
            p1.clone().multiplyScalar(r1), p2.clone().multiplyScalar(r1),
            p2.clone().multiplyScalar(r1), p3.clone().multiplyScalar(r1),
            p3.clone().multiplyScalar(r1), p4.clone().multiplyScalar(r1),
            p4.clone().multiplyScalar(r1), p1.clone().multiplyScalar(r1),

            p1.clone().multiplyScalar(r1), p1.clone().multiplyScalar(r2),
            p2.clone().multiplyScalar(r1), p2.clone().multiplyScalar(r2),
            p3.clone().multiplyScalar(r1), p3.clone().multiplyScalar(r2),
            p4.clone().multiplyScalar(r1), p4.clone().multiplyScalar(r2)
        ];

        const positions = new Float32Array(verts.length * 3);
        verts.forEach((v, i) => {
            positions[i * 3] = v.x;
            positions[i * 3 + 1] = v.y;
            positions[i * 3 + 2] = v.z;
        });

        this.crosshairBox.geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        this.crosshairBox.geometry.setDrawRange(0, verts.length);
        this.crosshairBox.geometry.attributes.position.needsUpdate = true;
    }
    updateUserMarkers(userMarkers, user_id) {
        if (!this.userMarkerGroup) {
            this.userMarkerGroup = new THREE.Group();
            this.scene.add(this.userMarkerGroup);
        }
        this.userMarkerGroup.clear();
        for (const u of userMarkers) {
            const lat = parseFloat(u.lat);
            const lon = parseFloat(u.lon);
            const alt = parseFloat(u.alt || 1.02);
            const phi = THREE.MathUtils.degToRad(90 - lat);
            const theta = THREE.MathUtils.degToRad(lon + 180);
            const r = alt + 0.02;
            const x = r * Math.sin(phi) * Math.cos(theta);
            const y = r * Math.cos(phi);
            const z = r * Math.sin(phi) * Math.sin(theta);
            const isSelf = u.id === user_id;
            // Create marker sphere
            const dot = new THREE.Mesh(
                new THREE.SphereGeometry(0.005, 6, 6),
                new THREE.MeshBasicMaterial({ color: isSelf ? 0x00ff00 : 0xff0000 })
            );
            dot.position.set(x, y, z);
            this.userMarkerGroup.add(dot);
            // Create line from marker toward the center
            const points = [
                new THREE.Vector3(x, y, z),
                new THREE.Vector3(x * 0.96, y * 0.96, z * 0.96) // toward center, slight reduction
            ];
            const lineGeo = new THREE.BufferGeometry().setFromPoints(points);
            const lineMat = new THREE.LineBasicMaterial({ color: isSelf ? 0x00ff00 : 0xff0000 });
            const line = new THREE.Line(lineGeo, lineMat);
            this.userMarkerGroup.add(line);
            // Add 3D text label
            if (u.nick) {
                const canvas = document.createElement('canvas');
                canvas.width = 256;
                canvas.height = 64;
                const ctx = canvas.getContext('2d');
                ctx.fillStyle = 'white';
                ctx.font = '20px sans-serif';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.clearRect(0, 0, canvas.width, canvas.height);
                ctx.fillText(u.nick, canvas.width / 2, canvas.height / 2);
                const texture = new THREE.CanvasTexture(canvas);
                const material = new THREE.SpriteMaterial({ map: texture, transparent: true });
                const sprite = new THREE.Sprite(material);
                sprite.scale.set(0.3, 0.075, 1);
                sprite.position.set(x, y + 0.02, z);
                this.userMarkerGroup.add(sprite);
            }
        }
    }
}