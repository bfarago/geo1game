<?php
$config = parse_ini_file(__DIR__ . '/../etc/geod/geod.ini', true);
if (!$config || !isset($config['MYSQL'])) {
    die("Missing or invalid ini file: geod.ini");
}

$dbconf = $config['MYSQL'];

$host = $dbconf['db_host'];
$db = $dbconf['db_database'];
$user = $dbconf['db_user'];
$pass = $dbconf['db_password'];
$port = $dbconf['db_port'];
$charset = 'utf8mb4';

$dsn = "mysql:host=$host;port=$port;dbname=$db;charset=$charset";
$options = [
    PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
];

try {
    $pdo = new PDO($dsn, $user, $pass, $options);
} catch (PDOException $e) {
    die("Database connection error: " . $e->getMessage());
}
?>