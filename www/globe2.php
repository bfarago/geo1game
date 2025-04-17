<?php header('Content-Type: text/html; charset=UTF-8'); ?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Geo Globe (Textúrával)</title>
  <style>
    html, body, canvas {
      margin: 0;
      padding: 0;
      width: 100%;
      height: 100%;
      display: block;
      background: black;
    }
#ui-menu {
  position: absolute;
  top: 10px;
  right: 10px;
  background: rgba(20, 20, 20, 0.7);
  border-radius: 8px;
  padding: 10px;
  z-index: 10;
}
#ui-menu button {
  display: block;
  width: 130px;
  margin: 5px 0;
  padding: 6px 10px;
  color: white;
  background: #333;
  border: none;
  border-radius: 5px;
  cursor: pointer;
}
#ui-menu button:hover {
  background: #555;
}
  #loading {
    position: fixed;
    top: 0; left: 0;
    width: 100%; height: 100%;
    background: black;
    color: white;
    font-size: 2em;
    display: flex;
    justify-content: center;
    align-items: center;
    z-index: 9999;
    transition: opacity 0.5s ease;
  }
  #infoPanel {
    color: white;
    font-size: 0.2em;
  }
</style>
</head>
<body>
  <div id="loading">
    Loading...
  </div>
<canvas id="globe" style="display: none;"></canvas>
<script type="module">
    import * as THREE from './j/three.module.js';
    import { OrbitControls } from './j/OrbitControls.js';

    let isLandscapeMode = false;
    let starTheta = 0;
    let cloudRotationSpeed = 0.001;
    let file_biome = 'm/biome.png';
    let file_elevation = 'm/elevation.png';
    let file_clouds = 'm/clouds.png';
    let selLat, selLon, selAlt;

    const inclination = THREE.MathUtils.degToRad(23.4);
    const canvas = document.getElementById('globe');
    const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    renderer.setSize(window.innerWidth, window.innerHeight);

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
    camera.position.z = 3;
  
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.minDistance = 1.15; // greater than radius
    controls.maxDistance = 15;   // max distance to camera
    controls.enableDamping = true;
    const raycaster = new THREE.Raycaster();
    const mouse = new THREE.Vector2();

    // Load all textures
    const loader = new THREE.TextureLoader();
    let biomeTexture, elevationTexture, cloudTexture;

    const light = new THREE.DirectionalLight(0xffffff, 1);
    const globalambient = new THREE.AmbientLight(0xffffff, 1);
    globalambient.visible=false;

    class ViewMode {
        constructor(name) { this.name = name; }
        init(scene, camera) {}
        update(delta) {}
        updateCrosshair(lat, lon, alt) {}
        dbclick(raycaster) {}
        show() {}
        hide() {}
        dispose() {}
    }
    export class SatelliteSystem {
        constructor(scene, camera) {
            this.scene = scene;
            this.camera = camera;
            this.satellites = [];
            this.numSats = 8000;
            this.satGroup = new THREE.Group();
            scene.add(this.satGroup);
            this.initSatellites();
        }

        initSatellites() {
            const geometry = new THREE.SphereGeometry(0.001, 4, 4);
            const material = new THREE.MeshBasicMaterial({ color: 0xffcc00 });

            for (let i = 0; i < this.numSats; i++) {
                const mesh = new THREE.Mesh(geometry, material);
                let orbitRadius , inclination, scale;
                if (i % 3 === 0) {
                    scale=1;
                }else{
                    scale=THREE.MathUtils.randFloat(0.001, 0.01);
                    mesh.scale.set(scale, scale, scale);
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
                    theta: phase
                };
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
            }
        }

        setVisible(visible) {
            this.satGroup.visible = visible;
        }
    }

    class GlobeView extends ViewMode {
        constructor() {
            super("globe");
            this.globe = null;
            this.ambient = null;
            this.crosshair = null;
            this.sateliteSystem=null;
            this.cloudLayerVisible = true;
        }

        init(scene, camera) {
            const widthSeg = 360*2;
            const heightSeg = 180*2;
            light.position.set(3, 2, 2);
            
            if (!this.ambient) { 
                this.ambient = new THREE.AmbientLight(0x334488, 0.2);
                scene.add(this.ambient);

                scene.add(light);
                scene.add(globalambient);
            }
            if (!this.globe){
                this.geometry = new THREE.SphereGeometry(1, widthSeg, heightSeg);
                this.material = new THREE.MeshStandardMaterial({
                    map: biomeTexture,
                    displacementMap: elevationTexture,
                    displacementScale: 0.05,
                    roughness: 1.0,
                    metalness: 0.0,
                });
                this.globe = new THREE.Mesh(this.geometry, this.material);
                scene.add(this.globe);
            
                // Cloud layer
                this.cloudLayerVisible = true;
                const cloudGeometry = new THREE.SphereGeometry(1.05, 256, 256);
                const cloudMaterial = new THREE.MeshPhongMaterial({
                    map: cloudTexture,
                    transparent: true,
                    opacity: 1.0,
                    depthWrite: false
                });
                this.cloudMesh = new THREE.Mesh(cloudGeometry, cloudMaterial);
                scene.add(this.cloudMesh);
                scene.add(this.globe);
                this.crosshair = new THREE.Mesh(
                    new THREE.SphereGeometry(0.01, 8, 8),
                    new THREE.MeshBasicMaterial({ color: 0xff0000 })
                );
                scene.add(this.crosshair);
                this.sateliteSystem = new SatelliteSystem(scene, camera); 
                this.sateliteSystem.setVisible(true);
            }
            camera.fov=60;
            camera.updateProjectionMatrix();
            controls.enableRotate = true;
            controls.enableZoom = true;
            controls.minDistance = 1.15;  // sugárnál kicsit nagyobb
            controls.maxDistance = 30;   // max távolság, ahonnan nézhet
            controls.enableDamping = true;
            controls.update();
            scene.background = new THREE.Color(0x000000);
            scene.fog = null;
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
        }
        show() {
            this.globe.visible = true;
            this.cloudMesh.visible = true;
            this.sateliteSystem.setVisible(true);
            isLandscapeMode = false;
        }
        hide() {
            this.globe.visible = false;
            this.cloudMesh.visible = false;
            this.sateliteSystem.setVisible(false);
        }
        toggleClouds() {
            this.cloudLayerVisible = !this.cloudLayerVisible;
            this.cloudMesh.visible = this.cloudLayerVisible;
        }
        update(delta) {
            starTheta += 0.002;
            const radius = 5;
            const x = radius * Math.cos(starTheta);
            const y = radius * Math.sin(inclination) * Math.sin(starTheta);
            const z = radius * Math.cos(inclination) * Math.sin(starTheta);
            light.position.set(x, y, z);
            light.lookAt(0, 0, 0);
            if (this.cloudMesh){
                this.cloudMesh.rotation.y += cloudRotationSpeed;
            }
            if (this.sateliteSystem){
                this.sateliteSystem.update(delta);
            }
        }
        dbclick(raycaster) {
            const intersects = raycaster.intersectObject(this.globe); // globe a THREE.Mesh-ed
            if (intersects.length > 0) {
                const point = intersects[0].point;
                const r = point.length();
                const lat = 90 - (Math.acos(point.y / r) * 180 / Math.PI);
                let lon = 180 + (Math.atan2(point.z, point.x) * 180) / Math.PI;
                if (lon < -180) lon += 360;
                if (lon > 180) lon -= 360;
                console.log("Clicked at:", lat.toFixed(4), lon.toFixed(4));
                updateCrosshair(lat, lon, 1.05);
            }
        }
        dispose() {
            scene.remove(this.cloudMesh);
            this.cloudMesh.geometry.dispose();
            this.cloudMesh.material.map.dispose();
            this.cloudMesh.material.dispose();
            scene.remove(this.ambient);
            this.ambient.dispose();
            scene.remove(this.globe);
            this.globe.geometry.dispose();
            this.globe.material.dispose();
        }
    }
    class TerrainView extends ViewMode {
        constructor() {
            super("terrain");
            this.terrain = null;
            this.crosshair = null;
            this.cloudLayer = null;
            this.cloudLayerVisible = true;
        }

        init(scene, camera) {
            if (!this.terrain){
                const widthSeg = 360*2;
                const heightSeg = 180*2;
                const geometry = new THREE.PlaneGeometry(2, 1, widthSeg - 1, heightSeg - 1);
                const material = new THREE.MeshStandardMaterial({
                    map: biomeTexture,
                    displacementMap: elevationTexture,
                    displacementScale: 0.05,
                    displacementBias: -0.03 
                });

                this.terrain = new THREE.Mesh(geometry, material);
                this.terrain.position.y = -0.5;
                this.terrain.rotation.x = -Math.PI / 2;
                scene.add(this.terrain);
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
                scene.add(this.crosshair);
                //Cloud layer
                const cloudGeometry = new THREE.PlaneGeometry(2, 1, 1, 1); // Ugyanolyan méret, mint a domborzat
                const cloudMaterial = new THREE.MeshStandardMaterial({
                    map: cloudTexture,
                    transparent: true,
                    opacity: 0.7,  // finomabb hatás
                    depthWrite: false,
                });

                this.cloudLayer = new THREE.Mesh(cloudGeometry, cloudMaterial);
                this.cloudLayer.rotation.x = -Math.PI / 2;
                this.cloudLayer.position.y = -0.45; // domborzat felett legyen
                this.cloudLayer.renderOrder = 1; // felülre kényszerítve
                scene.add(this.cloudLayer);
            }
            scene.background = new THREE.Color(0x87ceeb); // világoskék ég
            scene.fog = new THREE.Fog(0x87ceeb, 15, 50);   // horizontba olvadás

            this.terrain.receiveShadow = true;
            this.terrain.castShadow = true;
            light.castShadow = true;
            light.shadow.mapSize.width = 4096;
            light.shadow.mapSize.height = 4096;
            renderer.shadowMap.enabled = true;
            renderer.shadowMap.type = THREE.PCFSoftShadowMap;
            controls.enableRotate = true;
            controls.enableZoom = true;
            controls.minDistance = 0.01; // greater than radius
            controls.maxDistance = 15;   // max distance to camera
            controls.enableDamping = true;
            camera.fov=20;
            camera.position.set(0, 0.2, 0.12);
            camera.lookAt(0, 0.2, 0.1);
            camera.updateProjectionMatrix();
        }
        toggleClouds() {
            this.cloudLayerVisible = !this.cloudLayerVisible;
            this.cloudLayer.visible = this.cloudLayerVisible;
        }
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
                updateCrosshair(lat, -lon+180, 1.05);
            }
        }
        show() {
            this.terrain.visible = true;
            if (this.cloudLayer) this.cloudLayer.visible = true;
            if (this.crosshair) this.crosshair.visible = true;
            isLandscapeMode = true;
        }
        hide() {
            this.terrain.visible = false;
            if (this.cloudLayer) this.cloudLayer.visible = false;
            if (this.crosshair) this.crosshair.visible = false;
            isLandscapeMode = false;
        }
        update(delta) {
            starTheta += 0.01;
            const radius = 10;
            const y = 5.0; // fix magasság, mindig a plain felett
            const x = radius * Math.cos(starTheta);
            const z = radius * Math.sin(starTheta);
            light.position.set(x, y, z);
            light.lookAt(0, 0, 0);
        }

        dispose() {
            scene.remove(this.terrain);
            this.terrain.geometry.dispose();
            this.terrain.material.map.dispose();
            this.terrain.material.displacementMap.dispose();
            this.terrain.material.dispose();
            
        }
    }

    let currentView = null;
    let globView = new GlobeView();
    let terrainView = new TerrainView();

    function updateCrosshair(lat, lon, alt) {
        if (lon > 180) lon -= 360;
        if (lon < -180) lon += 360;
        terrainView.updateCrosshair(lat, -lon+180, alt);
        globView.updateCrosshair(lat, lon, alt);
        document.getElementById("latVal").textContent = lat.toFixed(2);
        document.getElementById("lonVal").textContent = lon.toFixed(2);
        document.getElementById("altVal").textContent = alt.toFixed(2);
    }
    function updateLatLonDisplay() {
//        const { lat, lon, alt } = getCurrentCameraPositionData();
    }

    function getGlobeViewLatLon(camera) {
        // Kamera nézeti irány vektora
        const direction = new THREE.Vector3();
        camera.getWorldDirection(direction);

        // Feltételezve, hogy a gömb a (0, 0, 0) középpontban van és egységsugarú
        const pos = direction.normalize();

        // Gömbi koordináták számítása
        const lat = Math.asin(pos.y) * (180 / Math.PI); // -90 .. +90
        const lon = Math.atan2(pos.z, pos.x) * (180 / Math.PI); // -180 .. +180

        return { lat, lon };
    }
    function getCurrentCameraPositionData() {
        const { lat, lon } = getGlobeViewLatLon(camera);
        const distance = camera.position.length();
        return { lat, lon, alt: distance };
    }
    function positionTerrainCamera(lat, lon) {
        const x = (lon + 180) / 360;
        const z = (90 - lat) / 180;

        // Terrain plane mérete: 2 x 1 (ha így definiáltad)
        const terrainX = x * 2;
        const terrainZ = z * 1;

        // Kamera a nézett pont közelébe kerül, fentről és kissé hátrébbról néz rá
        camera.position.set(terrainX, 1, terrainZ + 0.02);
        camera.lookAt(new THREE.Vector3(terrainX, 0, terrainZ));
    }

    function switchView(newView) {
        if (currentView) {
            currentView.hide();
        }
        currentView = newView;
        currentView.init(scene, camera);
        currentView.show();
    }
    let clock = new THREE.Clock();
    function animate() {
        requestAnimationFrame(animate);
        controls.update();
        if (currentView) {
            const delta = clock.getDelta()
            currentView.update(delta);
        }
        renderer.render(scene, camera);
        const distance = camera.position.length();
        if (!isLandscapeMode  && distance < 1.3) {
            const { lat, lon } = getGlobeViewLatLon(camera);
            switchView(terrainView);
            positionTerrainCamera(lat, lon);
        } else if (isLandscapeMode && distance > 3) {
            switchView(globView);
        }
        updateLatLonDisplay();
    }

    animate();

    function toggleClouds() {
        currentView.toggleClouds();
        const button = document.querySelector('#ui-menu button:nth-child(1)');
        button.textContent = globView.cloudLayerVisible ? '🌤 Clouds: 0': '☁ Clouds: 1';
    }
    function toggleSun() {
//        globView.toggleSun();
        if (light.visible) {
            light.visible = false;
            globalambient.visible = true;
        } else {
            light.visible = true;
            globalambient.visible = false;
        }
    }
    function toggleHelp() {
        const modal = document.getElementById("helpModal");
        modal.style.display = (modal.style.display === "flex") ? "none" : "flex";
    }
    // Global functions
    window.toggleClouds = function() {
        toggleClouds();
    };
    window.toggleSun = function() {
        toggleSun();
    };
    window.toggleHelp = function() {
        toggleHelp();
    };
    window.addEventListener('keydown', (event) => {
        if (event.key.toLowerCase() === 'c') {
            toggleClouds()
        } else if(event.key.toLowerCase() === 's') {
            toggleSun();
        } else if (event.key.toLowerCase() === 'h') {
            toggleHelp();
        }   
    });
    window.addEventListener('resize', () => {
        camera.aspect = window.innerWidth / window.innerHeight;
        camera.updateProjectionMatrix();
        renderer.setSize(window.innerWidth, window.innerHeight);
    });
    function onActivatePoint(mouse){
        raycaster.setFromCamera(mouse, camera);
        if (!currentView) return;
        currentView.dbclick(raycaster);
    }
    function onDoubleClick(event) {
        // Normalizált egérpozíció (-1 .. 1)
        mouse.x = (event.clientX / window.innerWidth) * 2 - 1;
        mouse.y = - (event.clientY / window.innerHeight) * 2 + 1;
        onActivatePoint(mouse);
    }
    let lastTouch = 0;
    function onTouchEnd(e) {
        if (e.changedTouches.length === 0) return;
        const touch = e.changedTouches[0];
        const now = Date.now();
        if (now - lastTouch < 300) {
            const rect = renderer.domElement.getBoundingClientRect();
            mouse.x = ((touch.clientX - rect.left) / rect.width) * 2 - 1;
            mouse.y = -((touch.clientY - rect.top) / rect.height) * 2 + 1;
            onActivatePoint(mouse);
        }
        lastTouch = now;
    }
    renderer.domElement.addEventListener("dblclick", onDoubleClick); // mouse
    renderer.domElement.addEventListener("touchend", onTouchEnd);    // touch
    async function preload1(){
        await Promise.all([
            new Promise((resolve) => {
                biomeTexture=loader.load(file_biome, resolve);
            }),
            new Promise((resolve) => {
                elevationTexture=loader.load(file_elevation, resolve);
            }),
            new Promise((resolve) => {
                cloudTexture=loader.load(file_clouds, resolve);
            })
        ]);
        elevationTexture.magFilter = THREE.LinearFilter;
        elevationTexture.minFilter = THREE.LinearFilter;
        /*
        biomeTexture.minFilter = THREE.LinearFilter;
        elevationTexture.minFilter = THREE.LinearFilter;
        */  
    }
    async function preload2(){
        await terrainView.init(scene, camera);
        terrainView.hide();
        await globView.init(scene, camera);
    }
    async function preload(){
        document.getElementById("loading").style.display = "flex";
        await preload1();
        await preload2();
    }
    await new Promise(r => setTimeout(r, 3));
    await preload(); // mutatja a "Loading..."-ot
    
    document.getElementById("loading").style.display = "none";
    document.getElementById("globe").hidden = false;
    canvas.hidden=false;
    // Alapnézet indítása
    camera.position.z = 3;
    switchView(globView);
    globView.show();
    canvas.style.display = "block";
    
</script>
<div id="ui-menu">
  <button onclick="toggleClouds()">'🌤 No Clouds'</button>
  <button onclick="toggleSun()">'🌤 Sun: 0'</button>
  <button onclick="window.location.href='index.php'">Menu</button>
  <button onclick="window.location.href='terrain.php'">Terrain</button>
  <button onclick="toggleHelp()">Help</button>
  <span id="infoPanel">LAT: <span id="latVal">-</span>, LON: <span id="lonVal">-</span>, ALT: <span id="altVal">-</span></span>  
</div>
<div id="helpModal" style="display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%;
     background-color: rgba(0,0,0,0.6); z-index: 1000; justify-content: center; align-items: center;">

  <div style="color: rgb(200, 228, 217); background: rgba(127, 109, 109, 0.4); padding: 20px; max-width: 600px; width: 90%; border-radius: 8px; box-shadow: 0 0 10px black;">
    <h2>Help</h2>
    <p>
      This is a planetary simulation demo.<br>
      - <b>Click</b> or <b>drag</b> to rotate the globe.<br>
      - Use <b>mouse wheel</b> to zoom in/out.<br>
      - Zoom closer to enter airplane mode, zoom out to enter globe mode.<br>
      - <b>Double click</b> to select a point on the globe or the map.<br>
      - Press <b>C</b> to toggle clouds.<br>
      - Press <b>S</b> to toggle sun.<br>
      - Press <b>H</b> for this help.
      
    </p>
    <button onclick="toggleHelp()" style="margin-top: 10px;">OK</button>
  </div>
</div>
</body>
</html>
