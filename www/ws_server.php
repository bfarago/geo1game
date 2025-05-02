<?php
include_once 'db.php';
error_reporting(E_ALL);
ini_set('display_errors', 1);

// Configuration
$address = '127.0.0.1';
$port = 9000;
$updateIntervalSec = 1; // to WS
$databaseUpdateIntervalSec = 30;    // from Db
$databaseSaveIntervalSec = 20;    // to Db
$clientTimeoutSec = 30;
$keepaliveIntervalSec=10;
$g_version = 1;
$g_min_version = 0; // to ws
$g_db_version  = 0; // to db

// Internal state
$clients = []; // client socket id => ['socket'=>resource, 'user_id'=>int|null, 'seen_versions'=>[], 'last_pong'=>time()]
$users = [];   // user_id => ['lat'=>float, 'lon'=>float, 'alt'=>float, 'version'=>int]
$sessions = []; 
$lastDbPoll = 0; // time();
$lastPingTime = 0;
$lastDbSave = time();
$lastWsUpdate = time();

// Create server socket
$sock = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
if ($sock === false) {
    die("socket_create failed: " . socket_strerror(socket_last_error()) . "\n");
}

if (!socket_set_option($sock, SOL_SOCKET, SO_REUSEADDR, 1)) {
    die("socket_set_option failed: " . socket_strerror(socket_last_error()) . "\n");
}

if (socket_bind($sock, $address, $port) === false) {
    die("socket_bind failed: " . socket_strerror(socket_last_error()) . "\n");
}

if (socket_listen($sock) === false) {
    die("socket_listen failed: " . socket_strerror(socket_last_error()) . "\n");
}

echo "Listening on $address:$port...\n";

function safe_socket_write($client, $data) {
    $bytes = @socket_write($client, $data);
    if ($bytes === false) {
        // Write failed, client probably dead, remove from clients
        echo "socket_write failed, removing client\n";
        global $clients;
        $clientId = (int)$client;
        if (isset($clients[$clientId])) {
            if (is_resource($clients[$clientId]['socket'])) {
                socket_close($clients[$clientId]['socket']);
            }
            unset($clients[$clientId]);
        }
        return false;
    }
    return true;
}
// Helper: send JSON message framed as WebSocket text frame
function sendWsMessage($client, $msg) {
    $data = json_encode($msg);
    $frame = chr(0x81);
    $len = strlen($data);
    if ($len <= 125) {
        $frame .= chr($len);
    } elseif ($len <= 65535) {
        $frame .= chr(126) . pack('n', $len);
    } else {
        $frame .= chr(127) . pack('J', $len);
    }
    $frame .= $data;
    return socket_write($client, $frame);
}

// Helper: decode a single websocket text frame (no fragmentation)
function decodeWsFrame($data) {
    if (strlen($data) < 6) return ''; // not enough data
    $length = ord($data[1]) & 127;
    $maskStart = 2;
    if ($length === 126) {
        $length = unpack('n', substr($data, 2, 2))[1];
        $maskStart = 4;
    } elseif ($length === 127) {
        $length = unpack('J', substr($data, 2, 8))[1];
        $maskStart = 10;
    }
    $masks = substr($data, $maskStart, 4);
    $payload = substr($data, $maskStart + 4, $length);
    $decoded = '';
    for ($i = 0; $i < $length; ++$i) {
        $decoded .= $payload[$i] ^ $masks[$i % 4];
    }
    return $decoded;
}

// Helper: perform WebSocket handshake
function doHandshake($client, $headers) {
    if (!preg_match('/Sec-WebSocket-Key: (.*)\r\n/', $headers, $matches)) {
        return false;
    }
    $key = trim($matches[1]);
    $acceptKey = base64_encode(pack('H*', sha1($key . '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')));
    $upgrade = "HTTP/1.1 101 Switching Protocols\r\n" .
               "Upgrade: websocket\r\n" .
               "Connection: Upgrade\r\n" .
               "Sec-WebSocket-Accept: $acceptKey\r\n\r\n";
    return socket_write($client, $upgrade);
}

function log_client($clientid, $info, $text) {
    global $clientTimeoutSec, $sessions, $users;
        
    $sessionid = $info['session_id'];
    $userid = $info['user_id'];
    if (!isset($userid)) {
        if (isset($sessions[$sessionid]) && isset($sessions[$sessionid]['user_id'])) {
            $userid = $sessions[$sessionid]['user_id'];
        } else {
            $userid = null;
        }
    }
    
    $lastpong = isset($info['last_pong']) ? $info['last_pong'] : 0;
    $userStillConnected = (time() - $lastpong) <= $clientTimeoutSec;
    $s = '';
    if (isset($sessionid) && isset($sessions[$sessionid])) {
        if (isset($sessions[$sessionid]['version'])){
            $s = $sessions[$sessionid]['version'];
        }
    }

    $nickname = '';
    if (isset($users[$userid]['nick']) && strlen(trim($users[$userid]['nick'])) > 0) {
        $nickname = '(' . $users[$userid]['nick'] . ')';
    } else {
        $nickname = '(' . $userid . ')';
    }

    echo "C:$clientid U:$userid$nickname S:$sessionid L:$lastpong V:$s C:$userStillConnected : $text\n";
}

// Helper: read user data from DB
function loadUsersFromDb() {
    global $pdo, $g_min_version;
    $stmt = $pdo->query("SELECT id, lat, lon, alt, nick FROM users ORDER BY last_login DESC LIMIT 100");
    $result = [];
    while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
        $result[(int)$row['id']] = [
            'lat' => (float)$row['lat'],
            'lon' => (float)$row['lon'],
            'alt' => (float)$row['alt'],
            'version' => $g_min_version,
            'nick' => $row['nick']
        ];
    }
    return $result;
}
function loadUserFromDb($userid) {
    global $pdo, $users, $g_version;
    $stmt = $pdo->prepare("SELECT id, lat, lon, alt, nick FROM users where id = ? LIMIT 1");
    $stmt->execute([$userid]);
    $result = [];
    while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
        $users[$userid]['lat'] = (float)$row['lat'];
        $users[$userid]['lon'] = (float)$row['lon'];
        $users[$userid]['alt'] = (float)$row['alt'];
        $users[$userid]['fromwstodb']=0;
        $users[$userid]['nick'] = $row['nick'];
        if (!isset($users[$userid]['version'])) $users[$userid]['version'] = $g_version;
    }
    return $users[$userid];
}
// Helper: update user position in DB and increment version
function updateUserPositionToDb($user_id, $lat, $lon, $alt, $version) {
    global $pdo, $g_version, $users;
    if (!isset($user_id)){
        echo "User id is null\n";
        return;
    }
    $stmt = $pdo->prepare("UPDATE users SET lat = ?, lon = ?, alt = ? WHERE id = ?");
    $stmt->execute([$lat, $lon, $alt, $user_id]);
    if (!isset($users[$user_id])) {
        $users[$user_id]['lat'] = $lat;
        $users[$user_id]['lon'] = $lon;
        $users[$user_id]['alt'] = $alt;
    }else{
        if ($users[$user_id]['version'] <= $version){
            $users[$user_id]['fromwstodb'] = 0;
        }
    }
    $users[$user_id]['version'] = $version;
}
function updateUserPositionFromWS($clientId, $user_id, $lat, $lon, $alt, $version){
    global $users, $g_version, $clients;
    if (!isset($user_id)){
        echo "User id is null\n";
        return;
    }
    // plausible values
    $lat = round($lat, 4);
    $lon = round($lon, 4);
    $alt = round($alt, 4);
    if (isset($users[$user_id])) {
        if ($users[$user_id]['version'] > $version){
            echo "User version is too old\n";
            return;
        }
        $adlat = abs($users[$user_id]['lat']-$lat);
        $adlon = abs($users[$user_id]['lon']-$lon);
        $adalt = abs($users[$user_id]['alt']-$alt);
        if ($adlat > 0.0001 || $adlon > 0.0001 || $adalt > 0.0001){
            $g_version+=1;
            $users[$user_id]['lat'] = $lat;
            $users[$user_id]['lon'] = $lon;
            $users[$user_id]['alt'] = $alt;
            $users[$user_id]['version'] = $g_version;
            $users[$user_id]['fromwstodb'] = 1; // db management will be posponed for later time
            $s= "Client sent position update for user $user_id (lat $lat, lon $lon, alt $alt) and stored in memory\n";
            log_client($clientId, $clients[$clientId], $s);
        }else{
            log_client($clientId, $clients[$clientId], "Client sent position update for user $user_id (lat $lat, lon $lon, alt $alt) but nothing changed\n");
        }
    }else{
        $g_version+=1;
        updateUserPositionToDb($user_id, $lat, $lon, $alt, $g_version);
    }
    //    $sessions[$clientId]['seen_versions'] = $g_version;
}
// Helper: lookup user_id by session_id
function getUserIdBySession($session_id) {
    global $sessions;
    if (isset($sessions[$session_id])) {
        if (isset($sessions[$session_id]['user_id']) && $sessions[$session_id]['user_id'] !== null) {
            return $sessions[$session_id]['user_id'];
        }
    }
    return null;
}
function getUserIdBySessionFromDb($session_id) {
    global $pdo;
    $stmt = $pdo->prepare("SELECT id FROM users WHERE session_id = ?");
    $stmt->execute([$session_id]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return $row ? (int)$row['id'] : null;
}
function extractSessionId($headers) {
    if (preg_match('/Cookie: .*PHPSESSID=([a-zA-Z0-9]+)/', $headers, $matches)) {
        return $matches[1];
    }
    return null;
}
// Main loop
while (true) {
    // Prepare sockets to read
    $read = [$sock];
    foreach ($clients as $info) {
        $read[] = $info['socket'];
    }
    // <<< ITT!!! >>>
    $read = array_filter($read, function($sock) {
        return is_resource($sock);
    });

    $write = $except = null;
    $tv_sec = 1; // 1 second timeout for socket_select

    $numChanged = socket_select($read, $write, $except, $tv_sec);
    if ($numChanged === false) {
        echo "socket_select error\n";
        break;
    }


    // Handle new connections
    if (in_array($sock, $read)) {
        $newClient = socket_accept($sock);
        if ($newClient !== false) {
            // Wait for handshake
            // 2 sec write and read timeout
            socket_set_option($newClient, SOL_SOCKET, SO_SNDTIMEO, ['sec' => 3, 'usec' => 0]);
            socket_set_option($newClient, SOL_SOCKET, SO_RCVTIMEO, ['sec' => 3, 'usec' => 0]);
            $header = '';
            while (true) {
                $buf = socket_read($newClient, 1024);
                if ($buf === false || $buf === '') {
                    socket_close($newClient);
                    echo "socket_read error\n";
                    continue ;
                }
                $header .= $buf;
                if (strpos($header, "\r\n\r\n") !== false) break;
            }
            $session_id = extractSessionId($header);
            if ($session_id !== null) {
                echo "Session ID from cookie: $session_id\n";
            }
            if (doHandshake($newClient, $header)) {
                $clients[(int)$newClient] = [
                    'socket' => $newClient,
                    'user_id' => null,
                    'session_id' => $session_id,
                    'seen_versions' => [],
                    'last_pong' => time(),
                ];
                echo "New client connected and handshake done\n";
            } else {
                log_client($newClient, $clients[$newClient], "Handshake failed");
                socket_close($newClient);
            }
        }
        // Remove server socket from read array so we don't process it again below
        $read = array_diff($read, [$sock]);
    }

    // Handle client messages
    foreach ($read as $sockResource) {
        $data = @socket_read($sockResource, 2048, PHP_BINARY_READ);
        $clientId = (int)$sockResource;
        if ($data === false || $data === '') {
            // Client disconnected
            //$clientId = (int)$sockResource;
            if (isset($clients[$clientId])) {
                //echo "Client disconnected (user_id=" . (isset($clients[$clientId]['user_id']) ? $clients[$clientId]['user_id'] : 'null') . ")\n";
                log_client($clientId, $clients[$clientId], "Client disconnected 219");
                socket_close($clients[$clientId]['socket']);
                unset($clients[$clientId]);
            } else {
                echo "Disconnect unknown $clientId\n";
                socket_close($sockResource);
            }
            continue;
        }

        // Decode websocket frame
        $message = decodeWsFrame($data);
        if ($message === '') {
            // Not a text frame or empty
            if (isset($clients[$clientId])) {
                log_client($clientId, $clients[$clientId], "Invalid message");
            }{
                echo "Invalid message $clientId\n";
            }
            continue;
        }

        // Parse JSON message
        $msg = json_decode($message, true);
        if (!is_array($msg) || !isset($msg['type'])) {
            // Invalid message
            log_client($clientId, $clients[$clientId], "Invalid message");
            continue;
        }

        // Session / User resolution
        $session_id = $clients[$clientId]['session_id'];
        // Client could send session_id to identify user
        if (isset($msg['session_id'])) {
            $session_id = $msg['session_id'];
            echo "Client $clientId identified as session_id $session_id\n";
        }
        if (!isset($session_id)) {
            // NO SESSION, NO GAME..
            sendWsMessage($clients[$clientId]['socket'], ['type'=>'error', 'message'=>'Missing session_id']);
            log_client($clientId, $clients[$clientId], "Missing session_id");
            break;
        }
        $user_id = getUserIdBySession($session_id); // it was online before.

        // Handle message types
        switch ($msg['type']) {
            case 'hello':
                if ($user_id === null) {
                    // never seen online, check the database
                    $user_id = getUserIdBySessionFromDb($session_id);
                    if ($user_id === null) {
                        sendWsMessage($clients[$clientId]['socket'], ['type'=>'error', 'message'=>'Invalid session_id']);
                        log_client($clientId, $clients[$clientId], "Invalid session_id");
                        break;
                    }
                    $sessions[$session_id]['user_id'] = $user_id;
                    $sessions[$session_id]['seeen_versions'] = [];
                    $sessions[$session_id]['version']=$g_version; 
                }
                $clients[$clientId]['user_id'] = $user_id;
                $sessions[$session_id]['version']=$g_version; 
                $clients[$clientId]['seen_versions'] = []; // Probably removable later
                
                // User daata memory is online, or need to be loaded from DB?
                if (!isset($users[$user_id])) {
                    // Load from DB if not loaded yet
                    log_client($clientId, $clients[$clientId], "Loading user data from DB");
                    //$users = array_merge($users, loadUsersFromDb());
                    loadUserFromDb($user_id);
                    echo "Loaded user data from DB finished\n";
                }

                if (isset($users[$user_id])) {
                    log_client($clientId, $clients[$clientId], "Sending user data as part of the HELO protocol");
                    sendWsMessage($clients[$clientId]['socket'], [
                        'type' => 'user_data',
                        'user_id' => $user_id,
                        'lat' => $users[$user_id]['lat'],
                        'lon' => $users[$user_id]['lon'],
                        'alt' => $users[$user_id]['alt'],
                        'version' => $g_version,
                    ]);
                    $sessions[$session_id]['version']=$g_version; 
                    $sessions[$session_id]['seen_versions'][$user_id] = $g_version; // This user device got its own data back
                    $clients[$clientId]['seen_versions'][$user_id] = $g_version;
                }
                break;

            case 'update_user_pos': // renamed
                // Client sends updated position
                log_client($clientId, $clients[$clientId], "Client sent position update");
                
                if ($user_id === null) {
                    sendWsMessage($clients[$clientId]['socket'], ['type'=>'error', 'message'=>'Not identified']);
                    break;
                }
                if (!isset($msg['lat'], $msg['lon'], $msg['alt'])) {
                    sendWsMessage($clients[$clientId]['socket'], ['type'=>'error', 'message'=>'Missing position data']);
                    break;
                }
                $lat = (float)$msg['lat'];
                $lon = (float)$msg['lon'];
                $alt = (float)$msg['alt'];
                updateUserPositionFromWS($clientId, $user_id, $lat, $lon, $alt, $g_version);
                break;
            case 'chat_message':
                log_client($clientId, $clients[$clientId], "Client sent chat message");

                if (!isset($msg['message'])) {
                    sendWsMessage($clients[$clientId]['socket'], ['type'=>'error', 'message'=>'Missing chat message']);
                    break;
                }
                $usr_sender =$clients[$clientId]['user_id'];
                $chatPacket = [
                    'type' => 'chat_message',
                    'user_id' => $usr_sender,
                    'nick' => isset($users[$usr_sender]['nick']) ? $users[$usr_sender]['nick'] : 'unknown',
                    'message' => $msg['message'],
                    'camera' => isset($msg['camera']) ? $msg['camera'] : null,
                    'timestamp' => time(),
                ];

                // Broadcast chat message to all connected clients
                foreach ($clients as $targetClientId => $targetInfo) {
                    if (is_resource($targetInfo['socket'])) {
                        sendWsMessage($targetInfo['socket'], $chatPacket);
                    }
                }
                break;
            case 'pong':
                // Client responded to ping
                $now = time();
                $clients[$clientId]['last_pong'] = $now;
                $sessions[$session_id]['last_pong'] = $now;
                $users[$user_id]['last_pong'] = $now;
                //log_client($clientId, $clients[$clientId], "Client responded to ping by a pong");
                break;
            case 'disconnect':
                log_client($clientId, $clients[$clientId], "Client requested disconnect");
                if (isset($clients[$clientId])) {
                    if (is_resource($clients[$clientId]['socket'])) {
                        socket_close($clients[$clientId]['socket']);
                    }
                    unset($clients[$clientId]);
                }
                break;
            case 'refresh':
                // Client requested refresh
                $sessions[$session_id]['seen_versions']=[];
                $sessions[$session_id]['version']=0;
                break;
            default:
                // Unknown message type, ignore
                log_client($clientId, $clients[$clientId], "Unknown message type\n");
                break;
        }
    }

    // Periodic DB polling and push updates to clients
    $now = time();
    if ($now - $lastDbPoll >= $databaseUpdateIntervalSec) {
        $lastDbPoll = $now;
        //echo "Polling DB\n";
        $newUsers = loadUsersFromDb();

        // Compare and update local users array
        foreach ($newUsers as $uid => $data) {
            $userStillConnected = false;
            foreach ($clients as $info) {
                if ($info['user_id'] === $uid) {
                    $userStillConnected = true;
                    break;
                }
            }
            if (!isset($users[$uid])) {
                $users[$uid] = $data;
                $users[$uid]['fromwstodb']=0;
            }else{
                $adlat=abs($users[$uid]['lat']-$data['lat']);
                $adlon=abs($users[$uid]['lon']-$data['lon']);
                $adalt=abs($users[$uid]['alt']-$data['alt']);
                if ($adlat>0.001 || $adlon>0.001 || $adalt>0.001) {
                    $users[$uid]['lat']=$data['lat'];
                    $users[$uid]['lon']=$data['lon'];
                    $users[$uid]['alt']=$data['alt'];
                    echo "User $uid updated from db\n";
                }
            }
            if (!$userStillConnected) {
                $users[$uid] = $data;
            }
            $users[$uid]['fromwstodb']=0;
            $users[$uid]['still_connected'] = $userStillConnected;
        }
    }
    $now = time();
    if ($now - $lastWsUpdate >= $updateIntervalSec) {
        if ($g_version > $g_min_version) {
            // Push updates to clients for users they haven't seen or outdated versions
            //foreach ($clients as &$info) {
            foreach ($clients as $clientId => $info) {
                // Iterate over client sockets, shall belongs to session...
                $clientId = (int)$info['socket'];
                $cli_sion = $clients[$clientId]['session_id'];
                foreach ($users as $uid => $data) {
                    $seenVersion = isset($sessions[$cli_sion]['seen_versions'][$uid]) ? $sessions[$cli_sion]['seen_versions'][$uid] : 0;
//                    $seenVersion = isset($info['seen_versions'][$uid]) ? $info['seen_versions'][$uid] : 0;
                    $dataversion = $data['version'];
                    if ($dataversion > $seenVersion) 
                    {
                        $lat = $data['lat'];
                        $lon = $data['lon'];
                        $alt = $data['alt'];
                        $packet=[
                            'type' => 'user_data',
                            'user_id' => $uid,
                            'lat' => $lat,
                            'lon' => $lon,
                            'alt' => $alt,
                            'version' => $dataversion
                        ];
                    
                        log_client($clientId, $info, "Pushing user_data $uid to client $clientId Started $lat,$lon,$alt,$dataversion v:$g_version)");
                        if (is_resource($info['socket'])) {
                            sendWsMessage($clients[$clientId]['socket'], $packet);
                        }
                        $sessions[$cli_sion]['seen_versions'][$uid] = $dataversion; // todo: here and thec seenversion may need to be updated if we have 1 common version.
                        $sessions[$cli_sion]['version']= $dataversion;  // I mean this one.
                        //$info['seen_versions'][$uid] = $g_version;; // TODO: delete this
                        //echo "Pushing user_data $uid to client $clientId Finished\n";
                    }
                }
                
            }
            $g_min_version = $g_version;
            unset($info);
        }
    }
    
    // Keepalive: send ping and disconnect clients without recent pong
    $now = time();
    if ($now - $lastPingTime >= $keepaliveIntervalSec) { 
        foreach ($clients as $clientId => &$info) {
            if ($now - (isset($info['last_pong']) ? $info['last_pong'] : 0) > $clientTimeoutSec) {
                echo "Client timeout, disconnecting user_id " . (isset($info['user_id']) ? $info['user_id'] : 'null') . "\n";
                if (is_resource($info['socket'])) {
                    socket_close($info['socket']);
                }
                unset($clients[$clientId]);
                continue;
            }
            // Send ping message
            if (is_resource($info['socket'])) {
                sendWsMessage($info['socket'], ['type' => 'ping']);
                //echo "Ping sent to client $clientId\n";
            }
        }
        $lastPingTime = $now;
        unset($info);
    }

    // a round robin through the users to save updates to database..
    if ($g_db_version < $g_version) {
        if ($now - $lastDbSave >= $databaseSaveIntervalSec) {
            $lastDbSave = $now;
            //echo "Saving to database\n";
            foreach ($users as $uid => $data) {
                if (isset($data['fromwstodb']) && $data['fromwstodb']) {
                    updateUserPositionToDB($uid, $data['lat'], $data['lon'], $data['alt'], $data['version']);
                    $g_db_version = $g_version;
                }
            }
        }
    }
}
?>
