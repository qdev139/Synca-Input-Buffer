# v1.17.0
- Removed the floating Synca button (top right of the title screen and menus) completely; open the menu with its key or from the pause menu
- Welcome slides: the logo, the star behind it and the lightning logo are 1024 px renders now (they were upscaled 128 px pictures), and the current page dot has a real soft glow
- Path view: the route keeps clear of the walls too (blocks and slopes, not only spikes) and compares more routes, so it stops taking the dip into a side pocket
- UFO: with the alternate assist on, a machine-gun flap section only has to last longer than pressing now (it used to need the whole look-ahead and so buffered nothing), and a flap gets at least 3 ticks of room

# v1.16.2
- Studio > WELCOME > REPLAY: plays the welcome slides, the lightning and the menu again, no file editing needed

# v1.16.1
- Text never runs off the screen: lines are now broken by measuring them (welcome slides and the changelog), instead of the font wrap that was unreliable
- Changelog: bigger text, short lines, more space between entries
- Socials: the credit line is gone, and the pictures are the sites own icons on menu-style plates

# v1.16.0
- Two drop-down panels at the top middle of the menu: SOCIALS (YouTube, TikTok, Discord, GitHub, clickable pictures with their names) and CHANGELOG (scrollable, with ADDED / FIXED / REWORKED tags). They slide in and out with a back animation; opening one closes the other. Set the links in the mod settings (Socials)
- Sliders: the groove is at the back, the glowing fill above it, the line handle on top
- Search box: bigger text, a plate in the menu style, no stray rim
- Menu music loops and continues where it stopped (it pauses when the menu closes instead of restarting)
- Welcome slides: the GIF is gone; a big pulsing Synca logo with a breathing glow instead (the package is smaller). The slides show once more
- NOTICE.md: credits for the engine (based on Click Before Frames by BASU)

# v1.15.0
- Path view: long enough for the whole delay window (a click held up to 120 ticks, plus the whole jump after it, up to 320 ticks), and its route search covers the window every 1-2 ticks with a longer time budget when a long delay is set
- Long delays: with a cube / ball delay of 12+ frames every single tick of the window is tested (it used to jump 4 ticks at a time after the first 4 frames, which could skip the 1-2 tick window of a frame-perfect jump)

# v1.14.2
- Package size: the logo frames are plain files now (the game made three copies of every picture); 560 px frames, 143 of them. The mod is about 9 MB instead of 18 MB
- Lightning: one sound for each of the two lightning strikes, then the appearing sound
- Menu music: opening the Synca menu fades the game's menu music out and the Synca music in (looping); closing fades it back. Never inside a level. Setting: "Menu music"
- Sliders: the library's bar was hidden behind the groove, so the glowing fill is drawn by Synca itself now (purple core, halo, white highlight) with the white line handle

# v1.14.1
- Fixed: the sounds were never packed into the mod (mod.json only listed png files); lightning, appearing and intro now are
- Logo animation: all 143 frames of the GIF at 640 px, a cleaner background cut (the white zigzag in the logo square stays, no white blobs, the ink and sparkles are light line art), only the current frame stays in memory
- Sliders: a long white-hot line with a soft glow (like the scrollbar thumb) on a glowing purple bar
- The search box has a dark plate with a purple rim in the style of the menu

# v1.14.0
- The welcome slides show your animated logo on the left (72 frames converted from the startup GIF, white background removed, the black ink turned light so it shows on the dark screen)
- Sounds: lightning.ogg plays with the lightning, appearing.ogg when the logo appears after it (instead of the old explosion), and intro.ogg when the menu opens (at most once every 90 seconds; switch it off with "Menu intro sound")

# v1.13.2
- Sliders: the handle is now a glowing line (purple) on the glowing bar; the scrollbar thumb is white with a soft glow
- Manual calibration is now osu style: a ring shrinks onto a target once a second, tap TAP the moment it lands; after 12 taps it shows how early or late you are and APPLY sets Click bias. The buttons fit inside the card
- Welcome slides: the card with the logo is gone; the left side is glowing 3D ribbons with beads and dashes that fade out at the ends. Bigger text, correctly wrapped; buttons without the broken edge; the current page dot glows. The slides show once more

# v1.13.1
- The floating Synca button no longer shows inside a level (playing or paused); the pause menu has its own button
- Welcome slides redone to fit the screen (smaller titles and buttons, nothing overlapping or cut off). They show once more after this update
- Stage switch: choosing a stage by hand (menu, pause menu, key) now pauses "Auto switch by level %" until the next level, so your stage is never switched back to a band stage (such as 8F) behind your back. The terminal says when this happens

# v1.13.0
- Fixed: the stage switch snapped back (for example to 0.5f) when "Auto switch by level %" was on: the bands forced their stage every step. They now only act when you enter or leave a band, so a stage you pick by hand stays
- Path view: much lighter in wave and ball (the route search has a few milliseconds at most, and the refresh rate backs off by itself); mode / speed / size portals on the route are marked with cyan diamonds
- The "Synca was on" text on the end screen follows "Hide watermark" (it still shows at a custom TPS)
- Both panels scroll with the mouse wheel; new scrollbars (drag the thumb or click the track) replace the old, broken one. If the wheel goes the wrong way, Studio > "Invert scroll wheel"
- The All tab is replaced by a search box at the top of the right panel, with a grey forecast of what you are typing (click it to take it)
- New Graph tab: the rhythm the alternate assist learned (down / up bars), the forecast error, and manual calibration (tap along with a beat; it can set Click bias for you)
- Gamemode icons are your own icons from the game in your own colours; orbs are the game's own orb art (no glow)
- New icons for every tab; new glowing purple sliders; the gamemode and orb sliders snap to whole frames (the number box and +/- still go tick by tick)
- First launch: four welcome slides (welcome, menu look, preset, socials), then a lightning intro that opens the menu. The old welcome and what's new popups are gone
- Update popup: when a newer GitHub release exists it offers to download and install it for you (set "Update source" in the settings; see UPDATING.md). Studio > UPDATES > CHECK NOW tests it

# v1.12.0
- Forecast error tracking: the alternate assist now measures how far its learned rhythm is from the interval you really made. The Fun tab evenness line shows it (FORECAST off 1.2t, bias +0.3t), and CHECK MY SETUP turns it into advice (uneven spam, or a click bias to set)
- Path view: the last two seconds you really flew are drawn as a trail coloured by how close the ghost predicted it (green exact, yellow a little off, red wrong, white no prediction)
- Ghost state audit (Explain inputs on): once a second the ghost is compared with the real player and a mismatch is logged
- death-log.txt in the save folder: one line per death with the level, percent, mode, stage, what the assist did with your last click and the forecast error
- mod.json: the repeated sentence in the keybind descriptions is fixed

# v1.11.3
- Removed the stage < > buttons from the F9 terminal
- The stage readout and < > in the gameplay pause menu now follow the Synca menu in real time (and the other way round): change the stage in either and the other updates within a moment

# v1.11.2
- Stage switch: "My delays on every stage" is switched off once for everyone (it had been on by default and made the stage look stuck on PER MODE). The stage readout now always names the stage, and says so when your gamemode delays are used on top of it

# v1.11.1
- The panel resize handles are gone from the menu. The F9 terminal has one instead: a small handle in its bottom right corner, drag it left to right to resize the terminal (the size is remembered)
- Fixed the stage switch showing only PER MODE: "My delays on every stage" is off by default again, so the stages (0.5f, 1f, 2f ...) cycle and show normally. Turn it on in the Studio tab to use your gamemode delays on every stage

# v1.11.0
- Long-delay bypass (Studio, on by default): with a cube / ball delay of 12+ frames, a click made far too early (the jump lands, then walks into the spike) is no longer passed through as "the next obstacle's crash", and the search no longer gives up after 4 delays. The whole range, up to 120 ticks, is searched in frame steps with a longer time budget, so the click is held until the jump is right
- The terminal (F9) has the stage < > buttons in its header
- The panel resize handles drag left to right only, grow each panel from its bottom outer corner, and can no longer leave the screen

# v1.10.0
- Fixed: the per-gamemode delays were ignored on every stage except PER MODE. On a preset stage such as 8F, every mode stayed at that stage's ticks (32) however high the sliders were set. New switch "My delays on every stage" (Studio, on by default) makes the gamemode delays, up to 120 ticks, apply on every stage. The setup check warns when a preset stage is hiding your delays
- Frame spam: new "Gap use per phase (%)" slider (default 12). A low value keeps each up / down phase short, so in a one-block gap (3x speed wave) it flies a straight line like Straight fly instead of zigzagging
- Two drag handles, at the bottom right of the left panel and the bottom left of the right panel, resize each panel (the size is remembered)
- Code: main.cpp (6,700 lines) is split into engine.hpp (types, constants, shared state, declarations), seven engine_*.cpp files, four hooks_*.cpp files and actions.cpp, so the engine compiles in parallel

# v1.9.0
- Fixed the multi-second freeze when auto wave release met a fast alternation (10 CPS): it ran on every physics step with no time limit and planned dozens of simulations over and over. It now stays out of a fast spam you are already handling, has a 2.5 ms budget per step, and skips a few steps when it runs out
- Fixed the lag with "Predict moving hazards": only moving objects near you are followed, and only the 40 nearest are placed on each simulated step
- Orbs: the "Orb timing" sliders and "Orb alignment: max delay" go up to 120 ticks, and the alignment reach follows the gamemode delay, so the first orbs of a level like Tidal Wave are moved onto even when they are far ahead. Set the orb timings back to 0 to use the automatic search
- Watchdog: three clicks in a row that take far too long cut the prediction budget to 3 ms for ten seconds
- New CHECK MY SETUP button (Studio tab): tells you, in plain words, which of your settings cancel each other or cause lag or late clicks, including what the alternate assist really did this session
- The Synca wordmark is 60% bigger and a little further right
- Aggressive preset updated

# v1.8.0
- Safe-window centring: a cube / ball save no longer sits on the edge of its safe window; when the window is wide enough it moves toward the middle (at most 1 frame later). Switch: Studio > Assist tuning > "Centre saves in the safe window"
- Alternate wave: new "Survive timing jitter" switch. A correction must also work when your next edge comes 1 tick early or late than forecast (the usual reason a correction fails on dense spam)
- Alternate assist death report: dying within a second of an assist decision logs what the assist did on its last edges (console + alt-deaths.txt in the save folder)
- Yellow route in the path view: it scans the level's death objects, prefers the route with the most room and marks the objects it passes
- Pause menu stage switch follows the pause button setting; the Synca wordmark is 45% bigger
- The menu opens on the tab you used last
- Code: the level scan (level_scan.cpp) and the Fun tab (fun_hud.cpp) are separate files, so they compile in parallel with the engine

# v1.7.0
- Removed the menu blur
- Fun tab: the prediction line also says why a click got no save (time ran out, same crash at any timing, max delay too small); the recap after a death shows what the assist did with your last click
- Long delay ranges get a longer time budget; the route view bisects to the smallest working click delay, fades toward the far end and stops at the crash
- Frame spam: new switch "Also spam UFO / ball / swing" (no gap to centre in, just an even spam at the chosen rate)
- Per-level settings now remember the whole setup (delays, assists, tuning), not just the switches, and put the global setup back when you leave the level
- Setup code: COPY / PASTE buttons in the Studio tab share your whole setup as text
- Near-best extra buffer: a few more ticks of room while you are within 3% of your best % (Studio > Assist tuning)
- Pause menu: stage readout with < > buttons (bottom right)

# v1.6.0
- Path view shows one route: the one the level wants (doing nothing if it survives, else the earliest click that survives), cut at the crash. No more alternative lines
- Frame spam: two sliders, "Start when I click (CPS)" and "Then spam at (CPS)"; the assist takes over once your own click rate reaches the first number
- Menu blur: the game behind the menu is blurred (Studio > Effects > "Blur behind the menu", on by default)
- Long delays (up to 120 ticks) are searched in frame steps after the first 4 frames, so the time budget no longer runs out around 8 frames
- Adaptive buffer: 1% buckets, grows after 2 deaths in +2 tick steps, max up to 32 ticks (default 12). Old adaptive data is reset
- Fun tab remade around the buffer: stage in use, assist delay, saves, save rate, average earliness, where saves happened, prediction time, buffered-click flash, recap after a death. Old Fun switches are reset

# v1.5.2
- Menu effects setting (Studio > Effects): CLEAN (gradient and rim only), NORMAL (default: glow, soft rays, thin circuit lines) and FULL (adds sparkles, a soft light passing along the lines, pulsing glows)
- The running dot on the circuit lines was replaced by a soft band of light, and the round line ends by a short tick
- The tab list is grouped (GENERAL, TUNING, ASSISTS, MORE) so it is clear where you are
- In-game path view: no more thick glow or square markers (colours are now drawn the way the game blends them, markers are round); the orb confidence dot too

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
