<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Planet Terrain View</title>
  <style>
    html, body {
        margin: 0;
        padding: 0;
        overflow: hidden;
        background: #000;
    }
    canvas {
        display: block;
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
        width: 100%;
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
  </style>
</head>
<body>
  <canvas id="terrainCanvas"></canvas>
  <script type="module">
    import * as THREE from './j/three.module.js';
    import { OrbitControls } from './j/OrbitControls.js';

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.1, 1000);
    camera.position.set(0, 0.5, 2);
    camera.lookAt(0, 0, 0);

    const renderer = new THREE.WebGLRenderer({ canvas: document.getElementById('terrainCanvas'), antialias: true });
    renderer.setSize(window.innerWidth, window.innerHeight);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.target.set(0, 0, 0);
    controls.update();

    const light = new THREE.DirectionalLight(0xffffff, 1);
    light.position.set(10, 10, 10);
    scene.add(light);

    scene.add(new THREE.AmbientLight(0x404040));

    const loader = new THREE.TextureLoader();
    const biomeTexture = loader.load('biome.png');
    const elevationTexture = loader.load('elevation.png');

    const geometry = new THREE.PlaneGeometry(10, 10, 1024, 1024);
    const material = new THREE.MeshStandardMaterial({
      map: biomeTexture,
      displacementMap: elevationTexture,
      displacementScale: 0.5,
    });

    const terrain = new THREE.Mesh(geometry, material);
    terrain.rotation.x = -Math.PI / 2;
    scene.add(terrain);
    scene.background = new THREE.Color(0x87ceeb); // világoskék ég
    scene.fog = new THREE.Fog(0x87ceeb, 15, 50);   // horizontba olvadás

    function animate() {
      requestAnimationFrame(animate);
      renderer.render(scene, camera);
    }

    animate();

    window.addEventListener('resize', () => {
      camera.aspect = window.innerWidth / window.innerHeight;
      camera.updateProjectionMatrix();
      renderer.setSize(window.innerWidth, window.innerHeight);
    });
  </script>
  <div id="ui-menu">
  <button onclick="window.location.href='index.php'">Menu</button>
  <button onclick="window.location.href='globe2.php'">Globe</button>
</div>
</body>
</html>
