# Building Synca

Windows only. Put the project in a folder with `src/`, `resources/` (the images) and `mod.json`, then run
`geode build` in that folder (the Geode SDK must be set up: `GEODE_SDK`). The `.geode` file ends up in the build
folder and, if you have a Geode profile, in the game's mods folder.

## Publishing on GitHub (optional)
1. Put the project on GitHub. The workflow in `.github/workflows/build.yml` builds the mod on every push and keeps
   the `.geode` under Actions > Build output.
2. To make a release: set `"version"` in `mod.json` (for example `v1.3.1`), then push a tag with the same name:
   `git tag v1.3.1` and `git push origin v1.3.1`. The `.geode` is attached to a GitHub release. The update popup
   (mod setting "Update source" = `yourname/yourrepo`, and `SYNCA_UPDATE_CHECK` set to `1` in `popups.cpp`) reads it.
