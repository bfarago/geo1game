<?php
require_once('db.php');
require_once('geo.php');
session_start();
geo_session_start();

$checkboxrowsdesc = [
    [
        'align' => 'center',
        'label' => 'Screen preferences',
        'comment' => 'You can change your default screen preferences here.',
        'type' => 'subtitle'
    ],
    [
        'align' => 'center',
        'label' => 'Web browser resolution',
        'type' => 'subtitle'
    ],
    [
        'align' => 'right',
        'label' => 'Desktop:',
        'type' => 'select',
        'name' => 'scrdesktop',
        'options' => $scrreslabels,
        'value' => '',
        'comment' => 'In case of you play on computer.',
        'required' => true
    ],
    [
        'align' => 'right',
        'label' => 'Handheld:',
        'type' => 'select',
        'name' => 'scrmobile',
        'options' => $scrreslabels,
        'value' => '',
        'comment' => 'In case you are browsing on your mobile phone.',
        'required' => true
    ],
    [
        'align' => 'center',
        'label' => 'Visual subsystem preferences',
        'type' => 'subtitle',
        'comment' => 'These are the settings that will be applied to the visual subsystem of the game.'
    ],
    [
        'align' => 'right',
        'label' => 'Star model:',
        'type' => 'checkbox',
        'name' => 'star',
        'value' => '1',
        'comment' => 'The star model used in the simulation for the night and day time visuals.',
        'required' => true
    ],
    [
        'align' => 'right',
        'label' => 'Clouds model:',
        'type' => 'checkbox',
        'name' => 'clouds',
        'value' => '1',
        'comment' => 'The clouds model used in the simulation for the visual representation of the sky.',
        'required' => true
    ],
    [
        'align' => 'right',
        'label' => 'Satellites:',
        'type' => 'checkbox',
        'name' => 'satellites',
        'value' => '1',
        'comment' => 'Satellites visuals are enabled',
        'required' => true
    ],
    [
        'align' => 'right',
        'label' => 'Graticule:',
        'type' => 'checkbox',
        'name' => 'graticule',
        'value' => '1',
        'comment' => 'Show/hide latitude and longitude grid lines (graticule) over the map or globe.',
        'required' => true
    ],
    [
        'align' => 'right',
        'label' => 'Atmosphere:',
        'type' => 'checkbox',
        'name' => 'atmosphere',
        'value' => '1',
        'comment' => 'Show/hide the atmospheric effect of the globe view. The color of the atmosphere changes depending on the angle of attack of the star rays.',
        'required' => true
    ]
];

//previous
$stmt = $pdo->prepare("SELECT * FROM users WHERE id = ? AND session_id = ?");
$stmt->execute([$_SESSION['user_id'], $_SESSION['session_id']]);
$user = $stmt->fetch();

if (!$user) {
    session_destroy();
    header("Location: login.php");
    exit;
}
fromDbToInternal();


$message = '';
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $nick = trim($_POST['nick']);
    $oldpassword = $_POST['oldpassword'];
    $password = $_POST['password'];
    $confirm = $_POST['confirm'];
    
    //check plausibility
    $user['scrdesktop'] = fromUIToInternal('scrdesktop', 0, count($scrreslabels) - 1, $user['scrdesktop']);
    $user['scrmobile'] = fromUIToInternal('scrmobile', 0, count($scrreslabels) - 1, $user['scrmobile']);
    for ($i=0; $i<count($useroptnames); $i++) {
        $id=$useroptnames[$i];
        $user[$id] = fromUIToInternal($id, 0, 1, 0);
    }

    //from internal to db
    $ascreens=[];
    for ($i=0; $i<count($userscrnames); $i++) {
        $value = isset($user[$userscrnames[$i]]) ? $user[$userscrnames[$i]] : 0;
        //saturate to plausible min..max range
        if ($value < 0) $value = 0;
        if ($value > count($scrreslabels) - 1) $value = count($scrreslabels) - 1;
        $ascreens[$i] = $value;
    }
    $screens = join(",",$ascreens);
    $aoptions=array();
    for ($i=0; $i<count($useroptnames); $i++) {
        $value = $user[$useroptnames[$i]];
        if ($value < 0) $value = 0;
        if ($value > 1) $value = 1;
        if ($value == 1) $aoptions[] = $i;
    }
    if (count($aoptions) == 0){
        $options = "";
    }else{
        $options = join(",",$aoptions);
    }

    if (empty($nick)) {
        $message = "Nickname cannot be empty.";
    } else {
        if ($nick != $user['nick']) {
            $stmt = $pdo->prepare("UPDATE users SET nick = ? WHERE id = ?");
            $stmt->execute([$nick, $user['id']]);
            $message .= "Nickname updated.";
        }
    }
    if ($screens != $user['screens']) {
        $stmt = $pdo->prepare("UPDATE users SET screens = ? WHERE id = ?");
        $stmt->execute([$screens, $user['id']]);
        $message .= "Screens updated.";
    }
    if ($options != $user['options']) {
        $stmt = $pdo->prepare("UPDATE users SET options = ? WHERE id = ?");
        $stmt->execute([$options, $user['id']]);
        $message .= "Options updated.";
    }

    /* CHECK old password with the db password, see login php.
    if ()
            if ($password !== $confirm) {
                $message = "Passwords did not matched.";
            } else {
                $hash = password_hash($password, PASSWORD_DEFAULT);
                $stmt = $pdo->prepare("UPDATE users SET nick = ?, password_hash = ? WHERE id = ?");
               // $stmt->execute([$nick, $hash, $user['id']]);
                $message = "Nickname and password updated.";
            }
    }
    */
    // After update, refresh $user
    $stmt = $pdo->prepare("SELECT * FROM users WHERE id = ?");
    $stmt->execute([$user['id']]);
    $user = $stmt->fetch();
    fromDbToInternal();
}

function fromUIToInternal($pname, $min, $max, $default) {
    if (isset($_POST[$pname])) {
        if ($_POST[$pname] >= $min && $_POST[$pname] <= $max) {
            return $_POST[$pname];
        }
    }
    return $default;
}

//helper
function genSelect($name,$value,$options,$required){
    global $user;
    $stag = "<select name='$name' id='$name' required>";
    for ($i=0;$i<count($options);$i++){
        $otag = "<option value='".$i."'";
        if ($user[$name]==$i) $otag .= " selected";
        $otag .= ">".$options[$i]."</option>";
        $stag .= $otag;
    }
    $stag .= "</select>";
    return $stag;
}
function genCheckbox($name,$value,$checked){
    $b = "<input type='checkbox' name='".$name."' value='".$value."'";
    if ($checked) $b .= " checked";
    $b .= " />";
    return $b;
}

function genRow($proc, $desc){
    global $user;
    $rows = "\n";
    for ($i=0;$i<count($desc);$i++){
        $d=$desc[$i];
        $a = "<tr>";
        $tdparams='';
        if (($proc['rowcounter'] % 2 ) ==0){
            $tdparams.=" bgcolor=#222"; //$tdclass="odd";
        }
        if ($d['type']=='subtitle'){
            $a .= "<td colspan=3 align='".$d['align']."' $tdparams>";
            $a .= $d['label']."\n";
            if (!empty($d['value'])){
                $a .= "<p>".$d['value']."</p>";
            }
            if (!empty($d['comment'])){
                $a .= "<p class='comment'>".$d['comment']."</p>";
            }
            $a .= "</td>";
        }else{
            $a .= "<td align='".$d['align']."'$tdparams>";
            $a .= "<label for='".$d['name']."'>".$d['label']."</label>";
            $a .= "</td><td$tdparams>";
            switch ($d['type']){
                case 'checkbox':
                    $checked= $user[$d['name']]==1;
                    $a .= genCheckbox($d['name'],$d['value'], $checked);
                    break;
                case 'select':
                    $value = $user[$d['name']];
                    $a .= genSelect($d['name'],$value,$d['options'], $d['required']);
                    break;
                default:
            }
            $a .= "</td><td $tdparams><span class='comment'>";
            $a .= $d['comment'];
            $a .= "</span></td>";
        }
        $a .= "</tr>";
        $rows .= $a."\n";
        $proc['rowcounter']++;
    }
    return $rows;
}
$proc = [
 'rowcounter'=>0
];

//To UI
$email = $user['email'];
$nick = $user['nick'];
$rowsCheckboxes=genRow($proc, $checkboxrowsdesc);

?>
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>User</title>
<link rel="stylesheet" href="g.css">
</head>
<body>
<DIV id="main" align="center" valign="middle">
<TABLE width="60%" cellspacing="5px" cellpadding="0">
<tr><td colspan="3" align="center"><h2>User administration</h2></td></tr>
<tr><td colspan="3"><a href="index.php">Home</a> -> USER
<p>Please enter your data here.</p>
<p class="comment"></p>
</td></tr>
<?php if ($message): ?>
    <tr><td colspan="3"><p class="error"><?= $message ?></p></td></tr>
<?php endif; ?>

<form method="POST" action="">
<TR><TD align=right bgcolor=#222><label>Nickname:</td><td bgcolor=#222><input type="text" name="nick" required maxlength="<?=$nicklimitmax; ?>" value="<?= htmlspecialchars($nick) ?>"></label></td><td bgcolor=#222><span class=comment>you can change it here.</span></td><td>
<TR><TD align=right width=30%><label>Email:</td><td width=20% align=left><input readonly type="email" name="email" required maxlength="<?= $emaillimitmax ?>" value="<?= htmlspecialchars($email) ?>" readonly></label></td><td><span class=comment>You can not change it from herer.</span></TD></TR>
<tr><td colspan=3 align=center bgcolor=#222>Optionally, you can change your password.
<p class="comment">If you do not enter your actual password, or the new passwords are not the same, your old password will be kept. So, these fields are optional, you don't need to provide it.
When you have successfully changed your password,then you will be logged out by the system, and you need to login again with the new one.    
</p>
</td></TR>
<TR><TD align=right><label>Password:</td><td><input type="password" name="oldpassword"></label></td><td><span class=comment>this is the previous password.</span></TD></TR>

<TR><TD align=right bgcolor=#222><label>New password:</td><td bgcolor=#222><input type="password" name="password"></label></td><td bgcolor=#222><span class=comment>should be at least 6 characters long.</span></TD></TR>
<TR><TD align=right><label>Confirm Password:</td><td align=left><input type="password" name="confirm"></label></td><td><span class=comment>It should match the new password.</span></td></TR>
<!----- -->
<?=$rowsCheckboxes ?>
<tr><td colspan=3 align=right><button class=subbtn type="submit">SAVE changes</button></td></TR>
</form>
<tr>
    <td colspan="3" align="center">
        <p>Or:</p>
        <p class="comment">In case you have pushed the SAVE button, only you can trust, the modifications are updated.
            Or, you can cancel the latest edit, if navigates to any other page.
        </p>
        <div class="linkbox-container">
            <div class="linkbox">
                <a href="globe2.php">back to the GAME</a>
            </div>
            <div class="linkbox">
                <a href="logout.php?demo=1">LOGOUT</a>
            </div>
        </div>
    </td>
</tr>
</TABLE>
</DIV></body></html>