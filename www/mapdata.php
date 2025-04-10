<?php
header('Content-Type: application/json');

$dbFile = '../var/mapdata.sqlite';

if (!file_exists($dbFile)) {
    http_response_code(500);
    echo json_encode(['error' => 'Adatbázis nem található']);
    exit;
}

$lat_min = isset($_GET['lat_min']) ? floatval($_GET['lat_min']) : -10;
$lat_max = isset($_GET['lat_max']) ? floatval($_GET['lat_max']) : 90;
$lon_min = isset($_GET['lon_min']) ? floatval($_GET['lon_min']) : -80;
$lon_max = isset($_GET['lon_max']) ? floatval($_GET['lon_max']) : 80;


try {
    $db = new PDO('sqlite:' . $dbFile);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

    $stmt = $db->prepare('
        SELECT lat, lon, r, g, b, elevation
        FROM mapdata
        WHERE lat BETWEEN :lat_min AND :lat_max
          AND lon BETWEEN :lon_min AND :lon_max
        LIMIT 50000
    ');
    $stmt->execute([
        ':lat_min' => $lat_min,
        ':lat_max' => $lat_max,
        ':lon_min' => $lon_min,
        ':lon_max' => $lon_max,
    ]);
    $result = [];

    while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
        $key = number_format($row['lat'], 2) . ',' . number_format($row['lon'], 2);
        $result[$key] = [
            'r' => round($row['r'], 4),
            'g' => round($row['g'], 4),
            'b' => round($row['b'], 4),
            'e' => round($row['elevation'], 4)
        ];
    }

    echo json_encode($result);

} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Adatbázis hiba: ' . $e->getMessage()]);
}
?>
