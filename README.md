# Synca

**An input buffer for Geometry Dash (Geode, Windows).**

Synca keeps a hidden "ghost" copy of your icon. Before the game sees a click or a release, the ghost tests it, and the
next few ticks after it. If clicking right now would get you killed but a slightly later click would not, the input is held
back by exactly that long. If the click is already safe, it goes through untouched.

> **Assist tool.** Synca changes the timing of inputs you make. Do not use it for records, leaderboards or any
> submission where assists are not allowed.

## Features

- **Per-gamemode delay** for cube, ship, ball, UFO, wave, robot, spider and swing, up to 120 ticks each (4 ticks = 1 frame
  at 60 FPS), or the quick 0.5f / 1f / 2f ... stage presets. Dual mode and two-player levels are handled.
- **Frame-perfect jumps:** with a long cube or ball delay set, every tick of the window is tested, so a click made far too
  early is held until the exact frame.
- **Orb alignment** and per-orb timing (up to 120 ticks).
- **Alternate assist** for fast wave, ship, ball, UFO and swing spam: learns your rhythm and corrects single edges.
- **Straight fly**, **ship guard** and **frame spam** (starts when your own CPS reaches a number you choose).
- **Path view:** the route the ghost expects, with hazards, portals and a trail of where you really flew.
- **Graph tab:** the rhythm the alternate assist learned, its forecast error, and an osu-style calibration.
- **Level memory**, an adaptive buffer, per-level settings, presets, and a setup code you can share.
- **Menu:** themed, searchable, with a forecast while you type; socials and changelog panels; welcome slides.
- **Auto-update:** a popup offers to install new GitHub releases for you (see [UPDATING.md](UPDATING.md)).

## Install

1. Install [Geode](https://geode-sdk.org) for Geometry Dash 2.2081.
2. Download `dd3v.synca.geode` from the latest release.
3. Put it in `Geometry Dash/geode/mods/` and start the game.

## Using it

- Open the menu from the pause menu, or with its key (rebind it in **Keybinds**).
- Pick a stage with `<` `>` in the pause menu or the menu header. **PER MODE** uses the delays from the Gamemodes tab.
- **Studio > CHECK MY SETUP** tells you which of your settings cancel each other or cause lag.
- Turn on **Explain inputs** to see why a click was or was not moved.

Auto assists (auto-release, frame spam, ship guard) are for Practice mode, or for normal runs if you switch on
**Allow in normal runs**. A tag marks runs that used them.

## Building

You need the Geode SDK, CMake 3.21+ and a C++20 compiler (Visual Studio on Windows).

```sh
geode build

Source layout: src/engine.hpp and the engine_*.cpp files are the prediction engine, hooks_*.cpp are the game hooks, menu.cpp with the .inc files is the menu, popups.cpp the welcome slides and update popup. See BUILD.md for more.

Credits
Synca's prediction engine started out as a solo single-file project. It was later rewritten and extensively expanded with the help of Claude, evolving into a full system with per-gamemode delays, orb alignment, alternate assist, path view, an entire menu system, and more.

Click Between Frames (CBF) by syzzi is a separate mod; Synca only detects it and can sync with it.

License
Under GitHub's default copyright terms, projects published without an explicit open-source license retain standard copyright protections—meaning all rights are reserved by the author. You hold full ownership of Synca; if you plan to open-source or distribute it, consider attaching an explicit license (e.g., MIT, Apache 2.0, or GPL) to define how others can use your code.
