<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>ShipModel Viewer</title>
    <style>
        body { margin: 0; background-color: black; overflow: hidden; }
        canvas { display: block; }
    </style>
</head>
<body>
    <script type="module">
        import * as THREE from './j/three.module.js';
        import { OrbitControls } from './j/OrbitControls.js';

        const scene = new THREE.Scene();
        const camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.1, 1000);
        camera.position.set(2, 2, 5); // Kamera pozíciója
        camera.lookAt(new THREE.Vector3(0, 0, 0)); // Nézzen az origóra

        const renderer = new THREE.WebGLRenderer({ antialias: true });
        renderer.setSize(window.innerWidth, window.innerHeight);
        document.body.appendChild(renderer.domElement);

        const controls = new OrbitControls(camera, renderer.domElement);
        const globalambient = new THREE.AmbientLight(0xffffff, 1);
        scene.add(globalambient);
        fetch('/geoapi/shipmodel?id=1')
            .then(res => {
                if (!res.ok) throw new Error("HTTP " + res.status);
                return res.json();
            })
            .then(data => {
                const comp = data.components[0];

                const geometry = new THREE.BufferGeometry();
                const positions = [];
                const colors = [];

                const palette = comp.colors;
                const verts = comp.vertices;
                const color_indices = verts.map((_, i) => i % palette.length);

                comp.faces.forEach(face => {
                    for (let idx of face) {
                        const [x, y, z] = verts[idx];
                        const [r, g, b] = palette[color_indices[idx]];
                        positions.push(x, y, z);
                        colors.push(r / 255, g / 255, b / 255);
                    }
                });

                geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
                geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
                geometry.computeVertexNormals();

                const material = new THREE.MeshStandardMaterial({ vertexColors: true ,
                    side: THREE.DoubleSide});
                const mesh = new THREE.Mesh(geometry, material);
                scene.add(mesh);

                const light = new THREE.DirectionalLight(0xffffff, 1);
                light.position.set(5, 5, 5).normalize();
                scene.add(light);
            })
            .catch(err => console.error("Fetch error:", err));

        window.addEventListener('resize', () => {
            camera.aspect = window.innerWidth / window.innerHeight;
            camera.updateProjectionMatrix();
            renderer.setSize(window.innerWidth, window.innerHeight);
        });

        function animate() {
            requestAnimationFrame(animate);
            controls.update();
            renderer.render(scene, camera);
        }
        animate();
    </script>
</body>
</html>