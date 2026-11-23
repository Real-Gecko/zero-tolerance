# Changelog

## 1.0.1

### Music

- Music now plays on an emulated YM2612 sound chip, as on the console, instead of through MIDI
  and a SoundFont. FluidSynth is no longer needed. Volume levels match the DGen emulator.

### Controls

- Keyboard: the original pad is the arrows, Z / X / C (A / B / C) and Enter (Start).
- Keyboard and mouse PC controls, in gameplay only: W / S move, A / D strafe, mouse turns,
  Space jumps, Left Ctrl toggles crouch, left click fires, right click switches items, Tab
  shows the map, Escape opens the pause menu.
- Gamepad: D-pad and South / West / East are the original pad's D-pad and A / B / C. In
  gameplay, the left stick moves and strafes, the right stick turns, the left bumper jumps,
  the right bumper toggles crouch, the right trigger fires, the left trigger switches items,
  Select shows the map and Start opens the pause menu.
- Jumping works while moving, and crouching is a toggle.

### Fixes

- Doors closing behind the player no longer flash in front of the view or on a nearby wall.
- The view no longer jitters while riding a lift.
- The view no longer jumps for a moment when stairs or a lift take the player to another floor.

## 1.0

- First release.
