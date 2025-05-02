<?php
require_once('db.php');
require_once('geo.php');
session_start();
$session_json_mode= 1;
geo_session_start();

header('Content-Type: application/json');

$task = isset($_GET['task']) ? $_GET['task'] : '';

switch ($task) {
    case 'update_user_pos':
        $input = json_decode(file_get_contents("php://input"), true);
        if (!$input || !isset($input['lat']) || !isset($input['lon']) || !isset($input['alt'])) {
            echo json_encode(["error" => "Missing parameters"]);
            exit;
        }
        $stmt = $pdo->prepare("UPDATE users SET lat = ?, lon = ?, alt = ? WHERE id = ?");
        $stmt->execute([
            $input['lat'],
            $input['lon'],
            $input['alt'],
            $_SESSION['user_id']
        ]);
        echo json_encode(["ok" => true]);
        break;

    case 'get_user_pos':
        $stmt = $pdo->prepare("SELECT lat, lon, alt FROM users WHERE id = ?");
        $stmt->execute([$_SESSION['user_id']]);
        $row = $stmt->fetch();
        echo json_encode($row ?: []);
        break;

    case 'users_pos':
        $stmt = $pdo->query("SELECT id, lat, lon, alt, nick FROM users WHERE lat IS NOT NULL AND lon IS NOT NULL");
        echo json_encode($stmt->fetchAll());
        break;
    case 'resources':
        $stmt = $pdo->query("SELECT id, name, unit FROM resources ORDER BY id");
        echo json_encode($stmt->fetchAll());
        break;

    case 'regions':
        $user_id = intval($_SESSION['user_id']);
        $stmt = $pdo->prepare("SELECT r.id, r.name FROM user_regions ur JOIN regions r ON ur.region_id = r.id WHERE ur.user_id = ?");
        $stmt->execute([$user_id]);
        echo json_encode($stmt->fetchAll());
        break;

    case 'trade_orders':
        $user_id = intval($_SESSION['user_id']);
        $stmt = $pdo->prepare("SELECT id, region_id, resource_id, mode, target_quantity, max_buy_price, min_sell_price, priority FROM trade_orders WHERE user_id = ?");
        $stmt->execute([$user_id]);
        echo json_encode($stmt->fetchAll());
        break;

    case 'delete_order':
        $id = isset($_GET['id']) ? intval($_GET['id']) : 0;
        if ($id > 0) {
            $stmt = $pdo->prepare("DELETE FROM trade_orders WHERE id = ? AND user_id = ?");
            $stmt->execute([$id, $_SESSION['user_id']]);
            echo json_encode(["ok" => true]);
        } else {
            echo json_encode(["error" => "Invalid id"]);
        }
        break;

    case 'add_order':
        $input = json_decode(file_get_contents("php://input"), true);
        if (!$input) {
            echo json_encode(["error" => "Invalid input"]);
            exit;
        }
        $stmt = $pdo->prepare("INSERT INTO trade_orders (user_id, region_id, resource_id, mode, target_quantity, max_buy_price, min_sell_price, priority) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        $stmt->execute([
            $_SESSION['user_id'],
            $input['region_id'],
            $input['resource_id'],
            $input['mode'],
            $input['target_quantity'],
            $input['max_buy_price'],
            $input['min_sell_price'],
            $input['priority']
        ]);
        echo json_encode(["ok" => true, "id" => $pdo->lastInsertId()]);
        break;

    default:
        echo json_encode(["error" => "Unknown task"]);
}
?>