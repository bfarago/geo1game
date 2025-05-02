<?php

//config
$userscrnames = ['scrdesktop','scrmobile'];
$scrreslabels = ["Low res","Medium res","High res","Very high res","Ultra high res"];
$useroptnames = ['star','clouds','satellites', 'graticule', 'atmosphere'];
$useroptlabels = [
    'Star model enabled',
    'Clouds enabled',
    'Satellites enabled',
    'Graticule enabled',
    'Atmosphere enabled'
];
$emaillimitmax =50;
$nicklimitmax = 20;

$config = [
    'user_id' => 0,
    'user_nick' => 'Anonymous',
    'is_mobile' => preg_match('/Mobi|Android/i', $_SERVER['HTTP_USER_AGENT']),
    'map_texture_resolution'=> [
        'preset'=>1,
        'width'=>720,
        'height'=>360],
    'resolutions'=>[
        ['map_width'=>360,  'map_height'=>180,  'name'=>'Low res'],
        ['map_width'=>720,  'map_height'=>360,  'name'=>'Medium res'],
        ['map_width'=>1440, 'map_height'=>720,  'name'=>'High res'],
        ['map_width'=>3600, 'map_height'=>1800, 'name'=>'Very high res'],
        ['map_width'=>7200, 'map_height'=>3600, 'name'=>'Ultra high res']
    ]
];

//vars
$session_id = 0;
$logged_in = false;
$session_json_mode =0;

//fn
function geo_session_start(){
    global $session_id, $logged_in, $pdo, $user, $config, $session_json_mode;

    $logged_in = false;
    $session_id = session_id();
    
    if (!isset($_SESSION['user_id']) || $_SESSION['user_id'] == 0) {
        $gw= '';
        $gw= isset($_ENV['GATEWAY_INTERFACE']) ? $_ENV['GATEWAY_INTERFACE'] : $gw;
        $gw= isset($_SERVER['GATEWAY_INTERFACE']) ? $_SERVER['GATEWAY_INTERFACE'] : $gw;

        if ($gw == 'CGI/1.1'){
            //server proxy, gw, service...
            $session_id = $_SERVER['PHPSESSID'];
            parse_str( $_SERVER['QUERY_STRING'], $_GET);
            $config['cgi']['request_time'] = $_SERVER['REQUEST_TIME'];
            $config['cgi']['session_id']=$session_id;
            $stmt = $pdo->prepare("SELECT * FROM users WHERE session_id = ? LIMIT 1");
            if ( $stmt->execute([$session_id]) ){
                $user = $stmt->fetch();
                if ($user) {
                    // we can trust in the server side...
                    $_SESSION['user_id']=$user['id'];
                }
            }
        }
    }        
    if (isset($session_id)){
        $stmt = $pdo->prepare("SELECT * FROM users WHERE session_id = ? LIMIT 1");
        if ( $stmt->execute([$session_id]) ){
            $user = $stmt->fetch();
            if ($user) {
                if ($_SESSION['user_id'] == $user['id']){
                    $logged_in = true;
                }
            }
        }else{

        }
    }
    if (!$logged_in){
        if ($session_json_mode){

            echo json_encode($config); 
            /* json_encode([
                'Error' => "You are not logged in.",
                'Session id' => $session_id
            ]);*/
            
        }else{
            session_destroy();
            header("Location: login.php");
        }
        exit;
    }
    $config['user_id'] = (int)$user['id'];
    $config['user_nick'] = $user['nick'];
    $config['last_known_location'] = [$user['lat'], $user['lon'], $user['alt']];
    fromDbToInternal();
    $config['resolution'] = [
        $config['mobile'] = (int)$user['scrmobile'],
        $config['desktop'] = (int)$user['scrdesktop']    
    ];
    $preset=$is_mobile ? (int)$user['scrmobile'] : (int)$user['scrdesktop'];
    $config['map_texture_resolution']['preset'] = $preset;
    $config['map_texture_resolution']['width'] = $config['resolutions'][$preset]['map_width'];
    $config['map_texture_resolution']['height'] = $config['resolutions'][$preset]['map_height'];
    $config['options']['star'] = (1==(int)$user['star']);
    $config['options']['clouds'] = (1==(int)$user['clouds']);
    $config['options']['satellites'] = (1==(int)$user['satellites']);
    $config['options']['graticule'] = (1==(int)$user['graticule']);
    $config['options']['atmosphere'] = (1==(int)$user['atmosphere']);
    $use_geod = isset($_GET['debug']) && ($_GET['debug']==33);
    if (use_geod){
        $config['api']['test']='/geoapi/test.php?';
        $config['api']['user']='/geoapi/user?';
        $config['api']['regions']='/geoapi/regions_chunk';
    } else {
        $config['api']['test']='test.php?';
        $config['api']['user']='/geoapi/user?';
        $config['api']['regions']='/geoapi/regions_chunk';
    }
}
function apiURL($params){
    return $config['api'] . http_build_query($params);
}
function fromDbToInternal() {
    global $user, $scrreslabels, $useroptnames, $userscrnames;
    // proces raw db users table fields to UI direction
    $screens = isset($user['screens']) ? $user['screens'] : '3,1'; // from the database or default (very high, low)
    $options = isset($user['options']) ? $user['options'] : '0,1,2,3'; // from the database or default. (star, clouds, satellites on)
    $screenvalues = explode(',', $screens);
    $optionvalues = explode(',', $options);
    $scrlen= min( count($screenvalues), count($scrreslabels));
    //prpcess screens related list
    for ($i=0;$i<$scrlen;$i++){
        $key=$userscrnames[$i];
        $value = isset($screenvalues[$i]) ? $screenvalues[$i] : 0;
        if ($value < 0) $value = 0;
        if ($value > count($scrreslabels)-1) $value = count($scrreslabels)-1;
        $user[$key] = $value;
        //echo "<br>$key: $value";
    }
    //prpcess options related set
    for ($i=0;$i<count($useroptnames);$i++){
        $user[$useroptnames[$i]] = 0;
    }
    for ($i=0;$i<count($optionvalues);$i++){
        $value = $optionvalues[$i];
        if ($value >=0){
            if ($value < count($useroptnames)){
                $user[$useroptnames[$value]] = 1;
            }
        }
    }
}

function getJsonConfig() {
    global $config;
    return json_encode($config, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
}
?>