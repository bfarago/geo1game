<!DOCTYPE html>
<html lang="hu">
<head>
  <meta charset="UTF-8">
  <title>Világ nézetek - Split</title>
  <style>
    html, body {
      margin: 0;
      height: 100%;
      overflow: hidden;
    }
    #top, #bottom {
      width: 100%;
      border: none;
    }
    #top {
      height: 50%;
    }
    #bottom {
      height: 50%;
    }
    #dragbar {
      height: 6px;
      background: #333;
      cursor: row-resize;
    }
  </style>
</head>
<body>
  <iframe id="top" src="globe.php"></iframe>
  <div id="dragbar"></div>
  <iframe id="bottom" src="map2d.php"></iframe>

  <script>
    const dragbar = document.getElementById('dragbar');
    const topFrame = document.getElementById('top');
    const bottomFrame = document.getElementById('bottom');

    let isDragging = false;

    dragbar.addEventListener('mousedown', function(e) {
      isDragging = true;
      document.body.style.cursor = 'row-resize';
    });

    document.addEventListener('mousemove', function(e) {
      if (!isDragging) return;
      const y = e.clientY;
      const totalHeight = window.innerHeight;
      const topHeight = y;
      const bottomHeight = totalHeight - y - dragbar.offsetHeight;

      topFrame.style.height = `${topHeight}px`;
      bottomFrame.style.height = `${bottomHeight}px`;
    });

    document.addEventListener('mouseup', function() {
      isDragging = false;
      document.body.style.cursor = 'default';
    });
  </script>
</body>
</html>

