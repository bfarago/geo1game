# geo1game
This will be a space-civilization type of game.

## Documentation

For a technical overview of the map generation system, including structure and module responsibilities, see the [Map Generator Overview](doc/overview.md).

## Version 1.1
Server side implementation rewritten to native c for XenU VPS environment, where SIMD instruction are available. To prepare these steps, the scalar calculations are reorganized to be multiple operands for each phase.
A preliminary compute kernel is implemented, where N oerands are prepared to compute. Even that points which are not yet massively paralelized, shaped in this form to be ready to implement that way.
All of these works was done less in a weeek.

## Version 1.0
At the time of 2025 Q1, this is the very first version of the project. It was done just in one day (or less).

## Actually implemented:
- Linux server-side map and city generators, using SQLite database.
- JSON reporter PHP scripts, which query the database using the lat/lon regions.
- 3D visualization using the Three.js engine and shaders:
  - Elevation bumpmap
  - Biome-related colors
  - Day/night cycle
  - City lights visible at night
  - Edge of the atmosphere depending on star position
- 2D map (very preliminary)

I will probably rewrite all of the code later completely. :)

---

### Screenshots

![s1](doc/s1.png)
![s2](doc/s2.png)
![s3](doc/s3.png)

