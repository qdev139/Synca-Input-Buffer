// ---------------------------------------------------------------------------------------------
// Input Buffer Studio additions, part 2: hotkey actions.
//
// Included from main.cpp INSIDE namespace synca, right after cycleStage().
//   toggle key : buffer off <-> back to the stage you were on
//   panic key  : buffer off right now, raw input, never turns itself back on
// ---------------------------------------------------------------------------------------------

// STAGES[] ends with the OFF stage; the per-gamemode stage is the one flagged `custom`.

void studioNotify(std::string const& text, bool warn = false) {
    Notification::create(text, warn ? NotificationIcon::Warning : NotificationIcon::None, 1.f)->show();
}

// Same steps cycleStage() takes, but to an exact stage.
void studioSetStage(int stage) {
    buf.stage = std::clamp(stage, 0, STAGE_COUNT - 1);
    Mod::get()->setSavedValue("stage", buf.stage);
    if (studio.progressStageBands && buf.stage < STUDIO_STAGE_OFF) {
        studio.progressBaseStage = buf.stage;
        Mod::get()->setSavedValue("studio-progress-base-stage", static_cast<int64_t>(buf.stage));
    }
    resetPending();
    // A delayed input that was waiting is dropped; the game gets what you're really holding now
    auto* layer = GameManager::get()->m_playLayer;
    if (layer && !buf.autoReleased && buf.appliedInput != buf.realInput) {
        sendButton(layer, buf.realInput);
    }
    applyStage();
    readSettings();
    saveProfile();
    say("stage -> {}", stageText());
    refreshAutoTag();
    refreshWatermark();
    refreshPanel();
}

void studioRememberStage() {
    if (buf.enabled) {
        studio.resumeStage = buf.stage;
        Mod::get()->setSavedValue("resume-stage", static_cast<int64_t>(studio.resumeStage));
    }
}

void studioToggle() {
    if (buf.enabled) {
        studioRememberStage();
        studioSetStage(STUDIO_STAGE_OFF);
        studioNotify("Buffer OFF");
    }
    else {
        int back = studio.resumeStage;
        if (back < 0 || back >= STUDIO_STAGE_OFF) {
            back = STUDIO_STAGE_CUSTOM;
        }
        studioSetStage(back);
        studioNotify(stageText());
    }
}

void studioPanic() {
    studioRememberStage();
    studioSetStage(STUDIO_STAGE_OFF);
    studioNotify("PANIC: buffer OFF, raw input", true);
}

