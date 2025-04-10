<?php
header('Content-Type: application/json');

$db = new PDO('sqlite:../var/mapdata.sqlite');
$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

$query = $db->query("SELECT id, lat, lon, lat2, lon2, population, light_pollution, name FROM regions");
$regions = $query->fetchAll(PDO::FETCH_ASSOC);

echo json_encode($regions, JSON_PRETTY_PRINT);

