<?php

require_once 'db.php'; // include DB connection logic

$message = '';
$email = '';
$nick = '';

?>
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>Registration</title>
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
a:link {
  color: #0ff;
}

a:visited {
  color: #08f;
}

a:hover {
  color: #0f0;
}

a:active {
  color: #f00;
}
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
  </style>
</head>
<body>
<?php

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $email = trim($_POST['email']);
    $password = $_POST['password'];
    $confirm = $_POST['confirm'];
    $nick = trim($_POST['nick']);

    $emaillimitmax = 90;
    $emaillimitmin = 4;
    $nicklimitmax = 20;
    $nicklimitmin = 3;
    $passwordlimitmax = 50;
    $passwordlimitmin = 6;

    if (strlen($email) > $emaillimitmax) {
        $message = "Email is too long. It shall be less than $emaillimitmax characters.";
    } elseif (strlen($email) < $emaillimitmin) {
        $message = "Email is too long. It shall be at least $emaillimitmax characters.";
    } elseif (strlen($nick) > 20) {
        $message = "Nickname is too long. Shall be less than $nicklimitmax characters.";
    } elseif (strlen($nick) < $nicklimitmin) {
        $message = "Nickname is too short, shall be at least $nicklimitmin characters.";
    } elseif (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
        $message = "Invalid email address. Check it again.";
    } elseif (strlen($password) < $passwordlimitmin) {
        $message = "Password must be at least $passwordlimitmin characters.";
    } elseif (strlen($password) > $passwordlimitmax) { 
        $message = "Password must be at least $passwordlimitmax characters.";
    } elseif ($password !== $confirm) {
        $message = "Passwords do not match. Please write the same password twice.";
    } else {
        $stmt = $pdo->prepare("SELECT id FROM users WHERE email = ?");
        $stmt->execute([$email]);
        if ($stmt->fetch()) {
            $message = "Email already registered. Probably you have already registered, then try to <a href='login.php?email=$email'>login</a> in...";
        } else {
            $hash = password_hash($password, PASSWORD_DEFAULT);
            $stmt = $pdo->prepare("INSERT INTO users (email, password_hash, nick) VALUES (?, ?, ?)");
            $stmt->execute([$email, $hash, $nick]);
            $message = "Registration successful.";
            header("Location: user.php");
            exit;
        }
    }
}
?>
<DIV id=main align=center valign=middle>
<TABLE width=60% cellspacing=5px cellpadding=0><TR><TD align=center colspan=3>
<h2>User Registration</h2>
</td></tr><TR><TD align=left width=600px colspan=3>
<a href="index.php">Home</a> -> New user
<p>
Please fill in the form below to register!</p>
<p> We are uing your email address only to send a confirmation email.
(actually, we will not even send a registration email yet, due to this is a demo page...)</p>
<p>All the fields are required! Come on, there are only four fields. Don't be shy! Not even four, this is three fields! You know, the password is there twice...</p>

</TD></TR>
<TR><TD align=center colspan=3>
<?php if ($message): ?>
    <p class=error><?= $message ?></p>
<?php endif; ?>
</td></TR><TR><TD align=center>
<form method="POST" action="">
<TR><TD align=right bgcolor=#222 width=30%><label>Email:</td><td width=20% align=left bgcolor=#222><input type="email" name="email" required maxlength="<?= $emaillimitmax ?>" value="<?= htmlspecialchars($email) ?>"></label></td><td bgcolor=#222><spam class=comment>will be used when you login</span></TD></TR>
<TR><TD align=right><label>Password:</td><td><input type="password" name="password" required></label></td><td><spam class=comment>should be at least 6 characters long.</span></TD></TR>
<TR><TD align=right bgcolor=#222><label>Confirm Password:</td><td align=left bgcolor=#222><input type="password" name="confirm" required></label></td><td bgcolor=#222><spam class=comment>should be the same...</span></td></TR>
<TR><TD align=right><label>Nickname:</td><td><input type="text" name="nick" required maxlength="<?=$nicklimitmax; ?>" value="<?= htmlspecialchars($nick) ?>"></label></td><td><span class=comment>you can change it later.</span></td><td>
<tr><td colspan=3 align=right><button type="submit">Register</button></td></TR>
</form>
</TD></TR></TABLE>
</DIV
</body></html>