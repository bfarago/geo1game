<?php
  include_once 'db.php';
  session_start();
  $session_id = session_id();

  $logged_in = false;
  if (isset($_SESSION['user_id'])) {
      $stmt = $pdo->prepare("SELECT * FROM users WHERE id = ? AND session_id = ?");
      $stmt->execute([$_SESSION['user_id'], $session_id]);
      $current_user = $stmt->fetch();
      if ($current_user) {
          $logged_in = true;
      } else {
          session_destroy();
      }
  }
?>
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <link rel="stylesheet" href="g.css">
  <title>GEO 1 game</title>
</head>
<body>
<DIV id=main align=center valign=middle>
<TABLE width=20%><TR><TD align=center>
  <h1>GEO</h1>
</td></tr><TR><TD align=center width=600px>
  <?php if ($logged_in): ?>
  <B>Logged in as <?php echo htmlspecialchars($current_user['nick']); ?></B><br>
  <a href="user.php">User admin</a> | <a href="logout.php">Logout</a>
<?php else: ?>
  <a href="login.php">Login</a> | <a href="newuser.php">Register</a> | <a href="login.php?demo=1">Demo user</a>
<?php endif; ?>
  </TD></TR><TR><TD align=center>
  <h2>The latest application</h2>
  <p class=comment>This is the latest development version of the application. Integrated landscape and globe view is there.</p><br>
  <a href="globe2.php"><img src=i/enter.png></a><br>
  <p class=comment>
  <p>Welcome to GEO – Your New World Awaits!</p>

<p class="comment">
GEO is a next-generation planet simulation and strategy game that combines cutting-edge 3D graphics with a deeply dynamic world.
In GEO, you don’t just watch a world evolve — you live and shape it.
</p>

<p class="comment">
Our simulation engine faithfully renders the position of the star (sun), orbiting satellites, a realistic atmosphere, and dynamic cloud systems.
You will witness planes, ships, and a wide range of ground-based entities moving across the planet’s surface — all in real-time.
</p>

<p class="comment">
As a player, you’ll engage in resource trading and manage your influence over the globe.
Build your network, expand your economy, and adapt to the ever-changing environment.
Every resource counts — and timing is everything.
</p>

<p class="comment">
Behind the scenes, our servers run a powerful native-code engine designed specifically for GEO:
</p>
<ul class=blquote>
<li>A massive terrain generator produces realistic planetary landscapes.</li>
<li>A dedicated entity manager handles fleets, vehicles, and trade routes.</li>
<li>Every simulation element — from a drifting cloud to a massive cargo fleet — is generated and managed live.</li>
</ul>

<p class="comment">
Whether you dream of commanding an air fleet, building a maritime empire, or trading precious commodities across continents,
GEO offers you a rich and immersive platform to explore your strategic genius.
</p>

<p>This is more than a game.<br>
This is your world, your story.</p>

<p><b>Join us — and shape the future of GEO!</b></p>
<p class="comment">This text was just a marketing bullshit, actuall this page is just a test page for a game under development...
  </p><br>
  </td></TR>
  <TR><TD align=center>
    <br><br><br><br><br><br><br><br><br>
  <h2>Earlier contents</h2>
  </td></tr>
  <TR><TD align=center bgcolor=#222>
  <h3>Old shader test view</h3>
  <p class=comment>This is a test page for the old shader, which is not used in the main application.</p><br>
  <a href="globe.php"><img src=i/globe1.png width=100 height=100 /></a><br>
  </td></TR><TR><TD align=center>
  <h3>Terrain view</h3>
  <p class=comment>This is an earlier development version of the terrain visualization.</p><br>
  <a href="terrain.php"><img src=i/landscape.png width=120 height=100 /></a>
  </td></TR><TR><TD align=center bgcolor=#222>
  <h3>Native C web service</h3>
  <p class=comment>This is a test page for webservice, if it is running. :) </p><br>
  <a href="http://myndideal.com/geoapi/"><img src=i/globe1.png width=120 height=110 /></a><br>
  </td></TR><TR><TD align=center>
  <h3>Source</h3>
  <p class=comment>This is an earlier development version of the source.</p><br>
  <a href="http://github.com/bfarago/geo1game">The github page.</a>
 </TD></TR></TABLE>
</DIV
</body>
</html>
