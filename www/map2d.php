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
    let cityList = new Set();

    function mapToScreen(lat, lon) {
      const x = ((lon + 180) / 360 * canvas.width) * scale + offsetX;
      const y = ((90 - lat) / 180 * canvas.height) * scale + offsetY;
      return [x, y];
    }

    function draw() {
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.setTransform(scale, 0, 0, scale, offsetX, offsetY);

      for (const key of loadedChunks) {
        const data = window.chunkData[key];
        if (!data || typeof data !== 'object') continue;
        const points = Object.keys(data).map(k => {
          const [lat, lon] = k.split(',').map(Number);
          const cell = data[k];
          const r = Math.floor(cell.r * 255);
          const g = Math.floor(cell.g * 255);
          const b = Math.floor(cell.b * 255);
          return { lat, lon, r, g, b };
        });

        points.sort((a, b) => a.lat - b.lat || a.lon - b.lon);

        for (const point of points) {
          const [x, y] = mapToScreen(point.lat, point.lon);
          ctx.fillStyle = `rgb(${point.r},${point.g},${point.b})`;
          ctx.fillRect((x - offsetX) / scale, (y - offsetY) / scale, 1, 1);
        }
      }

      for (const key in cityList) {
        const city = cityList[key];
        const [lat, lon] = key.split(',').map(Number);
        ctx.fillStyle = '#ff8080';
        ctx.beginPath();
	const [x, y] = mapToScreen(lat, lon);
        ctx.arc((x - offsetX) / scale, (y - offsetY) / scale, 2*city.p, 0, 2 * Math.PI);
        ctx.fill();

        if (scale > 3.0 || city.p >0.96) {
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.fillStyle = '#ffffff';
          ctx.font = '10px sans-serif';
          ctx.fillText(city.n, x + 3, y - 3);
          ctx.setTransform(scale, 0, 0, scale, offsetX, offsetY);
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
              if (data && typeof data === 'object') {
                window.chunkData[key] = data;
                draw();
              }
            })
  //          .catch(err => console.error('Chunk fetch error:', err));
        }
      }
    }

    function loadCities() {
      fetch('regions_chunk.php')
        .then(res => res.json())
        .then(cities => {
//          if (Array.isArray(cities)) {
//            cityList.push.apply(cityList, cities);
            cityList = cities;
            draw();
//          }
        })
//        .catch(err => console.error('City fetch error:', err));
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

