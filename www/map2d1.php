<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>2D Interaktív Világtérkép</title>
  <style>
    body { margin: 0; overflow: hidden; }
    canvas { display: block; background: #000; }
  </style>
</head>
<body>
  <canvas id="map" width="2048" height="1024"></canvas>
  <script>
    const canvas = document.getElementById('map');
    const ctx = canvas.getContext('2d');
    let scale = 1.0;
    let offsetX = 0;
    let offsetY = 0;
    let dragging = false;
    let lastX, lastY;

    const chunkSize = 45;
    const loadedChunks = new Set();
    const cityList = []; // ide jönnek a városok külön fetch-ből

    // rajzolás koordináta-átszámítással
    function mapToScreen(lat, lon) {
      const x = ((lon + 180) / 360 * canvas.width) * scale + offsetX;
      const y = ((90 - lat) / 180 * canvas.height) * scale + offsetY;
      return [x, y];
    }

    function draw() {
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.setTransform(scale, 0, 0, scale, offsetX, offsetY);

      // újrarajzolás chunk adat alapján
      for (const key of loadedChunks) {
        const data = window.chunkData[key];
        for (const k in data) {
          const [lat, lon] = k.split(',').map(Number);
          const cell = data[k];
          const r = Math.floor(cell.r * 255);
          const g = Math.floor(cell.g * 255);
          const b = Math.floor(cell.b * 255);

          const [x, y] = mapToScreen(lat, lon);
          ctx.fillStyle = `rgb(${r},${g},${b})`;
          ctx.fillRect((x - offsetX) / scale, (y - offsetY) / scale, 1, 1);
        }
      }

      // városok
      for (const city of cityList) {
        const [x, y] = mapToScreen(city.lat, city.lon);
        ctx.fillStyle = '#ff8080';
        ctx.beginPath();
        ctx.arc((x - offsetX) / scale, (y - offsetY) / scale, 2, 0, 2 * Math.PI);
        ctx.fill();

        if (scale > 2.0) {
          ctx.fillStyle = '#ffffff';
          ctx.font = '10px sans-serif';
          ctx.fillText(city.name, (x - offsetX) / scale + 3, (y - offsetY) / scale - 3);
        }
      }
    }

    window.chunkData = {};
    function loadChunks() {
      const lonChunks = 360 / chunkSize;
      const latChunks = 180 / chunkSize;

      for (let i = 0; i < latChunks; i++) {
        for (let j = 0; j < lonChunks; j++) {
          const lat_min = -90 + i * chunkSize;
          const lat_max = lat_min + chunkSize;
          const lon_min = -180 + j * chunkSize;
          const lon_max = lon_min + chunkSize;

          const key = `${lat_min},${lon_min}`;
          if (loadedChunks.has(key)) continue;
          loadedChunks.add(key);

          const url = `mapdata.php?lat_min=${lat_min}&lat_max=${lat_max}&lon_min=${lon_min}&lon_max=${lon_max}`;
          fetch(url)
            .then(res => res.json())
            .then(data => {
              window.chunkData[key] = data;
              draw();
            });
        }
      }
    }

    function loadCities() {
      fetch('regions.php')
        .then(res => res.json())
        .then(cities => {
          cityList.push(...cities);
          draw();
        });
    }

    canvas.addEventListener('wheel', e => {
      const zoom = e.deltaY < 0 ? 1.1 : 0.9;
      scale *= zoom;
      offsetX = e.offsetX - (e.offsetX - offsetX) * zoom;
      offsetY = e.offsetY - (e.offsetY - offsetY) * zoom;
      draw();
    });

    canvas.addEventListener('mousedown', e => {
      dragging = true;
      lastX = e.clientX;
      lastY = e.clientY;
    });

    canvas.addEventListener('mousemove', e => {
      if (!dragging) return;
      offsetX += e.clientX - lastX;
      offsetY += e.clientY - lastY;
      lastX = e.clientX;
      lastY = e.clientY;
      draw();
    });

    canvas.addEventListener('mouseup', () => dragging = false);
    canvas.addEventListener('mouseleave', () => dragging = false);

    loadChunks();
    loadCities();
  </script>
</body>
</html>

