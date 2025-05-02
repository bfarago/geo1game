<?php
session_start();

require_once 'db.php'; // include DB connection logic

function generate_session_id() {
    return bin2hex(openssl_random_pseudo_bytes(16));
}

if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['logout'])) {
    session_destroy();
    header("Location: index.php");
    exit;
}

$message = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST' && !isset($_POST['logout'])) {
    $email = isset($_POST['email']) ? $_POST['email'] : '';
    $password = isset($_POST['password']) ? $_POST['password'] : '';

    $stmt = $pdo->prepare("SELECT id, password_hash FROM users WHERE email = ?");
    $stmt->execute([$email]);
    $user = $stmt->fetch(PDO::FETCH_ASSOC);

    if ($user && password_verify($password, $user['password_hash'])) {
        $session_id = session_id();  //generate_session_id();
        $_SESSION['user_id'] = $user['id'];
        $_SESSION['session_id'] = $session_id;

        $ip = $_SERVER['REMOTE_ADDR'];
        $stmt = $pdo->prepare("UPDATE users SET session_id = '' WHERE session_id = ?");
        $stmt->execute([$session_id]);

        $stmt = $pdo->prepare("UPDATE users SET session_id = ?, last_login = NOW(), last_ip = ? WHERE id = ?");
        $stmt->execute([$session_id, $ip, $user['id']]);

        header("Location: globe2.php");
        exit;
    } else {
        $message = "Wrong username or password.";
        // (".$email.", ".$password.", id: ".$user['id'].", hash:".$user['password_hash'].")";
        //var_dump($password);
        //var_dump($user['password_hash']);
        //var_dump(password_verify($password, $user['password_hash']));
        //exit;
    }
}else if( isset($_GET['demo'])){
    $session_id = session_id(); 
    $stmt = $pdo->prepare("SELECT id FROM users WHERE email LIKE 'demo%' ORDER BY last_login ASC LIMIT 1");
    $stmt->execute();
    $demoUser = $stmt->fetch();
    if ($demoUser) {
        $stmt = $pdo->prepare("UPDATE users SET session_id = '' WHERE session_id = ?");
        $stmt->execute([$session_id]);
        // Update demo user's session_id
        $stmt = $pdo->prepare("UPDATE users SET session_id = ?, last_login = NOW() WHERE id = ?");
        $stmt->execute([$session_id, $demoUser['id']]);
        // Set session variables
        $_SESSION['user_id'] = $demoUser['id'];
        $_SESSION['session_id'] = $session_id;
        header("Location: globe2.php");
        exit;
    }
}else if( isset($_GET['email'])){
    $email = $_GET['email'];
}
?>
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>Login</title>
<style>
html, body {
    margin: 0;
    height: 100%;
    overflow: hidden;
    background: #000;
    color: #FFF;
    font-family: Verdana, Geneva, Tahoma, sans-serif;
    font-size: 18px;
}
a:link { color: #0ff; }
a:visited { color: #0ff; }
a:hover { color: #0f0; }
a:active { color: #0ff; }
.comment {
    font-size: 0.75em;
    color: #999;
    padding: 10px;
}
.error {
    font-size: 0.99em;
    color: #f99;
    padding: 5px;
}
.linkbox-container {
    display: flex;
    justify-content: center;
    flex-wrap: wrap;
    gap: 10px;
    margin-top: 15px;
}
.linkbox {
    background-color: #222;
    border: 1px solid #555;
    padding: 20px;
    min-width: 140px;
    text-align: center;
    font-size: 18px;
    border-radius: 5px;
}
.linkbox a {
    text-decoration: none;
    color: #0ff;
    display: block;
    width: 100%;
    height: 100%;
}
.linkbox:hover {
    background-color: #333;
}
.subbtn {
    border-radius: 5px;
    width: 100px;
    height: 50px;
    background-color: #222;
    color: #0ff;
    font-size: 18px;
}
</style>
</head>
<body>
<DIV id="main" align="center" valign="middle">
<TABLE width="60%" cellspacing="5px" cellpadding="0">
<tr><td colspan="3" align="center"><h2>User Login</h2></td></tr>
<tr><td colspan="3"><a href="index.php">Home</a> -> Login
<p>Please enter your email and password to login.</p>
</td></tr>
<?php if ($message): ?>
<tr><td colspan="3"><p class="error"><?= $message ?></p></td></tr>
<?php endif; ?>

<?php if (!isset($_SESSION['user_id'])): ?>
<form method="POST" action="">
<tr valign=top>
    <td align="right"><label for="email">Email:</label></td>
    <td><input type="email" id="email" name="email" required value="<?=$email ?>"></td>
    <td rowspan=2><span class="comment">
    <span class="comment">(Yeah, we know — entering your email and password again isn't the most thrilling part. But hey, it's a small step before conquering the planet!)</span>    
    </td>
</tr>
<tr valign=top>
    <td align="right"><label for="password">Password:</label></td>
    <td><input type="password" id="password" name="password" required></td>
    
</tr>
<tr>
    <td colspan="3" align="right"><button class="subbtn" type="submit">Login</button></td>
</tr>
</form>
<tr>
    <td colspan="3" align="center">
        <p>Or:</p>
        <p class="comment">If you did not yet registered in this page, then you can do it now, or try out the demo page without registration..</p>
        <div class="linkbox-container">
            <div class="linkbox">
                <a href="newuser.php">REGISTER</a>
            </div>
            <div class="linkbox">
                <a href="login.php?demo=1">DEMO</a>
            </div>
        </div>
    </td>
</tr>
<?php else: ?>
<tr>
    <td colspan="3" align="center">
        <p>Logged in as: <strong><?= htmlspecialchars($_SESSION['user_id']) ?></strong></p>
        <form method="POST" action="">
            <button class="subbtn" type="submit" name="logout">Kijelentkezés</button>
        </form>
    </td>
</tr>
<?php endif; ?>
</TABLE>
</DIV>

<!-- Facebook login link (később implementáljuk) -->
<!--
    <hr> 
    <h3>Bejelentkezés közösségi fiókkal</h3>
    <a href="login_facebook.php">Facebook fiókkal</a>
-->
<!-- Később: Google / Apple / GitHub stb. -->
</body>
</html>