#pragma once
// engine.hpp: the types, constants, shared variables and function declarations of the Synca engine.
// The engine itself lives in engine_*.cpp (one file per area); the Geode hooks live in hooks_*.cpp.
// Variables are `inline`, so every file sees the same one.
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/SliderNode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <map>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>
#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <climits>
#include <cmath>
#include <complex>
#include <deque>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

#include "synca.hpp"

namespace synca {
    constexpr int HOOK_PRIORITY = -1000000;
    constexpr size_t PANEL_LINES = 4;
    constexpr int SPIDER_LAND = 12;  // v2.15: ticks on a surface after a spider input = safe

    constexpr uint64_t typeBit(GameObjectType type) {
        return 1ull << static_cast<int>(type);
    }

    // Objects the ghost always collides with.
    // v2.61: back to the v2.48 set (v2.49 left CollisionObject out; cube and wave got less consistent)
    constexpr uint64_t COLLIDING_TYPES = typeBit(GameObjectType::Solid) | typeBit(GameObjectType::Hazard) | typeBit(GameObjectType::Slope) |
                                         typeBit(GameObjectType::CollisionObject) | typeBit(GameObjectType::AnimatedHazard);

    constexpr uint64_t HAZARD_TYPES = typeBit(GameObjectType::Hazard) | typeBit(GameObjectType::AnimatedHazard);

    // Objects the ghost may also interact with when smart prediction is on.
    constexpr uint64_t INTERACT_TYPES =
        typeBit(GameObjectType::YellowJumpPad) | typeBit(GameObjectType::PinkJumpPad) | typeBit(GameObjectType::RedJumpPad) |
        typeBit(GameObjectType::GravityPad) | typeBit(GameObjectType::SpiderPad) | typeBit(GameObjectType::YellowJumpRing) |
        typeBit(GameObjectType::PinkJumpRing) | typeBit(GameObjectType::RedJumpRing) | typeBit(GameObjectType::GravityRing) |
        typeBit(GameObjectType::GreenRing) | typeBit(GameObjectType::DropRing) | typeBit(GameObjectType::CustomRing) |
        typeBit(GameObjectType::DashRing) | typeBit(GameObjectType::GravityDashRing) | typeBit(GameObjectType::SpiderOrb) |
        typeBit(GameObjectType::InverseGravityPortal) | typeBit(GameObjectType::NormalGravityPortal) |
        typeBit(GameObjectType::GravityTogglePortal);

    // Portals that change too much state to simulate: reaching one ends prediction as "safe".
    constexpr uint64_t STOP_TYPES =
        typeBit(GameObjectType::ShipPortal) | typeBit(GameObjectType::CubePortal) | typeBit(GameObjectType::BallPortal) |
        typeBit(GameObjectType::UfoPortal) | typeBit(GameObjectType::WavePortal) | typeBit(GameObjectType::RobotPortal) |
        typeBit(GameObjectType::SpiderPortal) | typeBit(GameObjectType::SwingPortal) | typeBit(GameObjectType::DualPortal) |
        typeBit(GameObjectType::SoloPortal) | typeBit(GameObjectType::TeleportPortal) | typeBit(GameObjectType::TeleportOrb);

    // v2.79: with "Predict through portals" these switch the ghost's mode instead of
    // ending the prediction (dual / solo / teleport still end it).
    constexpr uint64_t MODE_TYPES =
        typeBit(GameObjectType::ShipPortal) | typeBit(GameObjectType::CubePortal) | typeBit(GameObjectType::BallPortal) |
        typeBit(GameObjectType::UfoPortal) | typeBit(GameObjectType::WavePortal) | typeBit(GameObjectType::RobotPortal) |
        typeBit(GameObjectType::SpiderPortal) | typeBit(GameObjectType::SwingPortal);

    // v2.8: size portals are applied to the ghost instead of ending the prediction.
    // (Mirror portals only flip the screen, so the ghost simply ignores them.)
    constexpr uint64_t SIZE_TYPES = typeBit(GameObjectType::RegularSizePortal) | typeBit(GameObjectType::MiniSizePortal);
    bool hasType(uint64_t mask, GameObject* object);
    float speedPortalValue(int id);




    std::string stageText();
    std::string stageShortText();
    std::string forecastText();
    void drawRealTrail();
    void ghostAudit(PlayerObject* real, PlayerObject* ghost);
    void deathSnapshot(PlayLayer* layer, float percent);
    int fullWaveHorizon(bool mini, bool alternate);
    void sendButton(PlayLayer* layer, bool down);
    float stepScale();
    int st(int ticks240);
    int updateAlternatePhase(bool wasDown, int observedTicks);
    bool alternatePhaseStable(bool wasDown);

    struct AlternatePhaseForecast {
        std::array<int, 7> intervals{};
        int count = 0;
    };
    AlternatePhaseForecast alternatePhaseForecast(bool wasDown);
    void resetAlternateRhythm();

    struct AlternatePredictionScope {
        int previous, previousDown, previousUp;
        std::array<int, 7> previousDownPattern, previousUpPattern;
        int previousDownCount, previousUpCount;
        AlternatePredictionScope(int period, int down, int up,
            AlternatePhaseForecast const& downPattern = {}, AlternatePhaseForecast const& upPattern = {})
          : previous(buf.altPredictionEvery), previousDown(buf.altPredictionDownEvery), previousUp(buf.altPredictionUpEvery),
            previousDownPattern(buf.altPredictionDownPattern), previousUpPattern(buf.altPredictionUpPattern),
            previousDownCount(buf.altPredictionDownPatternCount), previousUpCount(buf.altPredictionUpPatternCount) {
            buf.altPredictionEvery = period;
            buf.altPredictionDownEvery = down;
            buf.altPredictionUpEvery = up;
            buf.altPredictionDownPattern = downPattern.intervals;
            buf.altPredictionUpPattern = upPattern.intervals;
            buf.altPredictionDownPatternCount = downPattern.count;
            buf.altPredictionUpPatternCount = upPattern.count;
        }
        ~AlternatePredictionScope() {
            buf.altPredictionEvery = previous;
            buf.altPredictionDownEvery = previousDown;
            buf.altPredictionUpEvery = previousUp;
            buf.altPredictionDownPattern = previousDownPattern;
            buf.altPredictionUpPattern = previousUpPattern;
            buf.altPredictionDownPatternCount = previousDownCount;
            buf.altPredictionUpPatternCount = previousUpCount;
        }
    };

    // v2.4 moving-object guard: last known position of each object the ghost collided with.
    struct Watched {
        double x;
        double y;
        int movedTick;
        double vx = 0.0;  // v2.27: movement per physics step while it's moving
        double vy = 0.0;
        double pvx = 0.0;  // v2.68: the step before (curved paths: how the direction turns)
        double pvy = 0.0;
        double rot = 0.0;  // v2.68: rotation, and how much it turns per step
        double vrot = 0.0;
    };
    constexpr int MOVER_TICKS = 120;
    constexpr size_t WATCH_LIMIT = 4096;
    inline std::unordered_map<GameObject*, Watched> watched;

    // v2.4 drift check: the recorded ghost run for your last input and how far you strayed.
    struct Drift {
        std::vector<CCPoint> path;
        bool active = false;
        int index = -1;
        int compared = 0;
        float maxErr = 0.f;
        int maxAt = 0;
    };
    inline Drift drift;
    ccColor3B lineColor(std::string const& line);
    void refreshPanel();

    // Log line -> in-game panel history, plus the Windows console if enabled.
    // (Formatting a short string is cheap; it was the synchronous console writes
    //  that hitched, and those still only happen with the console setting on.)
    // v2.29: the full log (not just the 8 lines on screen) for the F8 export, with the
    // physics step each line happened on.
    constexpr size_t EXPORT_LINES = 400;
    inline std::deque<std::string> exportLines;
    inline int logClock = 0;  // physics steps since the level started, kept by the update hook
    void sayLine(std::string const& msg);
    bool isGhost(PlayerObject* p);
    void resetPending();
    void readSettings();

    // v2.76 auto budget: the budget setting is used as it is, and only cut (to 4 ms) while
    // the game is already dropping frames. v2.75 also capped every click at 45% of a frame:
    // at 240+ fps that was 2 ms, too short for a long cube jump's search (Penumbral's first
    // jump: only +1..+5 delays were ever tried, so most clicks got no save).
    // v1.9 watchdog: three clicks in a row that took far longer than the budget (a dense spot, a weak PC) cut the budget to 3 ms
    // for the next ten seconds, so the game keeps its frame rate instead of stalling on every click.
    inline int watchdogUntil = 0;
    inline int watchdogStrikes = 0;
    void clickBudget();
    void applyStage();
    void refreshWatermark();
    void refreshAutoTag();
    void updateProgressStageBand(PlayLayer* layer);

    size_t memorySpots();
    void exportConsole();
    std::string stageText();
    void dropGhosts();
    PlayerObject* makeGhost(PlayLayer* layer);
    void sweepGhostLayer();
    bool gameHolds(PlayerObject* player);

    // Everything that belongs to ONE player's input stream. (Settings, ghosts and UI are shared.)
    struct InputSlot {
        bool realInput = false, appliedInput = false, isPlayer1 = false;
        std::deque<QueuedInput> queue;
        int shift = 0, releaseShift = 0;
        int sinceInput = 999, rapid = 0, altRapid = 0;
        std::array<int, 7> altDownSamples{}, altUpSamples{};
        int altDownSampleCount = 0, altUpSampleCount = 0, altDownSampleNext = 0, altUpSampleNext = 0;
        int altDownEvery = 7, altUpEvery = 7, altWaveDelay = 0;
        bool autoReleased = false, autoClicked = false, swallowRelease = false, autoDash = false;
        int autoClickAge = 0;
    };
    inline InputSlot slotP2;
    void swapInputSlot(InputSlot& s);

    // While one of these exists, `buf` holds Player 2's stream; the destructor puts Player 1's back.
    struct P2Context {
        explicit P2Context(PlayLayer* layer) {
            g_p2.active = true;
            swapInputSlot(slotP2);
            if (g_p2.needsReset) {
                g_p2.needsReset = false;
                buf.queue.clear();
                buf.shift = 0;
                buf.releaseShift = 0;
                buf.realInput = buf.appliedInput = gameHolds(layer->m_player2);
                buf.isPlayer1 = false;
                resetAlternateRhythm();
                buf.rapid = 0;
                buf.sinceInput = 999;
                buf.autoReleased = buf.autoClicked = buf.swallowRelease = buf.autoDash = false;
                buf.autoClickAge = 0;
            }
        }
        ~P2Context() {
            swapInputSlot(slotP2);
            g_p2.active = false;
        }
        P2Context(P2Context const&) = delete;
        P2Context& operator=(P2Context const&) = delete;
    };
    bool p2OwnBuffer(PlayLayer* layer);
    void syncHeld(PlayLayer* layer);
    void sendButton(PlayLayer* layer, bool down);
    void pressGhost(PlayerObject* ghost, bool down);
    void copyState(PlayerObject* ghost, PlayerObject* real);

    // Your recent input timings, so predictions model what you actually do instead of
    // an infinite hold: cube taps (holding auto-re-jumps) and robot holds (they set the
    // jump height).
    constexpr int HOLD_SAMPLES = 8;

    struct HoldStats {
        int samples[HOLD_SAMPLES] = {};
        int count = 0;
        int index = 0;

        void add(int ticks) {
            samples[index] = std::clamp(ticks, 1, 240);
            index = (index + 1) % HOLD_SAMPLES;
            count = std::min(count + 1, HOLD_SAMPLES);
        }

        // percentile of the recent samples, or `fallback` with no samples yet
        int pick(int fallback, int percent) const {
            if (count == 0) {
                return fallback;
            }
            int tmp[HOLD_SAMPLES];
            std::copy(samples, samples + count, tmp);
            int at = std::min(count - 1, count * percent / 100);
            std::nth_element(tmp, tmp + at, tmp + count);
            return tmp[at];
        }
    };

    inline HoldStats tapHold, robotHold;

    enum class InputKind { Tap, Wave, Robot, Other };
    inline int pressTick = -1;
    inline InputKind pressKind = InputKind::Other;
    InputKind kindOf(PlayerObject* p);
    void recordRealInput(bool down, PlayerObject* p1);
    void forgetPress();
    int holdEstimate();
    void watchObject(GameObject* object);

    // v2.41: objects that moved on the last step, so predictions don't have to search the
    // whole watch list for them.
    inline std::vector<GameObject*> movingNow;
    void updateWatched();

    // v2.27 moving-object prediction. Objects near your path that are moving right now are
    // moved along their current motion during a prediction (where they'll be k steps
    // later), and put back exactly afterwards. Straight-line movement only: easing,
    // rotation and toggles still aren't predicted.
    // v2.68: also curved paths (rotate triggers move objects around a center, Orbit's saws):
    // when the direction of motion turned between the last two steps, it keeps turning by the
    // same angle each step, and objects that spin keep spinning. Still put back exactly after.
    struct Mover {
        GameObject* object;
        double x, y, vx, vy;
        double turn = 0.0;  // radians the motion direction turns per step (0 = straight)
        double rotX = 0.0, rotY = 0.0, vrot = 0.0;  // both axes kept as they were (skewed objects)
        double ax = 0.0, ay = 0.0;  // v2.75: speeding up / slowing down (easing), per step
        int accelSteps = 0;         // v2.75: how long the easing is followed (until it would stop / over 3x speed)
    };
    inline std::vector<Mover> movers;
    void collectMovers();
    void placeMovers(int steps);
    void finishDrift(char const* why);
    void driftStep(PlayerObject* real);

    // v2.13 level memory. Each level is cut into half-block slices along x. Every buffer
    // decision is remembered at the slice where it was made, and ~0.4 s later it is
    // scored: still alive = it worked, died = it didn't. Over your runs this learns, per
    // spot, which delays work there, where saves keep failing (the ghost doesn't match
    // the level there: triggers, moving objects) and where you keep dying. Stored per
    // level in the mod's save folder, so it carries over between sessions.
    constexpr float SLICE = 15.f;          // half a block
    constexpr int OUTCOME_TICKS = 90;      // alive this long after a decision = it worked

    struct Spot {
        uint32_t ok = 0;        // saves here that you survived
        uint32_t bad = 0;       // saves here followed by a death
        uint32_t deaths = 0;    // deaths after any decision here
        uint32_t delaySum = 0;  // sum of the delays of the saves that worked
        uint32_t last = 0;      // v2.79: the delay of the last save that worked here (240/s), 0 = none
    };

    inline std::unordered_map<int, Spot> memory;
    inline std::string memoryKey;
    inline bool memoryDirty = false;
    inline int memoryUpdates = 0;

    struct PendingOutcome {
        bool active = false;
        int slice = 0;
        int tick = 0;
        int delay = 0;
        bool skipped = false;  // v2.64: a save was held back here because of memory
    };
    inline PendingOutcome pending;

    // Fun tab state (defined here because decided() records the assist delay into it).
    inline float frontierBest = 0.f;   // best level % this visit (the near-best extra buffer follows it)
    int sliceOf(PlayerObject* p);
    Spot spotAround(int slice);
    std::filesystem::path memoryFile();
    size_t memorySpots();
    void saveMemory();
    void loadMemory(GJGameLevel* level);
    void memoryChanged();
    void creditPending();
    void memoryNote(PlayerObject* p1, int delay);
    void memoryTick();

    bool memorySaysSkip(int slice);
    void memoryDeath();
    bool memorySaysSkip(int slice);
    int memoryDelay(int slice);
    float memoryHardestX();
    bool memoryRisky(int slice);
    void snapshotObject(GameObject* object);

    // v2.88: anything a ghost added to the real level during a prediction (the dash start
    // effect of dash orbs, bursts...) is removed afterwards. Dash orbs still flashed before
    // you reached them: those effects go onto the level's layers, not the ghost's own.
    struct NodeMark {
        CCNode* node;
        unsigned count;
    };
    inline std::vector<NodeMark> levelMarks;
    void markLevelNodes(PlayLayer* layer);
    void sweepLevelNodes();
    void restoreObjects();
    void advanceGhost(PlayerObject* ghost, float dt);
    void checkGhost(PlayLayer* layer, PlayerObject* ghost, float dt);
    void setGhostMode(PlayerObject* ghost, GameObjectType type);
    bool isFlying(PlayerObject* player);
    void releaseAutoClicks();

    enum class SimEnd { Ok, StopSearch };
    int simulate(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int releaseAfter, int stopAt, SimEnd* end);

    // v2.4: simulate() with the orb setting; if that run touched an orb and didn't make
    // it, also try the other choice (click it / skip it) and keep whichever lives longer,
    // like a player who only clicks the orb when they need it.
    // v2.70: when the current click started being processed.
    // v2.79: the whole click shares its time: each search used to get the full budget on its
    // own, on top of the base run, memory try and orb checks before it, so a wave save on
    // GRIEF could take 2-3 frames at 240 fps. Orb retries stop once the click is over time.
    inline std::chrono::steady_clock::time_point clickStart;
    inline bool clickTimed = false;
    double clickMs();
    bool clickOverTime();
    double searchBudget();

    struct ClickTimer {
        ClickTimer() {
            clickStart = std::chrono::steady_clock::now();
            clickTimed = true;
        }
        ~ClickTimer() {
            clickTimed = false;
        }
    };
    int simulateOrbs(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int releaseAfter, int stopAt, SimEnd* end);
    std::vector<CCPoint> recordRun(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int releaseAfter, bool* died = nullptr, bool traceThroughHits = false, int* collisionAt = nullptr);
    void drawPath(std::vector<CCPoint> const& path, int delay, ccColor4F color, int collisionAt = -1, CCDrawNode* node = nullptr);

    // v5.12 route view. The path used to draw every alternative (do nothing, click now, buffered click) side by side
    // and kept drawing past the crash. Now ONE route is shown: the one the level wants, meaning the input that gets
    // through the look-ahead. Doing nothing wins if it survives, otherwise the earliest click that survives, and if
    // nothing survives the route that lasts longest. It stops at the crash (red dot) instead of running on.
    struct RouteChoice {
        std::vector<CCPoint> path;
        int delay = 0;
        int hit = -1;
        bool idle = true;
        bool survives = false;
        float clear = 1000.f;   // closest the route comes to a death object (units)
    };

    // v1.8 hazard scan: every death object (spikes, saws ...) of the level, sorted by x, collected once per level.
    // The route view uses it to prefer the route that keeps the most room and to mark what it is steering around.
    inline std::vector<CCPoint> hazardPts;
    inline std::vector<CCPoint> solidPts;   // blocks and slopes: the corridor walls the route keeps away from
    inline bool hazardsScanned = false;
    void ensureHazards(PlayLayer* layer);
    float routeClearance(std::vector<CCPoint> const& path);
    RouteChoice chooseRoute(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, bool down, int horizon, int releaseAfter, int maxDelay, double limitMs);
    void showPaths(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int chosen, bool down, int horizon, int releaseAfter);
    void startDrift(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int releaseAfter);
    void decided(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int releaseAfter);
    int fullWaveHorizon(bool mini, bool alternate = false);
    int alignOrb(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int horizon, int releaseAfter, int learned, int maxDelay, int* landed);

    // v2.1: best delayed run seen by the last search (shown in the "no save" log line)
    inline int dbgBestK = 0;
    inline int dbgBestRet = -1;
    bool probeGap(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int& holdLife, int& idleLife);

    // v2.7 wave scoring, the wave version of the cube's "landed safely". A wave never
    // lands, so a run that keeps going one way eventually hits a wall even in a normal
    // corridor, where you'd simply turn. So: if the run crashes, try turning back a bit
    // before the crash; if that turn survives, you had time to react and the run counts
    // as safe. Only crashes you couldn't have turned away from count as deaths.
    // v2.10: three turn points are tried (4, 8 and 12 ticks before the crash) instead of
    // one, so "you could have turned away" is found whenever it's true.
    constexpr int WAVE_TURN_MARGINS[] = { 4, 8, 12 };
    constexpr int WAVE_MIN_TURN = 6;      // a turn sooner than this after the input is too fast to count
    constexpr int WAVE_ESCAPE = 16;       // the turn must then survive this many ticks
    int waveScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int target, SimEnd* end);

    // v2.7 wave search, same rules as the cube: the smallest delay that is safe (or
    // clearly better than now) wins, so turns are only held back as long as needed.
    // v2.12 robot scoring. A robot press is a jump whose height you set by when you let go,
    // so "pressing now crashes" only if NO release time saves it. Your usual hold is tried
    // first, then short to long holds. (Before, only your usual hold was tested, so a press
    // that needed a shorter or longer hold looked like a death and got delayed for nothing.)
    constexpr int ROBOT_HOLDS[] = { 6, 14, 26, 45 };
    inline int robotUsedHold = -1;   // hold length that made the last robot run survive

    // v2.73: quick pass of a robot / UFO / spider search: only the plain run per delay (your
    // usual robot hold, no extra flap / flip). Those modes tried up to 5x more runs per
    // delay, so the search ran out of time after 2-10 delays and missed saves further out.
    inline bool quickScore = false;
    int robotScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, int horizon, int target, SimEnd* end);

    // v2.15 spider scoring, like the wave's: if the run crashes, tap again (flip back) a
    // little before the crash; if that survives, you could have reacted, so it's safe.
    constexpr int SPIDER_RETAP[] = { 4, 8, 12 };
    constexpr int SPIDER_MIN_RETAP = 4;
    int spiderScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, int horizon, int releaseAfter, int target, SimEnd* end);

    // Same rules as the cube: the smallest delay that is safe (or clearly better) wins.
    // v2.72: the crash happens at the same moment however late the input comes (delay k
    // dies k ticks sooner, for the first SAME_CRASH_TRIES delays): it's the next obstacle's,
    // not this input's, so the rest of the search can't help. Robot and UFO searches spent
    // their whole time budget on exactly that, on almost every input.
    constexpr int SAME_CRASH_TRIES = 4;
    inline bool crashFixed = false;  // the last search stopped for that reason
    inline bool longDelayBypass = false;   // a long cube / ball delay is set: the same-crash early-out is off for this click
    inline bool bandsPausedByHand = false;   // you picked a stage by hand: the level-% bands leave it alone until the next level

    // v1.12 forecast error: how far the alternate assist's learned interval was from the interval you really made
    struct ForecastStats {
        int n = 0;
        long absSum = 0;   // sum of |actual - forecast| in 240/s ticks
        long sum = 0;      // sum of (actual - forecast): + means you are slower than forecast
        int last = 0;
    };
    inline ForecastStats altForecast;

    // v1.12 real trail: where the real player was, and how far from the ghost's prediction (path view overlay + deviation colours)
    struct TrailPoint {
        CCPoint pos;
        float err;   // -1 = no prediction to compare with
    };
    inline std::deque<TrailPoint> realTrail;
    bool sameCrash(int k, int ret, int base, int& streak);
    int delayStep(int k);
    int nextDelay(int k, int maxDelay);
    int searchSmallest(int horizon, int maxDelay, int base, int* landed, std::function<int(int, int, SimEnd*)> const& score, double budget = -1.0, bool quiet = false, bool needFull = false);
    int searchTwoPass(int horizon, int maxDelay, int base, int* landed, std::function<int(int, int, SimEnd*)> const& score, bool needFull = false);

    // v2.25 ship scoring. Like the wave: a crash only counts if you couldn't have recovered.
    // Ship has momentum, so recovery (the opposite input) is tried earlier before the crash:
    // 8, 16 and 24 ticks.
    constexpr int SHIP_RECOVER[] = { 8, 16, 24 };
    int shipScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, bool down, int horizon, int target, SimEnd* end);

    // v2.40 UFO scoring. One flap and then nothing falls for the whole look-ahead into
    // whatever is below, which your next flap would have avoided. So if the run crashes,
    // try one more flap 6 / 12 / 18 ticks before the crash; if that survives, you could have
    // flapped, so the click counts as safe. Crashing up into something can't be fixed by
    // flapping, so those stay real crashes.
    constexpr int UFO_REFLAP[] = { 6, 12, 18 };
    int ufoScore(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, int delay, int horizon, int releaseAfter, int target, SimEnd* end);

    inline bool marginUsed = false;  // the last wave save took the +1 tick safety margin
    int searchWave(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, bool down, int horizon, int maxDelay, int base, int* landed, bool requireFullSurvival = false, int preferredDelay = 0);
    int searchAlternateWave(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, bool down, int horizon, int maxDelay, int base, int preferredDelay, int* landed, int smoothTarget = -1);
    int searchDelay(PlayLayer* layer, PlayerObject* p1, PlayerObject* p2, bool dual, bool down, bool anyWave, int horizon, int maxDelay, int releaseAfter, int base, int* landed, bool needFull = false, int preferredDelay = 0);

    // v2.75 run check (test bench). Every attempt played from the level start is recorded
    // (the inputs the game got, and where you were on every physics step); the longest one
    // per level is kept. "Check run" replays it on the hidden ghost at the start of your next
    // attempt and reports how long the ghost follows the recording. Run it after an update:
    // if the ghost stops matching much earlier than before, the update changed the physics
    // copy. Moving objects and triggers aren't replayed, so it's a comparison between
    // versions, not a percentage that should reach 100.
    struct BenchRun {
        float startX = 0.f, startY = 0.f, dt = TICK;
        std::vector<std::pair<int, bool>> inputs;
        std::vector<CCPoint> track;
    };
    inline BenchRun benchLive;
    inline bool benchRecording = false;
    inline bool benchNeedStart = false;  // next P1 step is the first of an attempt
    inline bool benchPending = false;    // "Check run" was pressed
    inline std::string lastBench;        // last result, for the stats popup
    std::filesystem::path benchFile();
    size_t benchSavedTicks();
    bool benchLoad(BenchRun& r);
    void benchStart(PlayLayer* layer, PlayerObject* p1);
    void benchInput(bool down);
    void benchStep(PlayerObject* p1);

    // end of an attempt: keep it if it's the longest run from the start so far
    // v2.77: the best run is kept in memory and only written when you leave or finish the
    // level; writing it on every new-best death made a hitch right when you died.
    inline BenchRun benchBest;
    inline size_t benchBestTicks = 0;   // longest run known for this level (memory or file)
    inline bool benchBestDirty = false;
    void benchFinish();
    void benchFlush();
    void benchCheck(PlayLayer* layer, PlayerObject* p1);
    void livePath(PlayLayer* layer, PlayerObject* p1);
    void queueInput(int ticks, bool down);

    // v2.51 practice stats: every save (or, in training, every input that would have been
    // saved) is how early you were, in 240/s ticks.
    struct Stats {
        int saves = 0;
        int earlySum = 0;
        int attempts = 0;            // attempts that had at least one save
        std::map<int, int> savesAt;  // level percent -> saves there
        // Alternate Assist is player-edge driven. Keep separate decisions so a
        // run log shows whether it re-timed an edge or passed it through.
        int altEdges = 0;
        int altAdjusted = 0;
        int altNoGain = 0;
        int altMarginRejected = 0;
        int altPortalPass = 0;
        int altDelaySum = 0;
    };
    inline Stats attemptStats;
    inline Stats sessionStats;
    inline std::string statsKey;

    enum class AlternateResult { Adjusted, NoGain, MarginRejected, PortalPass };

    struct AltEdge {
        int tick;
        AlternateResult result;
        int delay;
    };
    inline std::deque<AltEdge> altRecent;   // the last few alternate-assist decisions, for the death report
    void noteAlternateResult(AlternateResult result, int delay = 0);
    void altDeathReport(float percent);
    void noteSave(PlayLayer* layer, int ticks);
    std::string attemptSummary();
    void endAttempt();
    bool trainInstead(PlayLayer* layer, int ticks, bool down, char const* what);

    // v2.51 per-level settings: your switches (v2.59: not the stage, not low latency) are remembered for each level and
    // come back when you open it again. Outside a level (and in levels you haven't changed
    // anything in) the normal settings apply.
    constexpr char const* PROFILE_KEYS[] = { "robust-saves", "orb-align", "orb-timing", "level-memory",
        "more-time-death-spots", "ship-buffer", "ufo-smart", "ball-smart", "swing-smart", "cbf-sync", "predict-moving-objects",
        "training-mode" };

    struct Globals {
        bool have = false;
        std::vector<bool> values;
        std::string setup;   // v1.6: the whole setup (delays, assists, tuning) as it was outside the level
    };
    inline Globals globals;
    std::string profileKey();
    void saveProfile();
    void restoreGlobals();
    void loadProfile();
    void recoverGlobals();
    void refreshHeatmap();
    std::string statsText();
    bool intercept(GJBaseGameLayer* self, bool down, bool isPlayer1);
    void cycleStage(int dir);

    // v2.4 wave auto-release (practice mode only): while you hold in wave, check every
    // tick whether holding crashes within the next `autoWindow` ticks. If it does and
    // letting go now clearly survives longer, let go for you. Your own release later is
    // swallowed, and your next press works as normal.
    // Auto assists (rewritten). All of them follow the "last moment" rule: the ghost looks a short way ahead; if
    // carrying on with what the game is doing now would crash, it checks whether switching NOW saves you and
    // whether switching AUTO_MARGIN ticks LATER still would. The switch is made at the last moment where it
    // still works (with a couple of ticks to spare), so your own input is left alone for as long as possible
    // and a save is as late (a jump as high) as it can be.
    constexpr int AUTO_MARGIN = 2;

    // The wave auto-release's plan: the tick to switch on (chosen for room, not just the last moment).
    inline int autoCooldownUntil = 0;

    struct AutoPlan {
        bool valid = false;
        bool toggleTo = false;
        int actTick = 0;
        int replanAt = 0;
        int slack = 0;
        int life = 0;
    };
    inline AutoPlan autoPlan;
    void tryAutoRelease(PlayLayer* layer, PlayerObject* p1);
    void tryAutoClick(PlayLayer* layer, PlayerObject* p1);
    void updateAutoTagFade();
    void updateNextModeHud(PlayLayer* layer);
    void tryShipGuard(PlayLayer* layer, PlayerObject* p1);
    void endFrameSpam(PlayLayer* layer, char const* why);
    void tickFrameSpam(PlayLayer* layer, PlayerObject* p1);
    void deliverPending(PlayLayer* layer, PlayerObject* player);
    std::string levelHardestText();
    void forgetLevelMemory();
    void requestRunCheck();
    MenuTelemetry sessionTelemetry();
    void applyHudLayout();
    void refreshOrbDot();
}  // namespace synca
