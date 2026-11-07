# v1.5.1
- Fixed the white blocks on cards, buttons and switches (see-through colours were drawn solid); the glow on the circuit lines and the sparkles are round and softer
- Bigger logo and signature in the header

# v1.5.0
- New look: near-black to violet gradient panels with a pink-to-dark rim, a soft glow, drifting light rays and sparkles, and thin circuit lines with a light running along them; the Synca wordmark in the header
- Animated: glow and rays, a light that travels along the circuit lines, a shine that sweeps across the selected tab, glowing ON switches, pages that slide in, a glow behind the logo and the floating button
- Cards, buttons, number boxes and switches share the new gradient style; themes were retuned for the new colours

# v1.4.0
- Menu: the tab list is bigger and scrolls (mouse wheel, drag or the scrollbar); the settings pages have scrollbars too
- New tabs: Assist (assist tuning), Straight fly (with a new Ship guard), Frame spam, Fun; Path is its own tab
- Gamemodes: every mode goes up to 120 ticks (the wave stopped at 64)
- Frame spam: you choose the clicks per second (default 16, it no longer jumps to 30+); a gap only limits it when it is too small
- Wave auto-release reworked: it plans the best frame (the one with the most timing room) instead of only the last moment, and tells you when a normal run blocks it
- Ship guard: switches the ship at the last safe moment inside a level range (for the ship part of Tidal Wave)
- Path view draws what you hold now; the expected spam is optional
- CBF status now shows what your clicks really do (mid-tick or not), so it also reacts to Mega Hack's CBF
- Assist tuning: even zigzag %, plan width, safe choices kept and room levels are settings now
- Fun tab: 20 optional extras (CPS, timers, death history, next busy part, spam meter, sounds, flashes, rainbow text, zen mode ...)

# v1.3.0
- Gap centering for 1-block gaps (optional): pulls you back toward the middle instead of just looking further ahead
- Alternate wave assist: best-correction search, Wave look is exact again
- Glossy rounded menu with colour themes; terminal restyled to match
- Studio tab: HUD positions, 6 preset slots, click bias, orb margin
- Orb confidence dot and a wider orb timing check
- Adaptive buffer and optional performance guard
- Progress bands: 4 bands, any stage (including PER MODE), +/- buttons
- Search finds settings on every tab and jumps to them
- Dual mode uses each icon's own delay; input queue gives every event its own step
- Trajectory view on by default, last delayed click stays on screen
- Removed the unfinished Move detect / Orb detect test options
- Per-level memory for the adaptive buffer, keybind conflict warning, cheaper path drawing
- 2-player levels: Player 2 now has its own buffer with its own mode / stage delay (switch in Studio)
- Popups moved into their own source file (first step of splitting main.cpp)
- Build: the menu is its own source file (menu.cpp) with shared headers, so main.cpp is smaller
- Built-in presets (Safe, Balanced, Aggressive, Performance) that configure every setting; type % values for progress bands; all number boxes use the key-plate style; bigger Orbs tab; TPS box fixed
- Path finding: two-edge planning for the alternate wave assist (optional), level scan that finds moving / toggled hazards and tracks movers early
- Frame spam (CPS cap) for 1-block wave gaps; Auto assists section in Studio; moving hazards predicted automatically when the level scan finds movers
- Bigger Gamemodes tab with a link switch; bigger Keybinds tab that turns duplicate keys red; Safe / Balanced / Aggressive buttons for the alternate-assist numbers
- Fixes: moving-hazard prediction (movers reach the ghost, no more veto), wave / robot / dash auto-release and auto-click rewritten, frame spam works without the alternate assist and sizes its spacing to the gap, Escape no longer restarts the closing animation
- Level scan now keeps looking until the level's objects exist (it used to run once, too early) and has a RESCAN button; the AUTO INPUTS ON tag can be dimmed (25% minimum)
- Crash fix: the level scan no longer reads per-object group data (it crashed on huge levels like Shadow Tidal Wave); it now counts in short slices and finds movers through the game's own nearby-object lists
- Gap centering and frame spam now aim the turns of the spam at symmetric top / bottom points instead of the exact middle (much earlier and stronger); alternate assist picks the correction with the most timing room; gap centering defaults are 100% strength / 1 tick dead zone
- The Synca button is now floating on every screen (not just the title screen), fades out after 3 seconds; the AUTO INPUTS ON tag is brightest for 3 seconds after an auto input and then settles to its set opacity (25% at the least)
- Auto tag option: show the AUTO INPUTS ON text only for the first 3 seconds of an attempt, while paused and on the end screen
- "Hide the AUTO INPUTS ON text" switch in the Studio tab
- Level scan: gamemode timeline from the portals (mode, speed, mini, dual, busy stretches), where you die most, and an optional "NEXT: ..." gamemode display while playing
- Alternate assist: "Keep the zigzag even" (edges wait so the phases keep the learned length; the correction search prefers the evenest safe delay)
