<?php
require_once('db.php');
require_once('geo.php');
session_start();
geo_session_start();
header('Content-Type: text/html; charset=UTF-8');

?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>GEO a game</title>
  <link rel="stylesheet" href="geo.css">
  <meta name="viewport" content="width=device-width, height=device-height, initial-scale=1, maximum-scale=1, user-scalable=no">
</head>
</head>
<body>
<div id="loading">
    <div>Loading...</div>
    <progress id="loadingProgress" value="0" max="100"></progress>
    <span id="loadingText"></span>
</div>
<script type="module">
    import * as THREE from './j/three.module.js';
    import { OrbitControls } from './j/OrbitControls.js';
    import { SatelliteSystem } from './j/SatelliteSystem.js';
    import { ViewMode } from './j/ViewMode.js';
    import { GlobeView } from './j/GlobeView.js';
    import { TerrainView } from './j/TerrainView.js';
    import { CommunicationController } from './j/CommunicationController.js';
    import { ViewController } from './j/ViewController.js';
    import { ChatClient } from './j/ChatClient.js';
    import { RegionInfoPopup } from './j/Region.js';
    import { ButtonPushable } from './j/ButtonPushable.js';
    import { GeoApiClient } from './j/GeoApiClient.js';
    import { apiURL } from './j/GeoApiClient.js';

    const CONFIG = <?=getJsonConfig(); ?>;
    CONFIG['is_mobile_ios'] = /Mobi|Android/i.test(navigator.userAgent);
    CONFIG['is_mobile_ios'] = /iPhone|iPad|iPod/i.test(navigator.userAgent);
    CONFIG['is_mobile_android'] = /Android/i.test(navigator.userAgent);
    CONFIG['is_mobile'] = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
    if (CONFIG['is_mobile']) {
       // document.body.classList.add('large-button-mode');
    }
    const user_id = CONFIG['user_id'];
    window.CONFIG = CONFIG;
    window.user_id = user_id;
    
    // --- Global variables ---
    let cc = null; // communication controller
    let viewController = null; // view controller
    let g_chatClient = null; // chat client
    let isModalActive = false;
    let isLandscapeMode = false; // state machine to help changing the view mode
 
    // --- User markers ---
    let userMarkers = [];
    let starState = { starTheta : 0, inclination : 0 };

    // --- API ---
    const geoApiClient = new GeoApiClient(null); // API client, construct without cc yet

    function resetViewport() {
        let scalefactor=1;
        if (CONFIG['is_mobile']) {
            if (CONFIG['is_mobile_ios']) {
                scalefactor=1;
            }else if (CONFIG['is_mobile_android']) {
                scalefactor=0.3;
            }

            let viewport = document.querySelector('meta[name="viewport"]');
            if (viewport) {
                viewport.content = 'width=device-width, initial-scale='+ scalefactor +', maximum-scale=2.0, user-scalable=0';
                setTimeout(() => {
                    viewport.content = 'width=device-width, initial-scale='+ scalefactor;
                }, 300);
            }
        }
        /*
        window.alert('sf:'+scalefactor+''+
            "is_mobile: " + CONFIG['is_mobile'] + "\n" +
            "is_mobile_ios: " + CONFIG['is_mobile_ios'] + "\n" +
            "is_mobile_android: " + CONFIG['is_mobile_android']
        );*/
        camera.aspect = window.innerWidth / window.innerHeight;
        camera.updateProjectionMatrix();
        renderer.setSize(window.innerWidth, window.innerHeight);
        if (CONFIG['is_monbile_ios']) {
            // --- EXTRA: small scroll trick for iPhone Safari ---
            setTimeout(() => {
                window.scrollTo(0, 1); // picit le
                window.scrollTo(0, 0); // vissza
            }, 400);
        }
    }
    
    // --- Screen resolution and texture preset ---

    const resolution_preset = CONFIG['map_texture_resolution']['preset'];
    const MAP_TEXTURE_WIDTH = CONFIG['map_texture_resolution']['width'];
    const MAP_TEXTURE_HEIGHT = CONFIG['map_texture_resolution']['height'];
    const dynamic_biome = "/geoapi/biome?width="+MAP_TEXTURE_WIDTH+"&height="+MAP_TEXTURE_HEIGHT;
    const dynamic_elevation = "/geoapi/elevation?width="+MAP_TEXTURE_WIDTH+"&height="+MAP_TEXTURE_HEIGHT;
    const dynamic_clouds = "/geoapi/clouds?width="+MAP_TEXTURE_WIDTH+"&height="+MAP_TEXTURE_HEIGHT;
    const file_biome = "m/biome.png";
    const file_elevation = "m/elevation.png";
    const file_clouds = "m/clouds.png";

    // --- Region lights support ---
    let regions = new Set();
    let regionLights = null;
    let clock = new THREE.Clock();

    // --- Hover globals ---
    let hoverMouseX = 0;
    let hoverMouseY = 0;
    
    const inclination = THREE.MathUtils.degToRad(23.4);
    const canvas = document.getElementById('globe');
    const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    renderer.setSize(window.innerWidth, window.innerHeight);

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
    camera.position.z = 3;
    camera.position.x = 0;    const controls = new OrbitControls(camera, renderer.domElement);
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
    
    // let currentView = null;
    let globView = new GlobeView(scene, camera, light, globalambient, controls, renderer);
    let terrainView = new TerrainView(scene, camera, light, globalambient, controls, renderer);
    viewController = new ViewController(globView, terrainView);
    function updateCrosshairFieldsUI(lat,lon,alt){
        document.getElementById("latVal").textContent = Number(lat).toFixed(2);
        document.getElementById("lonVal").textContent = Number(lon).toFixed(2);
        document.getElementById("altVal").textContent = Number(alt).toFixed(2);
    }
    function updateCrosshairViews(lat,lon,alt){
        if (lon > 180) lon -= 360;
        if (lon < -180) lon += 360;
        terrainView.updateCrosshair(lat, -lon+180, alt); //todo: fix needed
        globView.updateCrosshair(lat, lon, alt);
        updateCrosshairFieldsUI(lat,lon, alt);
    }
  function updateCrosshair(lat, lon, alt) {
      updateCrosshairViews(lat, lon, alt);
      const payload = { lat, lon, alt };

      geoApiClient.request("test", { task: "update_user_pos" }, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(payload)
      }).catch(e => console.error("Position update error:", e));
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
/*
    function switchView(newView) {
        let currentView = viewController.getCurrentView();
        if (currentView) {
            currentView.hide();
        }
        viewController.switchView(newView);
        currentView = viewController.getCurrentView();
        currentView.init(scene, camera);
        currentView.show();
        console.log("Current view:", currentView.name);
    }
  */  
    // --- Region lights (city lights) logic ---
    async function loadAllRegions() {
        const res = await geo_fetch('regions',{});
        regions = await res.json();
        globView.setRegions(regions);
        globView.renderRegionLights();
        //globView.createAtmosphere(); //delayed for now
    }
    function isTerrainViewActive() {
        return viewController.getCurrentView()?.name === "terrain";
    }
    function animate() {
        requestAnimationFrame(animate);
        controls.update();
        let isLandscapeMode = false;
        let currentView = viewController.getCurrentView();
        if (currentView) {
            isLandscapeMode = isTerrainViewActive();
            const delta = clock.getDelta()
            currentView.update(delta);
            currentView.updateRegionLights();
        }
        renderer.render(scene, camera);
        const distance = camera.position.length();
        if (!isLandscapeMode  && distance < 1.3) {
            const { lat, lon } = getGlobeViewLatLon(camera);
            viewController.switchView(terrainView);
            positionTerrainCamera(lat, lon);
        } else if (isLandscapeMode && distance > 3) {
            viewController.switchView(globView);
        }
    }

let pushableClouds = null;
    // UI options
    function updateUIOptionStar(){
        //const starVisible = light.visible = !light.visible;
        const visible = viewController.getOption('star');
        light.visible = visible;
        globalambient.visible = !visible;
        const starBtn = document.getElementById('starButton');
        const starCheckbox = document.getElementById('starCheckbox');
        if (starBtn) starBtn.textContent = visible ? '🌒' : '🌞';
        if (starCheckbox) starCheckbox.checked = visible;
    }
    function updateUIOptionClouds(){
        const visible=viewController.getOption('clouds');
        const cloudsCheckbox = document.getElementById('cloudsCheckbox');
        if (cloudsCheckbox) cloudsCheckbox.checked = visible;
        pushableClouds.setPushed(visible); //toggle button handled by this class
        pushableClouds.updateUI();
    }
    function updateUIOptionGraticule(){
        const visible=viewController.getOption('graticule');
        const graticuleCheckbox = document.getElementById('graticuleCheckbox');
        if (graticuleCheckbox) graticuleCheckbox.checked = visible;
    }
    function updateUIOptionSatellites(){
        const visible = viewController.getOption('satellites');
        const satellitesCheckbox = document.getElementById('satellitesCheckbox');
        if (satellitesCheckbox) satellitesCheckbox.checked = visible;
    }
    function onOptionChangeCb(optionId, value, previous)
    {
        switch(optionId) {
            case 'star':
                updateUIOptionStar();
                break;
            case 'clouds':
                updateUIOptionClouds();
                break;
            case 'satellites':
                updateUIOptionSatellites();
                break;
            case 'graticule':
                updateUIOptionGraticule();
                break;
            case 'atmosphere':
                document.getElementById('atmosphereCheckbox').checked = value;
                break;
        }
    }
    function toggleOption(optionId){
        viewController.toggleOption(optionId)
    }
    

    // UI modal panels
    let lastModalId =null;
    function closeModal() {
        toggleModal(lastModalId);
    }
    function onEnterModal(modalId) {
        switch (modalId) {
            case 'mapModal': loadMap(); break;
            case 'userinfoModal': loadUserStats(); break;
        }
    }
    function onLeaveModal(modalId) {
    }
    function toggleModal(modalId) {
        const modal = document.getElementById(modalId);
        if (!modal) return false;
        lastModalId = modalId;
        const isVisible = modal.classList.contains('visible');
        modal.classList.toggle('visible');
        modal.classList.toggle('hidden');
        // Ensure modalOverlay is display:flex when visible
        if (modal.classList.contains('visible')) {
            modal.style.display = 'flex';
        } else {
            modal.style.display = 'none';
        }
        isModalActive = !isVisible;
        if (isModalActive) {
            onEnterModal(modalId);
        } else {
            onLeaveModal(modalId);
        }
        return !isVisible;
    }
    function savePreferences() {
        // TODO: write the serverside json POST handler
    }
    function handleServerMessage(data) {
        if (!data.type) return;
        switch (data.type) {
            case 'user_data':
                const { user_id: uid, lat, lon, alt, version } = data;
                if (!uid) {
                    console.log("Invalid user_data message");
                    return;
                }
                let found = false;
                for (let marker of userMarkers) {
                    if (marker.id === uid) {
                        marker.lat = lat;
                        marker.lon = lon;
                        marker.alt = alt;
                        found = true;
                        console.log("Updating user:", uid, lat, lon, alt);
                        break;
                    }
                }
                if (!found) {
                    userMarkers.push({ id: uid, lat, lon, alt, version });
                    console.log("New user:", uid, lat, lon, alt, version);
                }
                if (uid == user_id) {
                    const previousCrosshair = viewController.getCrosshair();
                    if (previousCrosshair == null) {
                        // just initialize crosshair
                        viewController.setCrosshair(lat, lon, alt);
                        updateCrosshairFieldsUI(lat, lon, alt);
                    }
                }
                const currentView = viewController.getCurrentView();
                if (currentView) currentView.updateUserMarkers(userMarkers, user_id);
                break;

            case 'chat_message': g_chatClient.handleServerMessage(data); break;
            default:
                console.log("Unhandled server message:", data);
                break;
        }
    }
    async function loadMap() {
        try {
            const lat = document.getElementById("latVal").textContent;
            const lon = document.getElementById("lonVal").textContent;

            const url = `/geoapi/localmap?lat_min=${lat}&lon_min=${lon}&width=360&height=360&radius=10`;
            const img = document.createElement('img');
            img.src = url;
            img.alt = "Local map";
            img.style.width = "100%";
            img.style.maxWidth = "512px";
            img.style.display = "block";
            img.style.margin = "0 auto";
            const mapDiv = document.getElementById("mapDiv");
            mapDiv.innerHTML = ""; // clear previous content
            mapDiv.appendChild(img);

            // --- Overlay city markers ---
            const latNum = parseFloat(lat);
            const lonNum = parseFloat(lon);
            const radius = 8;
            const lat_min = latNum - radius;
            const lat_max = latNum + radius;
            const lon_min = lonNum - radius;
            const lon_max = lonNum + radius;

            const mapOverlay = document.createElement('div');
            mapOverlay.style.position = 'absolute';
            mapOverlay.style.top = '0';
            mapOverlay.style.left = '0';
            mapOverlay.style.width = '100%';
            mapOverlay.style.height = '100%';
            mapOverlay.style.pointerEvents = 'none';
            mapOverlay.style.zIndex = '2';
            mapOverlay.id = 'mapOverlay';

            mapDiv.style.position = 'relative';
            mapDiv.appendChild(mapOverlay);

            img.onload = () => {
                for (const key in regions) {
                    const [klat, klon] = key.split(',').map(Number);
                    if (klat < lat_min || klat > lat_max || klon < lon_min || klon > lon_max) continue;

                    // Apply a 90° longitudinal shift to match the globe's visual rotation
                    const adjustedLon = ((360-klon  ) % 360);  // Rotate 90° eastward
                    const adjustedLonMin = ((lon_min + 90 + 360) % 360);
                    const x = ((adjustedLon - adjustedLonMin) / (2 * radius)) * img.width;
                    const y = ((lat_max - klat) / (2 * radius)) * img.height;

                    const dot = document.createElement('div');
                    dot.style.position = 'absolute';
                    dot.style.width = '6px';
                    dot.style.height = '6px';
                    dot.style.background = 'red';
                    dot.style.borderRadius = '50%';
                    dot.style.left = `${x - 3}px`;
                    dot.style.top = `${y - 3}px`;
                    mapOverlay.appendChild(dot);
                    if (regions[key]?.name) {
                        const label = document.createElement('div');
                        label.textContent = regions[key].name;
                        label.style.position = 'absolute';
                        label.style.left = `${x + 4}px`;
                        label.style.top = `${y - 6}px`;
                        label.style.fontSize = '10px';
                        label.style.color = 'white';
                        label.style.pointerEvents = 'none';
                        mapOverlay.appendChild(label);
                    }
                }
            };
        } catch (e) {
            console.error("Failed to load map:", e);
        }
    }
    async function loadUserStats() {
        try {
            const res = await geo_fetch('user',{});
            if (!res.ok) throw new Error("Request failed");
            const data = await res.json();

            // Alap mezők frissítése
            document.getElementById("money").textContent = (data.resources.find(r => r.id == 1)?.quantity || 0).toFixed(0);
            document.getElementById("cities").textContent = data.region_count || 0;
            document.getElementById("population").textContent = data.workers || 0;
            document.getElementById("soldiers").textContent = data.soldiers || 0;

            // Resources táblázat
            const resourceTable = document.getElementById("resourceTable");
            let html = "<table style='width:100%;color:white;border-collapse:collapse'><tr><th style='text-align:left;'>Resource</th><th style='text-align:right;'>Quantity</th></tr>";
            for (const res of data.resources) {
                html += `<tr><td>${res.name}</td><td style='text-align:right;'>${parseFloat(res.quantity).toFixed(0)}</td></tr>`;
            }
            html += "</table>";
            resourceTable.innerHTML = html;

            // --- TRADE ORDERS TABLE WITH INLINE EDIT/ADD/DELETE ---
            const orderTable = document.getElementById("orderTable");

            let [resList, regList, orders] = await Promise.all([
                geo_fetch('test',{task:'resources'}).then(r => r.json()),
                geo_fetch('test',{task:'regions'}).then(r => r.json()),
                geo_fetch('test',{task:'trade_orders'}).then(r => r.json())
            ]);

            // Filter orders to prevent duplicates within region-resource pairs and exclude resource id 1 (Money)
            const seenRegionResources = new Set();
            orders = orders.filter(o => {
                if (o.resource_id == 1) return false;
                const key = `${o.region_id}_${o.resource_id}`;
                if (seenRegionResources.has(key)) return false;
                seenRegionResources.add(key);
                return true;
            });

            const resourceMap = Object.fromEntries(resList.map(r => [r.id, r.name]));
            const regionMap = Object.fromEntries(regList.map(r => [r.id, r.name]));

            // Filter out resource with id = 1 (Money) from options
            const resOptions = resList
                .filter(r => r.id != 1)
                .map(r => `<option value="${r.id}">${r.name}</option>`)
                .join('');
            const regOptions = regList.map(r => `<option value="${r.id}">${r.name}</option>`).join('');
            const modeOptions = `<option value="1">Buy</option><option value="2">Sell</option><option value="3">Both</option>`;

            function rowHtml(o = {}) {
                return `<tr data-id="${o.id ?? ''}">
                    <td><select class="region">${regOptions}</select></td>
                    <td><select class="resource">${resOptions}</select></td>
                    <td><select class="mode">${modeOptions}</select></td>
                    <td><input type="number" class="target" value="${o.target_quantity ?? ''}"></td>
                    <td><input type="number" class="buy" value="${o.max_buy_price ?? ''}"></td>
                    <td><input type="number" class="sell" value="${o.min_sell_price ?? ''}"></td>
                    <td><input type="number" class="priority" value="${o.priority ?? 1}"></td>
                    <td><div class=smallButtonLine>
                        <button class=small onclick="saveOrder(this)">💾</button>
                        <button class=small onclick="deleteOrder(this)">🗑</button>
                        </div>
                    </td>
                </tr>`;
            }

            let orderHtml = "<table style='width:100%;color:white;border-collapse:collapse'>";
            orderHtml += "<tr><th>Region</th><th>Resource</th><th>Mode</th><th>Target</th><th>Buy</th><th>Sell</th><th>Prio</th><th></th></tr>";
            for (const o of orders) orderHtml += rowHtml(o);
            orderHtml += rowHtml(); // új üres sor
            orderHtml += "</table>";
            orderTable.innerHTML = orderHtml;

            // Set dropdown values after DOM set
            const rows = orderTable.querySelectorAll("tr[data-id]");
            orders.forEach((o, i) => {
                const tr = rows[i];
                tr.querySelector(".region").value = o.region_id;
                tr.querySelector(".resource").value = o.resource_id;
                tr.querySelector(".mode").value = o.mode;
            });

            window.saveOrder = async (btn) => {
                const tr = btn.closest("tr");
                const id = tr.dataset.id;
                const payload = {
                    region_id: parseInt(tr.querySelector(".region").value),
                    resource_id: parseInt(tr.querySelector(".resource").value),
                    mode: parseInt(tr.querySelector(".mode").value),
                    target_quantity: parseFloat(tr.querySelector(".target").value),
                    max_buy_price: parseFloat(tr.querySelector(".buy").value),
                    min_sell_price: parseFloat(tr.querySelector(".sell").value),
                    priority: parseInt(tr.querySelector(".priority").value)
                };
                // Prevent inserting a new order with the same region-resource pair
                // Only check duplicates if this is a new order (no id)
                if (!id) {
                    const regionId = parseInt(tr.querySelector(".region").value);
                    const resourceId = parseInt(tr.querySelector(".resource").value);
                    let duplicate = false;
                    document.querySelectorAll("#orderTable tr[data-id]").forEach(row => {
                        if (row === tr) return;
                        const rid = parseInt(row.querySelector(".resource").value);
                        const rgid = parseInt(row.querySelector(".region").value);
                        if (rid === resourceId && rgid === regionId) {
                            duplicate = true;
                        }
                    });
                    if (duplicate) {
                        alert("This region already has a trade rule for this resource.");
                        return;
                    }
                }
                // If existing order, include id
                if (id) payload.id = id;
                const res = await geo_fetch('test',{task:'add_order'}, {
                    method: "POST",
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });
                if (res.ok) loadUserStats();
            };

            window.deleteOrder = async (btn) => {
                const tr = btn.closest("tr");
                const id = tr.dataset.id;
                // Only delete if id is present (do not try to delete blank row)
                if (!id) return;
                const res = await fetch('test',{task:'delete_order', id} );
                if (res.ok) loadUserStats();
            };

        } catch (e) {
            console.error("Failed to load user stats:", e);
        }
    }
    // Global functions
    window.resetViewport = function() {
        resetViewport();
    }
    window.toggleModal = function(m){
        toggleModal(m);
    }
    window.toggleOption = function(o) {
        toggleOption(o);
    };
    window.savePreferences = function() {
        savePreferences();
    }

    function onActivatePoint(event) {
        const mouse = new THREE.Vector2();
        mouse.x = (event.clientX / window.innerWidth) * 2 - 1;
        mouse.y = - (event.clientY / window.innerHeight) * 2 + 1;
        raycaster.setFromCamera(mouse, camera);
        const currentView = viewController.getCurrentView();
        if (!currentView) return;
        currentView.handleDoubleClick(raycaster);
    }
    function onDoubleClick(event) {
        onActivatePoint(event);
    }
  

    // --- Set up onRegionHover callback for both views ---
    let regionPopup = null;
    function onRegionHover(region) {
        if (!regionPopup) return;
        if (region) {
            regionPopup.show(region, hoverMouseX, hoverMouseY);
            regionPopup.updateLine(hoverMouseX, hoverMouseY, region.lat, region.lon, region.data.e ?? 1.0);
        } else {
            regionPopup.hide();
        }
    }
    function onRegionClick(region) {
        if (region) {
            // TODO: handle region click later
        }
    }

    // Debounced mousemove handler for raycaster update
    let _debounceMoveTimer = null;
    function onMouseMove(event) {
        hoverMouseX = event.clientX;
        hoverMouseY = event.clientY;
        if (_debounceMoveTimer) clearTimeout(_debounceMoveTimer);
        _debounceMoveTimer = setTimeout(performHoverCheck, 120);
    }

    function performHoverCheck() {
        mouse.x = (hoverMouseX / window.innerWidth) * 2 - 1;
        mouse.y = -(hoverMouseY / window.innerHeight) * 2 + 1;
        raycaster.setFromCamera(mouse, camera);
        const currentView = viewController.getCurrentView();
        if (!currentView || !regions) return;
        currentView.handleHover(raycaster);
    }

    let lastTouch = 0;
    function onTouchEnd(e) {
        if (e.changedTouches.length === 0) return;
        const touch = e.changedTouches[0];
        const now = Date.now();
        if (now - lastTouch < 300) {
            // BUGBUG: after dblclick, we shall forget the click due toue multi-touch thingy...
            const rect = renderer.domElement.getBoundingClientRect();
            mouse.x = ((touch.clientX - rect.left) / rect.width) * 2 - 1;
            mouse.y = -((touch.clientY - rect.top) / rect.height) * 2 + 1;
            onActivatePoint(mouse);
        }
        lastTouch = now;
    }
    function setProgress(percentage, text){
        const progress = document.getElementById("loadingProgress");
        const loadingText = document.getElementById("loadingText");
        if ((percentage>0)&&(percentage<100)){
            document.getElementById("loading").style.display = "flex";
        }
        progress.value=percentage;
        loadingText.innerHTML = text;
        console.log(text);
        if (percentage >= 100){
            document.getElementById("loading").style.display = "none";
        }
    }
    async function preload1() {
        const progressmax = 50; // this is the max value of the progress bar for the actual pass phase
        let preload1_tasks = [
            { name: "Biome", dynamicUrl: dynamic_biome, fallbackUrl: file_biome, texture: null, retriesLeft: 3, completed: false },
            { name: "Elevation", dynamicUrl: dynamic_elevation, fallbackUrl: file_elevation, texture: null, retriesLeft: 3, completed: false },
            { name: "Clouds", dynamicUrl: dynamic_clouds, fallbackUrl: file_clouds, texture: null, retriesLeft: 3, completed: false }
        ];
        let completed = 0;
        const total = preload1_tasks.length;

        async function loadTexture(url) {
            return new Promise((resolve, reject) => {
                loader.load(url, resolve, undefined, reject);
            });
        }

        while (preload1_tasks.some(task => !task.completed)) {
            for (const task of preload1_tasks) {
                if (task.completed) continue;
                try {
                    setProgress((completed / total) * progressmax, `${task.name} loading...`);
                    task.texture = await loadTexture(task.dynamicUrl);
                    task.completed = true;
                    completed++;
                    setProgress((completed / total) * progressmax, `${task.name} loaded.`);
                } catch (e) {
                    task.retriesLeft--;
                    if (task.retriesLeft <= 0) {
                        try {
                            setProgress((completed / total) * progressmax, `${task.name} fallback loading...`);
                            task.texture = await loadTexture(task.fallbackUrl);
                            task.completed = true;
                            completed++;
                            setProgress((completed / total) * progressmax, `${task.name} fallback loaded.`);
                        } catch (fallbackErr) {
                            console.error(`Fallback also failed for ${task.name}`);
                            task.completed = true; // hogy ne maradjon végtelen ciklusban
                        }
                    }
                }
            }
        }

        biomeTexture = preload1_tasks.find(t => t.name === "Biome")?.texture;
        elevationTexture = preload1_tasks.find(t => t.name === "Elevation")?.texture;
        cloudTexture = preload1_tasks.find(t => t.name === "Clouds")?.texture;

        const textures = [biomeTexture, elevationTexture, cloudTexture];
        for (const tex of textures) {
            if (tex) {
                tex.magFilter = THREE.LinearFilter;
                tex.minFilter = THREE.LinearMipMapLinearFilter;
                tex.generateMipmaps = true;
                const maxAnisotropy = renderer.capabilities.getMaxAnisotropy();
                tex.anisotropy = Math.min(4, maxAnisotropy);
            }
        }
    }
    async function preload2(){
        setProgress(50, "Setup Terrain View");
        terrainView.setStarState(starState);
        terrainView.setBiomTexture(biomeTexture);
        terrainView.setElevTexture(elevationTexture);
        terrainView.setCloudTexture(cloudTexture);
        await terrainView.init();
        terrainView.hide();
        setProgress(60, "Setup Globe View");
        globView.setStarState(starState);
        globView.setBiomTexture(biomeTexture);
        globView.setElevTexture(elevationTexture);
        globView.setCloudTexture(cloudTexture);
        await globView.init(scene, camera);
    }

  
    // --- Fetch current user position and update crosshair before showing UI ---
    /*
    try {
        const res = await fetch("test.php?task=get_user_pos");
        const pos = await res.json();
        if (pos && pos.lat !== undefined && pos.lon !== undefined && pos.alt !== undefined) {
            updateCrosshair(parseFloat(pos.lat), parseFloat(pos.lon), parseFloat(pos.alt));
        }
    } catch (e) {
        console.error("Failed to load user position:", e);
    }
*/
    
    function geo_fetch(program, params, options = {}){
        return fetch(apiURL(program, params), options);
    }

    // --- User markers: fetch all users and show them as small dots ---
    async function fetchUserMarkers() {
        try {
            const res = await geo_fetch('test', { 'task' :'users_pos', 'debug': 33});
            userMarkers = await res.json();
            const currentView = viewController.getCurrentView();
            if (currentView) currentView.updateUserMarkers(userMarkers, user_id);
        } catch (e) {
            console.error("Failed to fetch user positions:", e);
        }
    }

    //---------------------------------------
    // UI UPDATE FUNCTIONS
    function handleGlobalKeyDown(event) {
        if (g_chatClient.isChatting()) return;
        if (isModalActive) {
            switch (event.key.toLowerCase()) {
                case 'escape': closeModal(); break;
            }
            return;
        }
        switch (event.key.toLowerCase()) {
            case 'enter': g_chatClient.startChat(); break;
            case 'c': toggleOption('clouds'); break;
            case 's': toggleOption('star'); break;
            case 'g': toggleOption('graticule'); break;
            case 'o': toggleModal('operationsModal'); break;
            case 'u': toggleModal('userinfoModal'); break;
            case 'h': toggleModal("helpModal"); break;
            case 'p': toggleModal('preferencesModal'); break;
            case 'm': toggleModal('mapModal'); break;
            
        }
    }
    function handleResize() {
        camera.aspect = window.innerWidth / window.innerHeight;
        camera.updateProjectionMatrix();
        renderer.setSize(window.innerWidth, window.innerHeight);
        //resetViewport();
    }

    function fixViewportHeight() {
        const vh = window.innerHeight * 0.01;
        document.documentElement.style.setProperty('--vh', `${vh}px`);
        handleResize();
    }

    function handleOnStopChat() {
        resetViewport();
    }
    function updateConnectionStatus(state) {
        const el = document.getElementById('connectionStatus');
        if (!el) return;
        switch (state.status) {
            case 'alive': el.textContent = '✅Connection Alive.'; break;
            case 'idle': el.textContent = '⚪Idle.'; break;
            case 'open': el.textContent = '🟡Socket opened.'; break;
            case 'reconnecting': el.textContent = '🔁Reconnecting.'; break;
            case 'error': el.textContent = '❌Error: ${state.error}'; break;
            case 'connecting':  el.textContent = '🔄Retry'; break;
            case 'closed': el.textContent = '🔴Connection closed.'; break;
            default: el.textContent = '⚠️Unknown state: ${state.status}'; break;
        }
    }
    //---------------------------------------
    // APPLICATION INITIALIZATION FUNCTIONS
    async function preloadAssets() {
        await new Promise(r => setTimeout(r, 3)); 
        setProgress(1, "Init preloading");
        

        await preload1(); // már meglévő texture load
        setProgress(50, "Init preloaded views");

        await preload2(); // globView és terrainView init 
    }
    async function setupViews(){
        setProgress(70, "Setup views...");
        viewController.refreshAllUI();

        document.getElementById("globe").hidden = false;
        canvas.hidden=false;

        globView.setCrosshairUpdateCallback(updateCrosshair);
        terrainView.setCrosshairUpdateCallback(updateCrosshair);
        globView.setOnRegionHover(onRegionHover);
        terrainView.setOnRegionHover(onRegionHover);
        globView.setOnRegionClick(onRegionClick);
        terrainView.setOnRegionClick(onRegionClick);
        viewController.switchView(globView);
        setProgress(80, "Views seti[ finished.");
//itt volt
        canvas.style.display = "block";
        setProgress(85, "Setup regions...");
        // Create the region popup after currentView is set
        regionPopup = new RegionInfoPopup(scene, viewController);
        // Load data from server.
        await loadAllRegions();
        const p3=CONFIG['last_known_location'];
        updateCrosshairViews(p3[0],p3[1],p3[2]);          

        setProgress(90, "Regions setup finished.");
    }

    async function initializeCommunication() {
        setProgress(90, "Init communication...");
        cc = new CommunicationController(
            `${window.location.protocol === "https:" ? "wss" : "ws"}://${window.location.hostname}/ws/`
        );
        cc.onMessage(handleServerMessage);
        cc.onClose(() => {
            const el = document.getElementById('connectionStatus');
            //if (el) el.textContent = '❌Connection closed.';
        });
        cc.onCommunicationStateChanged(updateConnectionStatus);
        geoApiClient.setCommnicationController(cc);
        setProgress(90, "Communication init finished.");
        g_chatClient = new ChatClient(cc, camera, user_id);
        g_chatClient.setOnStopChat(handleOnStopChat);
        g_chatClient.init();
        g_chatClient.setUserId(user_id);
    }
    

fixViewportHeight();


    async function initializeUI(){
        setProgress(90, "Init UI...");
        //Checkboxes
<?php
         for($i=0; $i < count($useroptnames); $i++){
            $id = $useroptnames[$i];
            echo "\t\tdocument.getElementById('".$id."Checkbox').addEventListener('change', () => {toggleOption('$id');} );\n";
         }
?>

        g_chatClient.initializeUI();

        window.addEventListener('keydown', handleGlobalKeyDown);
        //window.addEventListener('resize', handleResize);
        window.addEventListener('resize', fixViewportHeight);
        window.addEventListener('orientationchange', fixViewportHeight);
        
        renderer.domElement.addEventListener("dblclick", onDoubleClick);
        renderer.domElement.addEventListener("touchend", onTouchEnd);
        renderer.domElement.addEventListener("mousemove", onMouseMove);
        pushableClouds = new ButtonPushable("cloudsButton", true, ["☁️", "☁️"]);
        pushableClouds.initializeUI();
        camera.position.z = 3;
        
        viewController.setOnOptionChangeCallback(onOptionChangeCb);
        viewController.refreshAllUI();
        setProgress(95, "UI initialized.");
    }
    function startMainLoop() {
        setProgress(100, "Start...");
        //resetViewport();
        animate();
        setInterval(fetchUserMarkers, 60000);
        fetchUserMarkers();
    }
    async function startApp() {
        await preloadAssets();
        await setupViews();
        await initializeCommunication();
        await initializeUI();
        startMainLoop();
    }
    startApp();
</script>
<div id="helpModal" class="modalOverlay hidden">
     <div class="panelBox">
        <div class="panelTitle">Help
            <button onclick="toggleModal('helpModal')"  class="panelCloseButton">❌</button>
        </div>
        <div class="panelInnerBox">
            <p>
            This is a planetary simulation demo.<br>
            - <b>Click</b> or <b>drag</b> to rotate the globe.<br>
            - Use <b>mouse wheel</b> to zoom in/out.<br>
            - Zoom closer to enter airplane mode, zoom out to enter globe mode.<br>
            - <b>Double click</b> to select a point on the globe or the map.<br>
            - Press <b>S</b> to toggle the star.<br>
            - Press <b>C</b> to toggle clouds.<br>
            - Press <b>G</b> to toggle graticule.<br>
            <br>
            - Press <b>U</b> to toggle user info panel<br>
            - Press <b>P</b> to toggle preferences<br>
            - Press <b>M</b> to toggle map.<br>
            - Press <b>H</b> for this help.
            </p>
        </div>
    </div>
</div>
<div id="userinfoModal" class="modalOverlay hidden">
    <div class="panelBox">
        <div class="panelTitle">User info
            <button onclick="toggleModal('userinfoModal')" class="panelCloseButton">❌</button>
        </div>
        <div class="panelInnerBox">
            Your are logged in as <span id="userNick"><?php echo $user['nick']; ?></span>.<br>
            <a href="user.php">User admin</a> | <a href="logout.php">Logout</a><br>
            Money: <span id="money"><?php echo $user['money']; ?></span>
            Cities: <span id="cities"><?php echo $user['cities']; ?></span>
            Population: <span id="population"><?php echo $user['population']; ?></span>
            Soldiers: <span id="soldiers"><?php echo $user['soldiers']; ?></span>
            <div id=resourceTable style="margin-top: 10px;"></div>
            <div id="orderTable" style="margin-top: 10px;"></div>
        </div>
    </div>
</div>
<div id="preferencesModal" class="modalOverlay hidden">
    <div class="panelBox">
        <div class="panelTitle">Preferences
        <button onclick="toggleModal('preferencesModal')" class="panelCloseButton">❌</button>
        </div>
        <div class="panelInnerBox">
            <p>Visual settings</p>
            <p class="panelTexts">
                <table class="panelTexts">
<?php
for($i=0; $i<count($useroptnames); $i++){
    $label=$useroptlabels[$i];
    $id=$useroptnames[$i];
    echo "\t\t\t              <tr><td>$label:</td><td><input type='checkbox' id='{$id}Checkbox' checked></input></td></trd>\n";
}
?>
                </table>
            </p>
            <button onclick="savePreferences()" class="panelButton">SAVE</button>
        </div>
    </div>
</div>
<div id="mapModal" class="modalOverlay hidden">
     <div class="panelBox">
        <div class="panelTitle">Map
            <button onclick="toggleModal('mapModal')" class="panelCloseButton">❌</button>
        </div>
        <div class="panelInnerBox">
            <div id=mapDiv style="margin-top: 10px;"></div>
            <small>Esc to exit and press <b>m</b> to toggle map.</small>
        </div>
    </div>
</div>
<div id="operationsModal" class="modalOverlay hidden">
     <div class="panelBox">
        <div class="panelTitle">Operations
            <button onclick="toggleModal('operationsModal')" class="panelCloseButton">❌</button>
        </div>
        <div class="panelInnerBox">
            <div class=smallButtonLine>
                <button onclick="toggleOption('graticule')" class="smallButton">Grt</button>
                <button onclick="toggleOption('satellites')" class="smallButton">Sat</button>
            </div>
            <small>Esc to exit and press <b>o</b> to toggle map.</small>
        </div>
    </div>
</div>
<div id="exitModal" class="modalOverlay">
     <div class="panelBox">
        <div class="panelTitle">Exit to...
            <button onclick="toggleModal('exitModal')" class="panelCloseButton">❌</button>
        </div>
        <div class="panelInnerBox">
            <button onclick="window.location.href='index.php'">Menu</button>
            <button onclick="window.location.href='user.php'">User administration</button>
            <button onclick="window.location.href='terrain.php'">Terrain</button>
            <small>these options all are external links.</small>
        </div>
    </div>
</div>

       
<button id="focusTrap" style="position:absolute; left:-9999px; width:1px; height:1px; opacity:0;" tabindex="0"></button>

<div id="ui-container">
    <div id="ui-top-bar"> GEO a game  (This is the top bar...)
    <input id="focusTrapInput" style="position:absolute; left:-9999px; width:1px; height:1px; opacity:0;" tabindex="0" type="text">
    </div>
    <div id="ui-side-bar-left">
        <button class="small" onclick="toggleOption('clouds')">☁️</button>
        <button class="small" onclick="toggleOption('star')">🌟</button>
        <button class="small" onclick="toggleOption('satellites')">🛰️</button>
        <button class="small" onclick="toggleOption('graticule')">📏</button>
        <button class="small" id="chatButton">💬</button>
    </div>
    <div id="ui-center-content">
    <canvas id="globe" tabindex="0"></canvas>
    </div>
    <div id="ui-side-bar-right">
        <div id="ui-menu">
            <?php if ($logged_in) { ?>
                <button onclick="toggleModal('userinfoModal')">User:<?php echo $user['nick']; ?></button>
            <?php } else { ?>
                <button onclick="window.location.href='login.php'">Login</button>
            <?php } ?>
            <button onclick="toggleModal('preferencesModal')">Preferences</button>
            <div class="smallButtonLine">
                <button id="cloudsButton" class=small onclick="toggleOption('clouds')"><span class=icon>☁️</span></button>
                <button id="starButton" class=small onclick="toggleOption('star')">🌒</button>
            </div>
            <button onclick="toggleModal('mapModal')">Map</button>
            <button onclick="toggleModal('operationsModal')">Operation</button>
            <button onclick="toggleModal('helpModal')">Help</button>
            <button onclick="toggleModal('exitModal')">Exit</button>
        </div>
    </div>
    <div id="ui-bottom-bar">
        <span id="connectionStatus" style="margin-right: 8px; color: yellow;">❌</span>
        <span id="infoPanel" style="margin-right: 8px; margin-top: 8px; font-size: 12px;">
        LAT: <span id="latVal">-</span>, LON: <span id="lonVal">-</span>, ALT: <span id="altVal">-</span> 
        </span>
    </div>  
    </div>
</div>

<div id="chatPanel" class="modalOverlay hidden">
<div id="chatLog" >This is the log...</div>
<div id="chatInputContainer">
    <div id=chatInputSubContainer>💬<input type="text" id="chatInput" placeholder="Type message and press Enter..."></div>
</div>
</body>
</html>
