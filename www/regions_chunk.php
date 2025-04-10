<?php
header('Content-Type: application/json');

if (!isset($_GET['lat_min']) || !isset($_GET['lat_max']) ||
    !isset($_GET['lon_min']) || !isset($_GET['lon_max'])) {
    $lat_min = -180;
    $lat_max = 180;;
    $lon_min = -180;
    $lon_max = 180;
}else{
    $lat_min = $_GET['lat_min'];
    $lat_max = $_GET['lat_max'];
    $lon_min = $_GET['lon_min'];
    $lon_max = $_GET['lon_max'];
}

$db = new PDO('sqlite:../var/mapdata.sqlite');
$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

$stmt = $db->prepare("
    SELECT lat, lon, elevation, light_pollution, name
    FROM regions
    WHERE lat >= :lat_min AND lat < :lat_max
      AND lon >= :lon_min AND lon < :lon_max
");

$stmt->execute([
    ':lat_min' => $lat_min,
    ':lat_max' => $lat_max,
    ':lon_min' => $lon_min,
    ':lon_max' => $lon_max
]);

$result = [];
foreach ($stmt as $row) {
    $key = sprintf("%.2f,%.2f", round($row['lat'], 2), round($row['lon'], 2));
    $result[$key] = [
        'r' => 1.0,
        'g' => 0.0,
        'b' => 0.0,
        'e' => $row['elevation'],
        'p' => $row['light_pollution'],
        'n' => $row['name'],
    ];
}

echo json_encode($result);

