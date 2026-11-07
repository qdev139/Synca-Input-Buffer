# Synca

Synca checks every press and release against a hidden "ghost" copy of your icon before the game sees it. If the input right now would get you killed but a few ticks later would not, it is delayed by exactly that long. Otherwise it passes through untouched.

> **Assist tool.** Synca adjusts the timing of inputs you make. Do not use it for records, leaderboards or submissions where assists are not allowed.

## Tabs
Basic, Smart, Visuals, Gamemodes, Orbs, Keybinds, All, **Path** (route view), **Assist** (alternate assist tuning), **Straight fly** (gap centering and ship guard), **Frame spam**, Studio and **Fun** (20 optional extras). The tab list scrolls.

## Features
- **Per-gamemode delay** for cube, ship, ball, UFO, wave, robot, spider and swing, or one of the 2f / 4f / 8f stage presets. In dual mode each icon's own limit applies, and in 2-player levels Player 2 has its own buffer (own queue, rhythm and delay).
- **Ship guard**: in a level range you choose (for example 35 - 45 for the ship part of Tidal Wave) the ship is switched at the last safe moment when holding or letting go would hit a wall.
- **Gap centering** for straight flying through 1-block gaps: it measures how far you are from the middle and delays the edge that would push you toward a wall.
- **Alternate assist** (when several corrections survive, it takes the one with the most timing room; gap centering aims the turns of your spam at symmetric points) for fast wave spam: learns your rhythm and re-times single edges, with a safety margin, click bias and a configurable look-ahead.
- **Orb alignment** with timing slack, a green / yellow / red confidence dot, and learned timings per spot.
- **Auto assists** (Studio tab): predict moving hazards (switches on by itself when the level scan finds movers), wave / robot / dash auto-release, auto-click, and **frame spam**: in a 1-block wave gap it turns your uneven spam into a perfectly even one, centred in the gap. Auto inputs work in Practice, or with "Allow in normal runs" (the AUTO INPUTS ON tag then shows).
- **Auto assists rewritten (last-moment rule)**: wave auto-release works both ways (it can press again), robot / dash auto-release and auto-click act at the last moment where the save still works; predicted moving hazards now really reach the ghost.
- **Even zigzag**: each edge waits just long enough that the phases keep the length your rhythm has, so the spam comes out even instead of "ziiig zag".
- **Two-edge planning**: when no single delay saves an edge, the assist also tries delaying the next edge, and applies the first delay of the pair that survives.
- **Level scan** (retries until the level's objects exist, with a RESCAN button in the Studio tab): when a level loads, Synca counts its objects and move / toggle triggers in small slices (no hitch, even on huge levels). A level with move triggers switches on moving-hazard prediction, and the solids and hazards near you are watched so their motion is known in time.
- **Gamemode timeline**: the level scan reads every portal and lists the level as stretches ("41%  WAVE 3x mini (busy)"), shows where you die most, and can show the next gamemode while you play ("NEXT: WAVE 3x in 1.2s").
- **Trajectory view**: where you end up if you keep your input, plus the route of the last delayed click.
- **Portal prediction** for speed, size, gravity and mode portals; anything it cannot predict ends the prediction instead of guessing.
- **Spam-safe input queue**: events keep their order and each gets its own physics step, so tiny taps are never lost.
- **Progress bands**: switch stage by level percentage, including PER MODE.
- **Studio tab**: colour themes, HUD positions, six preset slots, adaptive buffer and an optional performance guard.

## Notes
- Open the menu with the key you set (default 7), from the pause menu button, or from the floating Synca button (it shows on every screen, drag it anywhere, it fades out after 3 seconds and comes back when you touch where it was).
- Tick delays are per game mode; 0 turns a mode off. 4 ticks = 1 frame at 60 FPS.
