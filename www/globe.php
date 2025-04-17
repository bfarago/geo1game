<?php header('Content-Type: text/html; charset=UTF-8'); ?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Geo: Procedurális Bolygó</title>
  <style>
    html, body, canvas {
      margin: 0;
      padding: 0;
      width: 100%;
      height: 100%;
      display: block;
    }
  </style>
</head>
<body>
  <canvas id="globe"></canvas>
  <script type="module">
    import * as THREE from './j/three.module.js';
    import { OrbitControls } from './j/OrbitControls.js';
    let starTheta = 0;
    const inclination = THREE.MathUtils.degToRad(23.4);
    let atmosphere = null;

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
    const renderer = new THREE.WebGLRenderer({ canvas: document.getElementById('globe'), antialias: true });
    renderer.setSize(window.innerWidth, window.innerHeight);

    const controls = new OrbitControls(camera, renderer.domElement);
    camera.position.z = 3;
    // Ne engedje túl közel menni
    controls.minDistance = 1.2;  // sugárnál kicsit nagyobb
    controls.maxDistance = 10;   // max távolság, ahonnan nézhetsz

    // Ne engedje átbillenni
    controls.enablePan = true;
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    controls.update();

    const light = new THREE.DirectionalLight(0xffffff, 1);
    light.position.set(5, 3, 5);
    scene.add(light);
    const ambient = new THREE.AmbientLight(0x334488, 0.4);
    scene.add(ambient);

    const geometry = new THREE.SphereGeometry(1, 1024, 1024);
    const colors = new Float32Array(geometry.attributes.position.count * 3);
    geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    const material = new THREE.MeshPhongMaterial({ vertexColors: true });
    const globe = new THREE.Mesh(geometry, material);
    scene.add(globe);

    const loadedChunks = new Set();
    let regions = new Set();
    let regionLights = null;
    const chunkSize = 40.0;
    const serverResolution = 0.5;
    const cacheValidity = 60000;
    let preloadLat = -90;
    let preloadLon = -180;
    let preloadSpeed = 100;
    function loadAllRegions()
    {
        const url = `regions_chunk.php`;
        fetch(url)
          .then(res => res.json())
          .then(data => {
	    regions = data;
	    renderRegionLights();
          });
    }
    
    function preloadStepTick() {
      const lat_min = preloadLat;
      const lat_max = preloadLat + chunkSize;
      const lon_min = preloadLon;
      const lon_max = preloadLon + chunkSize;
      const key = `${lat_min},${lon_min}`;

      if (!loadedChunks.has(key)) {
        const url = `mapdata.php?lat_min=${lat_min}&lat_max=${lat_max}&lon_min=${lon_min}&lon_max=${lon_max}`;
        fetch(url)
          .then(res => res.json())
          .then(data => {
            loadedChunks.add(key);
            applyColorData(data);
          })
          .catch(err => {
            console.warn("Preload error:", key, err);
          });
      }

      // Következő csempe léptetése
      preloadLon += chunkSize;
      if (preloadLon >= 180) {
        preloadLon = -180;
        preloadLat += chunkSize;
        if (preloadLat >= 90) {
          preloadLat = -90; // újraindul a ciklus
          preloadSpeed = 1000;
        }
      }

      setTimeout(preloadStepTick, preloadSpeed);
    }

    function getVisibleLatLonBounds(camera) {
      const direction = new THREE.Vector3();
      camera.getWorldDirection(direction);
      const focusPoint = new THREE.Vector3().copy(camera.position).add(direction).normalize();
      const lat = THREE.MathUtils.radToDeg(Math.asin(focusPoint.y));
      const lon = THREE.MathUtils.radToDeg(Math.atan2(focusPoint.z, focusPoint.x));
      const half = chunkSize / 2.0;
      const latMin = Math.floor((lat - half) / chunkSize) * chunkSize;
      const latMax = latMin + chunkSize;
      const lonMin = Math.floor((lon - half) / chunkSize) * chunkSize;
      const lonMax = lonMin + chunkSize;
      return { lat_min: latMin, lat_max: latMax, lon_min: lonMin, lon_max: lonMax };
    }

function loadRegion(bounds) {
  const key = `region:${bounds.lat_min},${bounds.lon_min}`;
  const url = `regions_chunk.php?lat_min=${bounds.lat_min}&lat_max=${bounds.lat_max}` +
              `&lon_min=${bounds.lon_min}&lon_max=${bounds.lon_max}`;
  fetch(url)
    .then(res => res.json())
    .then(data => applyRegionLights(data));
}


    function loadChunkIfNeeded(bounds) {
      const key = `${bounds.lat_min},${bounds.lon_min}`;
      if (loadedChunks.has(key)) return;
      loadedChunks.add(key);

      const url = `mapdata.php?lat_min=${bounds.lat_min}&lat_max=${bounds.lat_max}` +
                  `&lon_min=${bounds.lon_min}&lon_max=${bounds.lon_max}`;
      fetch(url)
        .then(res => res.json())
        .then(data => applyColorData(data));
    }

    function applyColorData(data) {
      const pos = geometry.attributes.position;
      const col = geometry.attributes.color;
      for (let i = 0; i < pos.count; i++) {
        const vertex = new THREE.Vector3().fromBufferAttribute(pos, i);
        const lat = THREE.MathUtils.radToDeg(Math.asin(vertex.y));
        const lon = THREE.MathUtils.radToDeg(Math.atan2(vertex.z, vertex.x));
        const latDeg = Math.round(lat * 2) / 2;
        const lonDeg = Math.round(lon * 2) / 2;
        const key = `${latDeg.toFixed(2)},${lonDeg.toFixed(2)}`;
        const cell = data[key];
        if (cell) {
          const scale = 1.0 + (cell.e || 0) * 0.01;
          vertex.multiplyScalar(scale);
          pos.setXYZ(i, vertex.x, vertex.y, vertex.z);
	  //const regn= regions[key];
	  /*
	  const currentR = col.getX(i);
	  const currentG = col.getY(i);
	  const currentB = col.getZ(i);
	  const isRed = (currentR === 1 && currentG === 0 && currentB === 0);
	  */
	  const isRed = false;
	  if (!isRed) {
            col.setX(i, cell.r/255.0);
            col.setY(i, cell.g/255.0);
            col.setZ(i, cell.b/255.0);
	  }
        }
      }
      pos.needsUpdate = true;
      col.needsUpdate = true;
    }

function createAtmosphere() {
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

  atmosphere = new THREE.Mesh(geometry, material);
  scene.add(atmosphere);
}


function renderRegionLights() {
  const positions = [];
  const pollution = [];
  const colors = [];

  for (const key in regions) {
    const [lat, lon] = key.split(',').map(Number);
    const { e, p, n } = regions[key];

    const phi = THREE.MathUtils.degToRad(90.0 - lat);
    const theta = THREE.MathUtils.degToRad(lon );
    const radius = 1.0 + e * 0.021;// kissé a felszín fölé, elevációval

    const x = radius * Math.sin(phi) * Math.cos(theta);
    const y = radius * Math.cos(phi);
    const z = radius * Math.sin(phi) * Math.sin(theta);
    positions.push(x, y, z);
    pollution.push(p);
    const brightness = Math.min(1.0, p * 2.0); // skálázás
    colors.push(brightness, brightness * 0.6, 0.2); // sárgás fény
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute('pollution', new THREE.Float32BufferAttribute(pollution, 1));
  geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));

  const simpleMaterial = new THREE.PointsMaterial({
    size: 0.005,
    vertexColors: true,
    transparent: true,
    blending: THREE.AdditiveBlending,
    depthWrite: false
  });
  const vertexShader = `
varying vec3 vWorldPosition;
varying float vPollution;
attribute float pollution;
void main() {
  vPollution = pollution;
  vec4 worldPosition = modelMatrix * vec4(position, 1.0);
  vWorldPosition = normalize(worldPosition.xyz);
  vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
  gl_PointSize = 30.0 / -mvPosition.z;
  gl_Position = projectionMatrix * mvPosition;
}
  `;

  const fragmentShader = `
uniform vec3 glowColor;
uniform vec3 dayColor;
uniform vec3 lightDirection;
uniform float time;
varying vec3 vWorldPosition;
varying float vPollution;
void main() {
  vec3 viewDir = normalize(vWorldPosition);
  float dotNL = dot(viewDir, lightDirection); // < 0 → sötét oldal
  float d = length(gl_PointCoord - vec2(0.5));
  float fade = 1.0 - smoothstep(0.0, 0.5, d);
  if (dotNL > 0.0){
    gl_FragColor = vec4(dayColor, fade);
  }else{
    //float angle=dot(vWorldPosition, lightDirection) * 10.0;
    float rand=0.0; //hash(gl_FragCoord.xy);
    float flicker = 0.8 + 0.2 * sin(time * 40.0+ vPollution*10.0)*vPollution;
    gl_FragColor = vec4(glowColor, fade * flicker);
  }
}
`;

  const glowMaterial = new THREE.ShaderMaterial({
   uniforms: {
    glowColor: { value: new THREE.Color(1.0, 0.7, 0.3) },
    dayColor:  { value: new THREE.Color(0.7, 0.2, 0.2) },
    lightDirection: { value: new THREE.Vector3() },
    time: { value: 0.0 }
   },
   vertexShader,
   fragmentShader,
   transparent: true,
   depthWrite: false,
   blending: THREE.AdditiveBlending
  });
  regionLights = new THREE.Points(geometry, glowMaterial);
  scene.add(regionLights);
}
function updateRegionLightColors() {
  if (!regionLights) return;
  const nightSide = light.position.clone().normalize().negate();
  regionLights.material.uniforms.lightDirection.value.copy(light.position).normalize();
  regionLights.material.uniforms.time.value = performance.now() * 0.001;
  if (atmosphere){
    atmosphere.material.uniforms.nightSide.value.copy(nightSide);
  }
  return;

  const positions = regionLights.geometry.attributes.position;
  const colors = regionLights.geometry.attributes.color;

  const lightDir = light.position.clone().normalize();
  const time = performance.now() * 0.001;

  for (let i = 0; i < positions.count; i++) {
    const vertex = new THREE.Vector3().fromBufferAttribute(positions, i);
    const toLight = vertex.clone().normalize();

    const dot = toLight.dot(lightDir);

    let intensity = 0.1; // alap szín nappal
    if (dot < 0) {
      // éjszakai oldal → fokozatos fény + vibrálás
      const strength = Math.abs(dot); // minél szembenebb, annál sötétebb
      const flicker = 0.6 + 0.4 * Math.sin(time * 5 + i); // kis vibrálás
      intensity = Math.min(1.0, strength * flicker);
      // Szín frissítés (pl. sárgás városfény)
      colors.setX(i, intensity);
      colors.setY(i, intensity * 0.6);
      colors.setZ(i, intensity * 0.2);
    }else{
      colors.setX(i, 0.2);
      colors.setY(i, 0);
      colors.setZ(i, 0);
    }
  }

  colors.needsUpdate = true;
}

    function updateVisibleRegion() {
      const bounds = getVisibleLatLonBounds(camera);
      loadChunkIfNeeded(bounds);
    }

    //controls.addEventListener('change', updateVisibleRegion);
    //updateVisibleRegion();

    function animate() {
      requestAnimationFrame(animate);
      controls.update();
      starTheta += 0.005;
      const radius = 5;
      const x = radius * Math.cos(starTheta);
      const y = radius * Math.sin(inclination) * Math.sin(starTheta);
      const z = radius * Math.cos(inclination) * Math.sin(starTheta);
      light.position.set(x, y, z);
      light.lookAt(0, 0, 0);
      updateRegionLightColors();
      renderer.render(scene, camera);
    }

    loadAllRegions();
    createAtmosphere();

    animate();
    setTimeout(preloadStepTick, 500);
    window.addEventListener('resize', () => {
      camera.aspect = window.innerWidth / window.innerHeight;
      camera.updateProjectionMatrix();
      renderer.setSize(window.innerWidth, window.innerHeight);
    });
  </script>
</body>
</html>
