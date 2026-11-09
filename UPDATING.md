# How updates reach players

Synca checks GitHub for a newer release when the game starts (at most every 6 hours) and shows an **Update** popup.
Pressing **Update** downloads the new `.geode` from the release, checks it, swaps it in, and asks the player to restart.

## One-time setup
1. In the mod settings set **Update source** to your repository, written `owner/repo` (for example `DD3V/synca`).
2. Optional: fill in the **Socials** links (GitHub, YouTube, TikTok, Discord) for the last welcome slide.

## Releasing a new version
1. Bump the version in `mod.json` and `CMakeLists.txt` (they must match), and add a section to `changelog.md`.
2. Commit, then tag it with a `v` in front: `git tag v1.13.0` and `git push --tags`.
3. The workflow in `.github/workflows/build.yml` builds the mod on that tag. Create a GitHub **release** for the tag and
   attach the built `dd3v.synca.geode` file. The file name must be exactly the mod id plus `.geode`.
4. Players on an older version see the popup the next time they start the game. **Settings > Studio > UPDATES > CHECK NOW**
   runs the same check immediately, which is handy to test a release.

## Notes
- The tag must be a version number (`v1.13.0` or `1.13.0`); only a higher version than the installed one triggers the popup.
- The download uses `curl.exe`, which ships with Windows 10 and 11.
- A player who dismisses a version is not asked about it again; the next release asks again.
