<?php
header('Content-Type: application/octet-stream');
header('Content-Disposition: attachment; filename="planet_export.obj"');

$dbFile = '../var/mapdata.sqlite';
if (!file_exists($dbFile)) {
    http_response_code(500);
    echo "# Error: Database not found";
    exit;
}

try {
    $db = new PDO('sqlite:' . $dbFile);
    $stmt = $db->query('SELECT lat, lon, elevation, r, g, b FROM mapdata');
    $data = $stmt->fetchAll(PDO::FETCH_ASSOC);

    $vertices = [];
    $colors = [];
    $faces = [];

    $latStep = 0.5;
    $lonStep = 0.5;
    $radius = 1.0;

    $indexMap = [];
    $idx = 1;

    foreach ($data as $row) {
        $lat = deg2rad($row['lat']);
        $lon = deg2rad($row['lon']);
        $elev = $row['elevation'];
        $r = $row['r'];
        $g = $row['g'];
        $b = $row['b'];

        $elevationScale = 0.05;
        $rad = $radius + $elev * $elevationScale;

        $x = $rad * cos($lat) * cos($lon);
        $y = $rad * sin($lat);
        $z = $rad * cos($lat) * sin($lon);

        $key = $row['lat'] . ',' . $row['lon'];
        $indexMap[$key] = $idx++;

        $vertices[] = sprintf("v %.5f %.5f %.5f %.3f %.3f %.3f", $x, $y, $z, $r, $g, $b);
    }

    echo "# Exported OBJ\n";
    echo "# Vertices\n";
    foreach ($vertices as $v) echo $v . "\n";

    echo "# Faces (optional low-res stitch)\n";
    // Egyszerű négyzetrácsos arc-generálás
    foreach ($data as $row) {
        $lat = $row['lat'];
        $lon = $row['lon'];

        $p1 = "$lat,$lon";
        $p2 = ($lat + $latStep) . ",$lon";
        $p3 = ($lat + $latStep) . "," . ($lon + $lonStep);
        $p4 = "$lat," . ($lon + $lonStep);

        if (isset($indexMap[$p1], $indexMap[$p2], $indexMap[$p3], $indexMap[$p4])) {
            echo "f {$indexMap[$p1]} {$indexMap[$p2]} {$indexMap[$p3]} {$indexMap[$p4]}\n";
        }
    }

} catch (PDOException $e) {
    http_response_code(500);
    echo "# DB error: " . $e->getMessage();
    exit;
}
?>