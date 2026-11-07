SYNCA - project files (everything except the images)
=====================================================

Folder layout to rebuild on the laptop:

  synca/                      <- the project folder (name it like your old one)
    CMakeLists.txt
    mod.json
    about.md  changelog.md  BUILD.md
    .github/workflows/build.yml
    src/                      <- all the C++ files (main.cpp, menu.cpp, menu_button.cpp, popups.cpp,
                                 synca.hpp, synca_state.hpp, studio_ui.inc, studio_actions.inc)
    resources/                <- NOT IN THIS ZIP (see below)
    logo.png                  <- NOT IN THIS ZIP, copy it from the old project if you have one

IMPORTANT: the images are not in this zip.
I only ever had the code, never your resources folder. Copy the whole `resources` folder (and logo.png) from
the old PC / your repository. mod.json loads resources/*.png. These are the image names the code asks for:

  mode-ball.png
  mode-cube.png
  mode-robot.png
  mode-ship.png
  mode-spider.png
  mode-swing.png
  mode-ufo.png
  mode-wave.png
  nav-all.png
  nav-basic.png
  nav-gamemodes.png
  nav-keybinds.png
  nav-orbs.png
  nav-smart.png
  nav-visuals.png
  orb-black.png
  orb-blue.png
  orb-dash-green.png
  orb-dash-pink.png
  orb-green.png
  orb-pink.png
  orb-red.png
  orb-spider.png
  orb-yellow.png
  sparkle.png
  synca-mark.png
  synca-slider-fill.png
  synca-slider-groove.png
  synca-slider-thumb-selected.png
  synca-slider-thumb.png

If one is missing the mod still loads, but that icon / slider piece will be blank.

After copying:
  1. Delete any old `build` folder.
  2. Make sure GEODE_SDK is set on the laptop.
  3. `geode build`

Remove from the old src folder if still there: studio_state.inc (its content is now synca_state.hpp).
