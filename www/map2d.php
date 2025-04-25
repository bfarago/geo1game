<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Fixed Tile Map Viewer</title>
  <style>
    body { margin: 0; overflow: hidden; }
    canvas { display: block; background: #000; }
  </style>
</head>
<body>
  <canvas id="map" width="1024" height="512"></canvas>
  <script>
    const canvas = document.getElementById('map');
    const ctx = canvas.getContext('2d');

    const tileSize = 256;
    const lat = 0; // fixed latitude (equator)
    let lonOffset = 0;

    const loadedTiles = new Map();

    function tileKey(lon) {
      return `0_${lon}`;
    }

    function updateTiles() {
      const tilesOnScreen = Math.ceil(canvas.width / tileSize) + 2;
      const step = 360 / Math.ceil(canvas.width / tileSize);

      const centerLon = ((canvas.width / 2 - lonOffset) / tileSize) * step - 180;
      const lonStart = centerLon - step;
      const lonEnd = centerLon + tilesOnScreen * step;

      for (let lon = Math.floor(lonStart / step) * step; lon <= lonEnd; lon += step) {
        const normalizedLon = ((lon + 180 + 360) % 360) - 180; // wrap around
        loadTile(lat, normalizedLon);
      }
    }

    function loadTile(lat, lon) {
      const key = tileKey(lon);
      if (loadedTiles.has(key)) return;

      const img = new Image();
      const radius = 180 / Math.ceil(canvas.width / tileSize);
      img.src = `/geoapi/localmap?lat_min=${lat}&lon_min=${lon}&radius=${radius}&width=${tileSize}&height=${canvas.height}`;
      img.onload = () => {
        loadedTiles.set(key, img);
        draw();
      };
    }

    function draw() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);

      const step = 18 / Math.ceil(canvas.width / tileSize);

      for (const [key, img] of loadedTiles) {
        const [, lon] = key.split('_').map(Number);
        const tileX = ((lon + 180) / 360) * canvas.width - lonOffset;
        ctx.drawImage(img, tileX, 0, tileSize, canvas.height);
      }
    }

    canvas.addEventListener('mousedown', e => {
      let lastX = e.clientX;
      const onMove = e => {
        lonOffset += e.clientX - lastX;
        lastX = e.clientX;
        draw();
      };
      const onUp = () => {
        canvas.removeEventListener('mousemove', onMove);
        canvas.removeEventListener('mouseup', onUp);
        updateTiles();
      };
      canvas.addEventListener('mousemove', onMove);
      canvas.addEventListener('mouseup', onUp);
    });

    updateTiles();
  </script>
</body>
</html>
