// Synca's code-drawn control pages. Timing is controlled with a snapped slider
// and a linked numeric field so mouse dragging and exact value entry share state.

// The on/off switches of the auto assists (they are ordinary mod settings, also listed on the Smart tab).
constexpr char const* AUTO_TOGGLE_KEYS[6] = { "predict-moving-objects", "wave-auto-release", "robot-auto-release",
    "dash-auto-release", "auto-click", "auto-release-normal-runs" };

constexpr char const* KEYBIND_ACTION_LABELS[9] = { "Buffer on / off", "Panic raw input", "Change stage",
    "Open Synca menu", "Path view", "Console", "Copy log", "Forget level", "Wave direct input" };

// Name shown for a progress-band stage: 0..5 fixed delays, 6 per-mode, 7 off.
inline std::string bandStageName(int stage) {
    stage = std::clamp(stage, 0, STAGE_COUNT - 1);
    if (STAGES[stage].custom) return "PER MODE";
    if (!STAGES[stage].enabled) return "OFF";
    if (STAGES[stage].ticksAll == STAGES[stage].ticksWave) {
        return fmt::format("{:g}f", STAGES[stage].ticksAll * TICK);
    }
    return fmt::format("{:g}f / W{:g}f", STAGES[stage].ticksAll * TICK, STAGES[stage].ticksWave * TICK);
}

void BufferMenu::refreshSpecialPage() {
    // Keep direct entry fields and sliders in sync after presets, linked orb
    // changes, and settings reloads, not only while the user drags a thumb.
    if (m_tab == 3) {
        for (int i = 0; i < SM_COUNT && i < static_cast<int>(m_customSliders.size()); ++i) {
            m_customSliders[i]->setValue(static_cast<float>(studio.modeTicks[i]));
        }
    }
    else if (m_tab == 4) {
        for (int i = 0; i < SO_COUNT && i < static_cast<int>(m_customSliders.size()); ++i) {
            m_customSliders[i]->setValue(static_cast<float>(studio.orbTicks[i]));
        }
        for (int i = 0; i < 3 && SO_COUNT + i < static_cast<int>(m_customSliders.size()); ++i) {
            int value = i == 0 ? buf.waveReleaseWindow : i == 1 ? buf.robotReleaseWindow : buf.dashReleaseWindow;
            m_customSliders[SO_COUNT + i]->setValue(static_cast<float>(value));
        }
    }
    else if (m_tab == 7 || m_tab == 9 || m_tab == 10 || m_tab == 11) {
        std::vector<int> values;
        if (m_tab == 7) {
            values = { studio.pathHorizon, studio.pathRefreshMs, studio.altWaveHorizon };
        }
        else if (m_tab == 9) {
            values = { studio.altMaxTicks, studio.altSamples, studio.altTolerance, studio.altMargin, studio.altBurstGap,
                studio.altWaveLook, studio.altWaveMinGain, studio.altSmoothPct, studio.altPlanWidth, studio.altSafeKeep,
                studio.altRoomLevels };
        }
        else if (m_tab == 10) {
            values = { studio.sfStrength, studio.sfGap, studio.sfDead, studio.shipGuardFrom, studio.shipGuardTo };
        }
        else {
            values = { studio.spamCps };
        }
        for (size_t i = 0; i < m_customSliders.size() && i < values.size(); ++i) {
            m_customSliders[i]->setValue(static_cast<float>(values[i]));
        }
    }

    if (m_tab == 8) {
        int values[] = { studio.autoTagOpacity, studio.orbMargin, studio.swingLook, studio.adaptiveMax, studio.perfGuardMs };
        for (size_t i = 0; i < m_customSliders.size() && i < std::size(values); ++i) {
            m_customSliders[i]->setValue(static_cast<float>(values[i]));
        }
    }

    for (auto* toggle : m_specialToggles) {
        if (!toggle) continue;
        int tag = toggle->getTag();
        bool enabled = false;
        if (m_tab == 4 && tag == 200) {
            enabled = studio.orbTicksLinked;
        }
        else if (m_tab == 3) {
            if (tag == 201) enabled = studio.modesLinked;
        }
        else if (m_tab == 8) {
            if (tag >= 130 && tag <= 135) enabled = Mod::get()->getSettingValue<bool>(AUTO_TOGGLE_KEYS[tag - 130]);
            else if (tag == 136) enabled = studio.moversAuto;
            else if (tag == 138) enabled = studio.autoTagCompact;
            else if (tag == 139) enabled = studio.autoTagHidden;
            else if (tag == 140) enabled = studio.nextModeHud;
            else if (tag == 120) enabled = studio.orbConfOn;
            else if (tag == 121) enabled = studio.adaptiveOn;
            else if (tag == 122) enabled = studio.perfGuardOn;
            else if (tag == 123) enabled = studio.p2Buffer;
        }
        else if (m_tab == 12) {
            if (tag >= 300 && tag < 300 + FUN_COUNT) enabled = studio.fun[tag - 300];
        }
        else if (m_tab == 7 || m_tab == 9 || m_tab == 10 || m_tab == 11) {
            if (tag == 100) enabled = studio.altOn;
            else if (tag >= 101 && tag <= 105) {
                static constexpr int MODES[] = { SM_WAVE, SM_SHIP, SM_BALL, SM_UFO, SM_SWING };
                enabled = studio.altMode[MODES[tag - 101]];
            }
            else if (tag == 110) enabled = buf.showPath;
            else if (tag == 111) enabled = studio.pathShowIdle;
            else if (tag == 112) enabled = studio.pathShowClick;
            else if (tag == 113) enabled = buf.autoRelease;
            else if (tag == 114) enabled = studio.progressStageBands;
            else if (tag == 115) enabled = studio.sfOn;
            else if (tag == 116) enabled = studio.altPlan;
            else if (tag == 117) enabled = studio.altSmooth;
            else if (tag == 118) enabled = studio.pathForecast;
            else if (tag == 137) enabled = studio.spamOn;
            else if (tag == 141) enabled = studio.shipGuard;
        }
        updateToggleVisual(toggle, enabled);
    }

    if (m_tab == 8 && m_studioLabels.size() >= 15) {
        m_studioLabels[0]->setString(STUDIO_THEMES[std::clamp(studio.themeIndex, 0, THEME_COUNT - 1)].name);
        m_studioLabels[0]->limitLabelWidth(84.f, 0.50f, 0.30f);
        for (int i = 0; i < 6; ++i) {
            int value = studio.hudOff[i];
            m_studioLabels[1 + i]->setString(value == 0 ? "0" : fmt::format("{:+}", value).c_str());
            m_studioLabels[1 + i]->limitLabelWidth(44.f, 0.50f, 0.30f);
        }
        for (int i = 0; i < PRESET_SLOTS; ++i) {
            bool filled = !Mod::get()->getSavedValue<std::string>(fmt::format("studio-preset-{}", i + 1), "").empty();
            m_studioLabels[7 + i]->setString(filled ? "saved" : "empty");
            m_studioLabels[7 + i]->setColor(filled ? themeColor({ 150, 230, 170 }) : themeColor({ 150, 130, 150 }));
        }
        int bias = studio.clickBias;
        m_studioLabels[13]->setString(bias == 0 ? "0" : (bias > 0 ? fmt::format("late {}", bias) : fmt::format("early {}", -bias)).c_str());
        m_studioLabels[13]->limitLabelWidth(52.f, 0.46f, 0.28f);
        std::string scanText = levelScan.done ? levelScan.summary
            : (levelScan.total > 0 ? fmt::format("Scanning... {}%", static_cast<int>(100.0 * levelScan.pos / levelScan.total))
            : (levelScan.tries > 0 ? std::string("Waiting for the level objects...") : std::string("Open a level to scan it")));
        m_studioLabels[14]->setString(scanText.c_str());
        m_studioLabels[14]->limitLabelWidth(262.f, 0.42f, 0.22f);
        if (m_studioLabels.size() >= 28) {
            m_studioLabels[15]->setString(levelHardestText().c_str());
            m_studioLabels[15]->limitLabelWidth(262.f, 0.46f, 0.24f);
            auto const& sections = levelScan.sections;
            for (int line = 0; line < 12; ++line) {
                std::string text;
                if (!levelScan.done) {
                    text = line == 0 ? "The timeline appears after the scan" : "";
                }
                else if (sections.size() > 12 && line == 11) {
                    text = fmt::format("... and {} more", sections.size() - 11);
                }
                else if (line < static_cast<int>(sections.size())) {
                    text = sectionText(sections[line], levelScan.lastX);
                }
                auto* row = m_studioLabels[16 + line];
                row->setString(text.c_str());
                row->limitLabelWidth(262.f, 0.48f, 0.26f);
                bool busy = levelScan.done && line < static_cast<int>(sections.size()) && !(sections.size() > 12 && line == 11) &&
                    sections[line].busy;
                row->setColor(busy ? ccColor3B{ 255, 200, 140 } : ccColor3B{ 225, 240, 250 });
            }
        }
    }

    if (m_tab == 9) {
        m_syncingInputs = true;
        for (int band = 0; band < PROGRESS_BANDS && band * 2 + 1 < static_cast<int>(m_bandLabels.size()); ++band) {
            bool active = studio.bandStart[band] < studio.bandEnd[band];
            m_bandLabels[band * 2]->setColor(active ? themeColor({ 255, 238, 249 }) : themeColor({ 150, 130, 150 }));
            m_bandLabels[band * 2 + 1]->setString(bandStageName(studio.bandStage[band]).c_str());
            m_bandLabels[band * 2 + 1]->limitLabelWidth(84.f, 0.46f, 0.28f);
            for (int field = 0; field < 2 && band * 2 + field < static_cast<int>(m_bandInputs.size()); ++field) {
                std::string text = fmt::format("{}", field == 0 ? studio.bandStart[band] : studio.bandEnd[band]);
                auto* box = m_bandInputs[band * 2 + field];
                if (box->getString() != text) {
                    box->setString(text);
                }
            }
        }
        m_syncingInputs = false;
    }

    if (!m_keyLabels.empty()) {
        // Read every binding; a key that drives two actions turns red (and is named at the top).
        std::vector<std::string> bound;
        for (size_t i = 0; i < m_keyLabels.size(); ++i) {
            auto setting = std::dynamic_pointer_cast<KeybindSettingV3>(Mod::get()->getSetting(KEYBIND_KEYS[i]));
            std::string value = "Unbound";
            if (setting && !setting->getValue().empty()) value = setting->getValue().front().toString();
            bound.push_back(value);
        }
        m_keyWarning.clear();
        for (size_t i = 0; i < bound.size(); ++i) {
            bool duplicate = false;
            for (size_t j = 0; j < bound.size(); ++j) {
                if (i != j && bound[i] != "Unbound" && bound[i] == bound[j]) {
                    duplicate = true;
                    if (m_keyWarning.empty()) {
                        m_keyWarning = fmt::format("{} is used twice: {} and {}", bound[i], KEYBIND_ACTION_LABELS[i], KEYBIND_ACTION_LABELS[j]);
                    }
                }
            }
            m_keyLabels[i]->setString(bound[i].c_str());
            m_keyLabels[i]->limitLabelWidth(98.f, 0.62f, 0.34f);
            m_keyLabels[i]->setColor(duplicate ? ccColor3B{ 255, 96, 96 } : ccColor3B{ 178, 233, 244 });
        }
    }

    if (m_captureHint) {
        bool warn = m_captureKeyIndex < 0 && !m_keyWarning.empty();
        m_captureHint->setString(m_captureKeyIndex >= 0
            ? "Press a key now (Escape cancels)"
            : (warn ? m_keyWarning.c_str() : "Tap SET KEY, then press the new key"));
        m_captureHint->setColor(warn ? ccColor3B{ 255, 110, 110 } : ccColor3B{ 150, 190, 210 });
        m_captureHint->limitLabelWidth(SETTINGS_W - 20.f, 0.50f, 0.28f);
    }
}

void BufferMenu::buildModePage() {
    constexpr float CARD_W = SETTINGS_W - 8.f;
    constexpr float ROW_H = 40.f;      // bigger rows, like the Orbs tab
    constexpr float PITCH = 46.f;
    auto pagePoint = [&](float x, float y) {
        return CCPoint{ x - CONFIG_PANEL_X + SETTINGS_W / 2.f, MODE_PAGE_HEIGHT - y };
    };
    auto addPlate = [&](float y, ccColor4B color, float height) {
        auto* plate = gelPanel(CARD_W, height, color, 12.f);
        plate->setPosition(pagePoint(CONFIG_PANEL_X, y));
        m_customPage->addChild(plate, -1);
    };
    auto addLabel = [&](char const* text, float x, float y, float width, float scale, ccColor3B color) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setAnchorPoint({ 0.f, 0.5f });
        label->limitLabelWidth(width, scale, scale * 0.6f);
        label->setColor(color);
        label->setPosition(pagePoint(x, y));
        m_customPage->addChild(label, 1);
    };
    auto addSlider = [&](float x, float y, float width, int maximum, int value,
                         std::function<void(geode::SliderNode*, float)> callback) {
        constexpr float INPUT_W = 46.f;
        auto* input = numberInput(INPUT_W, "0", 3);
        auto* slider = styledSlider(width, 0.f, static_cast<float>(maximum),
            static_cast<float>(value), std::move(callback));
        slider->linkTextInput(input, 0);
        slider->setPosition(pagePoint(x, y));
        input->setPosition(pagePoint(1130.f, y));
        m_customPage->addChild(slider, 20);
        m_customPage->addChild(input, 21);
        m_customSliders.push_back(slider);
        m_customInputs.push_back(input);
    };

    static constexpr char const* MODE_SPRITES[SM_COUNT] = {
        "mode-cube.png"_spr, "mode-ship.png"_spr, "mode-ball.png"_spr, "mode-ufo.png"_spr,
        "mode-wave.png"_spr, "mode-robot.png"_spr, "mode-spider.png"_spr, "mode-swing.png"_spr
    };

    float y = 158.f;
    for (int i = 0; i < SM_COUNT; ++i) {
        addPlate(y, ccc4(52, 24, 53, 255), ROW_H);
        if (auto* icon = CCSprite::create(MODE_SPRITES[i])) {
            icon->setScale(1.05f);
            icon->setPosition(pagePoint(903.f, y));
            m_customPage->addChild(icon, 2);
        }
        addLabel(STUDIO_MODES[i].name, 930.f, y, 78.f, 0.58f, { 235, 245, 250 });
        addSlider(1062.f, y, 74.f, STUDIO_MODES[i].maxTicks, studio.modeTicks[i],
            [this, i](geode::SliderNode*, float value) { onModeSlider(i, value); });
        y += PITCH;
    }

    // link switch: one slider moves every gamemode together
    y += 8.f;
    addPlate(y, ccc4(66, 30, 62, 255), ROW_H);
    addLabel("LINK ALL MODES", 892.f, y, 150.f, 0.58f, { 245, 220, 236 });
    auto* link = toggleItem(201, menu_selector(BufferMenu::onModeLink), studio.modesLinked, 66.f, 30.f);
    link->setPosition(pagePoint(1118.f, y));
    m_customMenu->addChild(link);
    m_specialToggles.push_back(link);
    y += PITCH + 8.f;

    static constexpr char const* PRESETS[3] = { "SAFE", "BALANCED", "AGGRESSIVE" };
    addPlate(y, ccc4(62, 28, 60, 255), ROW_H + 6.f);
    for (int i = 0; i < 3; ++i) {
        auto* item = actionItem(PRESETS[i], menu_selector(BufferMenu::onModePreset), i,
            84.f, 30.f, ccc4(46, 56, 112, 255));
        item->setPosition(pagePoint(929.f + 91.f * i, y));
        m_customMenu->addChild(item);
    }
}

void BufferMenu::buildOrbPage() {
    constexpr float CARD_W = SETTINGS_W - 8.f;
    // each orb card is 40 tall (it used to be 26)
    constexpr float PITCH = 46.f;
    auto pagePoint = [&](float x, float y) {
        return CCPoint{ x - CONFIG_PANEL_X + SETTINGS_W / 2.f, ORB_PAGE_HEIGHT - y };
    };
    auto addPlate = [&](float y, ccColor4B color, float height = 40.f) {
        auto* plate = gelPanel(CARD_W, height, color, 12.f);
        plate->setPosition(pagePoint(CONFIG_PANEL_X, y));
        m_customPage->addChild(plate, -1);
    };
    auto addLabel = [&](char const* text, float x, float y, float width, float scale, ccColor3B color) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setAnchorPoint({ 0.f, 0.5f });
        label->limitLabelWidth(width, scale, scale * 0.6f);
        label->setColor(color);
        label->setPosition(pagePoint(x, y));
        m_customPage->addChild(label, 1);
        return label;
    };
    auto addControl = [&](float x, float y, float width, int minValue, int maxValue, int value,
                          std::function<void(geode::SliderNode*, float)> callback, char const* placeholder) {
        constexpr float INPUT_W = 46.f;
        auto* input = numberInput(INPUT_W, placeholder, 3);
        auto* slider = styledSlider(width, static_cast<float>(minValue), static_cast<float>(maxValue),
            static_cast<float>(value), std::move(callback));
        slider->linkTextInput(input, 0);
        slider->setPosition(pagePoint(x, y));
        input->setPosition(pagePoint(1130.f, y));
        m_customPage->addChild(slider, 20);
        m_customPage->addChild(input, 21);
        m_customSliders.push_back(slider);
        m_customInputs.push_back(input);
    };
    auto orbSprite = [](int index) -> CCSprite* {
        switch (index) {
            case SO_YELLOW: return CCSprite::create("orb-yellow.png"_spr);
            case SO_PINK: return CCSprite::create("orb-pink.png"_spr);
            case SO_BLUE: return CCSprite::create("orb-blue.png"_spr);
            case SO_RED: return CCSprite::create("orb-red.png"_spr);
            case SO_BLACK: return CCSprite::create("orb-black.png"_spr);
            case SO_GREEN: return CCSprite::create("orb-green.png"_spr);
            case SO_SPIDER: return CCSprite::create("orb-spider.png"_spr);
            case SO_DASH_GREEN: return CCSprite::create("orb-dash-green.png"_spr);
            case SO_DASH_PINK: return CCSprite::create("orb-dash-pink.png"_spr);
            default: return nullptr;
        }
    };

    float y = 158.f;
    for (int i = 0; i < SO_COUNT; ++i) {
        addPlate(y, ccc4(52, 24, 53, 255));
        if (auto* icon = orbSprite(i)) {
            icon->setScale(0.90f);
            icon->setPosition(pagePoint(903.f, y));
            m_customPage->addChild(icon, 2);
        }
        addLabel(STUDIO_ORBS[i].name, 928.f, y, 80.f, 0.56f, { 235, 245, 250 });
        addControl(1062.f, y, 74.f, 0, 32, studio.orbTicks[i],
            [this, i](geode::SliderNode*, float value) { onOrbSlider(i, value); }, "0");
        y += PITCH;
    }

    y += 8.f;
    addPlate(y, ccc4(66, 30, 62, 255));
    addLabel("LINK ALL ORBS", 892.f, y, 150.f, 0.58f, { 245, 220, 236 });
    auto* link = toggleItem(200, menu_selector(BufferMenu::onOrbLink), studio.orbTicksLinked, 66.f, 30.f);
    link->setPosition(pagePoint(1118.f, y));
    m_customMenu->addChild(link);
    m_specialToggles.push_back(link);
    y += PITCH + 8.f;

    static constexpr char const* RELEASE_NAMES[3] = { "Wave release", "Robot release", "Dash release" };
    for (int row = 0; row < 3; ++row) {
        addPlate(y, ccc4(52, 24, 53, 255));
        addLabel(RELEASE_NAMES[row], 892.f, y, 104.f, 0.52f, { 235, 245, 250 });
        int current = row == 0 ? buf.waveReleaseWindow : row == 1 ? buf.robotReleaseWindow : buf.dashReleaseWindow;
        addControl(1062.f, y, 74.f, 2, 64, current,
            [this, row](geode::SliderNode*, float value) { onReleaseSlider(row, value); }, "2");
        y += PITCH;
    }
}

void BufferMenu::buildKeybindPage() {
    constexpr float CARD_X = CONFIG_PANEL_X;
    constexpr float CARD_W = SETTINGS_W - 8.f;
    constexpr float PITCH = 62.f;
    auto pagePoint = [&](float x, float y) {
        return CCPoint{ x - CONFIG_PANEL_X + SETTINGS_W / 2.f, KEY_PAGE_HEIGHT - y };
    };

    // the line at the top says what to do, and turns red when one key drives two actions
    m_captureHint = CCLabelBMFont::create("", "bigFont.fnt");
    m_captureHint->setColor({ 150, 190, 210 });
    m_captureHint->setPosition(pagePoint(CARD_X, 152.f));
    m_customPage->addChild(m_captureHint, 1);

    for (int i = 0; i < KEYBIND_COUNT; ++i) {
        float y = 196.f + PITCH * i;
        auto* card = gelPanel(CARD_W, 54.f, ccc4(48, 22, 49, 245), 12.f);
        card->setPosition(pagePoint(CARD_X, y));
        m_customPage->addChild(card, -1);

        // line 1: what the key does (big)
        auto* name = CCLabelBMFont::create(KEYBIND_ACTION_LABELS[i], "bigFont.fnt");
        name->setAnchorPoint({ 0.f, 0.5f });
        name->limitLabelWidth(CARD_W - 24.f, 0.62f, 0.36f);
        name->setColor({ 235, 245, 250 });
        name->setPosition(pagePoint(890.f, y - 14.f));
        m_customPage->addChild(name, 1);

        // line 2: the key itself and the rebind button
        auto* keyPlate = gelPanel(106.f, 28.f, ccc4(24, 14, 36, 255), 9.f, false);
        keyPlate->setPosition(pagePoint(946.f, y + 13.f));
        m_customPage->addChild(keyPlate, 0);
        auto* key = CCLabelBMFont::create("...", "bigFont.fnt");
        key->limitLabelWidth(98.f, 0.62f, 0.34f);
        key->setColor({ 178, 233, 244 });
        key->setPosition(pagePoint(946.f, y + 13.f));
        m_customPage->addChild(key, 1);
        m_keyLabels.push_back(key);

        auto* button = actionItem("SET KEY", menu_selector(BufferMenu::onCaptureKey), i,
            100.f, 28.f, ccc4(46, 56, 112, 255));
        button->setPosition(pagePoint(1098.f, y + 13.f));
        m_customMenu->addChild(button);
    }
}

void BufferMenu::buildPathAssistPage() {
    constexpr float CARD_X = CONFIG_PANEL_X;
    constexpr float CARD_W = SETTINGS_W - 8.f;   // card spans x 878 .. 1162
    constexpr float PAD_L = 890.f;               // text starts 12 px inside the card
    constexpr float TOGGLE_X = 1122.f;           // 60 px toggle, 12 px from the right edge
    constexpr float ROW = 38.f;                  // one row pitch for every row on this page
    auto pagePoint = [&](float x, float y) {
        return CCPoint{ x - CONFIG_PANEL_X + SETTINGS_W / 2.f, pageHeightFor(m_tab) - y };
    };
    auto addPlate = [&](float y, ccColor4B color = ccc4(52, 24, 53, 255), float height = 32.f) {
        auto* plate = gelPanel(CARD_W, height, color, 10.f);
        plate->setPosition(pagePoint(CARD_X, y));
        m_customPage->addChild(plate, -1);
    };
    auto addLabel = [&](char const* text, float x, float y, float width, bool caption = false) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setAnchorPoint({ 0.f, 0.5f });
        label->limitLabelWidth(width, caption ? 0.48f : 0.54f, 0.34f);
        label->setColor(caption ? ccColor3B{ 255, 157, 220 } : ccColor3B{ 215, 234, 243 });
        label->setPosition(pagePoint(x, y));
        m_customPage->addChild(label, 1);
    };
    auto addToggle = [&](int tag, char const* text, bool value, SEL_MenuHandler callback, float y) {
        addPlate(y);
        addLabel(text, PAD_L, y, 196.f);
        auto* toggle = toggleItem(tag, callback, value, 60.f, 26.f);
        toggle->setPosition(pagePoint(TOGGLE_X, y));
        m_customMenu->addChild(toggle);
        m_specialToggles.push_back(toggle);
    };
    auto addSlider = [&](char const* text, float y, float minimum, float maximum, float value,
                         std::function<void(geode::SliderNode*, float)> callback) {
        addPlate(y);
        addLabel(text, PAD_L, y, 106.f);
        constexpr float INPUT_W = 40.f;
        auto* input = numberInput(INPUT_W, "0", 3);
        auto* slider = styledSlider(88.f, minimum, maximum, value, std::move(callback));
        slider->linkTextInput(input, 0);
        slider->setPosition(pagePoint(1064.f, y));
        input->setPosition(pagePoint(1132.f, y));
        m_customPage->addChild(slider, 20);
        m_customPage->addChild(input, 21);
        m_customSliders.push_back(slider);
        m_customInputs.push_back(input);
    };

    auto addNote = [&](char const* text, float y, float width = 270.f) {
        auto* note = CCLabelBMFont::create(text, "chatFont.fnt");
        note->setAnchorPoint({ 0.5f, 0.5f });
        note->limitLabelWidth(width, 0.50f, 0.28f);
        note->setColor({ 189, 150, 179 });
        note->setPosition(pagePoint(CARD_X, y));
        m_customPage->addChild(note, 1);
    };
    bool onPath = m_tab == 7;
    bool onAlt = m_tab == 9;
    bool onGap = m_tab == 10;
    bool onSpam = m_tab == 11;
    float y = 160.f;
    if (onPath) {
        addLabel("PATH VIEW", 888.f, y, 170.f, true);
        y += 32.f;
        addToggle(110, "Show predicted route", buf.showPath, menu_selector(BufferMenu::onPathToggle), y);
        y += ROW;
        addToggle(111, "Show route with no input", studio.pathShowIdle, menu_selector(BufferMenu::onPathToggle), y);
        y += ROW;
        addToggle(112, "Show route after input", studio.pathShowClick, menu_selector(BufferMenu::onPathToggle), y);
        y += ROW;
        addToggle(118, "Also draw my expected spam", studio.pathForecast, menu_selector(BufferMenu::onStudioToggle), y);
        y += ROW;
        addSlider("Path horizon", y, 24.f, 480.f, static_cast<float>(studio.pathHorizon),
            [this](geode::SliderNode*, float value) { onPathHorizonSlider(value); });
        y += ROW;
        addSlider("Path refresh (ms)", y, 8.f, 100.f, static_cast<float>(studio.pathRefreshMs),
            [this](geode::SliderNode*, float value) { onPathRefreshSlider(value); });
        y += ROW;
        addSlider("Spam path length", y, 60.f, 480.f, static_cast<float>(studio.altWaveHorizon),
            [this](geode::SliderNode*, float value) { onAltWaveHorizonSlider(value); });
        y += 34.f;
        addNote("Off = the wave path is what you hold right now, a straight line.", y);
        y += 20.f;
        addNote("On = it also draws the spam it expects from you (the old zigzag).", y);
    }
    if (onAlt) {
    addLabel("ALTERNATE ASSIST", 888.f, y, 160.f, true);
    auto* uskcPreset = actionItem("USKC WAVE", menu_selector(BufferMenu::onUSKCPreset), 0,
        90.f, 28.f, ccc4(46, 56, 112, 255));
    uskcPreset->setPosition(pagePoint(1110.f, y));
    m_customMenu->addChild(uskcPreset);
    y += 34.f;
    addToggle(100, "Adjust my real inputs", studio.altOn, menu_selector(BufferMenu::onAltToggle), y);
    y += ROW;

    // assist presets: only the alternate-assist numbers below change
    addPlate(y + 11.f, ccc4(62, 28, 60, 255), 58.f);
    addLabel("ASSIST PRESET", PAD_L, y - 6.f, 170.f);
    static constexpr char const* ASSIST_NAMES[3] = { "SAFE", "BALANCED", "AGGRESSIVE" };
    for (int i = 0; i < 3; ++i) {
        auto* item = actionItem(ASSIST_NAMES[i], menu_selector(BufferMenu::onAssistPreset), i, 84.f, 24.f, ccc4(46, 56, 112, 255));
        item->setPosition(pagePoint(929.f + 91.f * i, y + 14.f));
        m_customMenu->addChild(item);
    }
    y += 62.f;

    // Supported modes: one card, caption on top, five evenly spaced columns centred on the card.
    addPlate(y + 21.f, ccc4(62, 28, 60, 255), 74.f);
    addLabel("SUPPORTED MODES", PAD_L, y - 4.f, 150.f, true);
    static constexpr int MODE_TAGS[] = { 101, 102, 103, 104, 105 };
    static constexpr char const* MODE_NAMES[] = { "WAVE", "SHIP", "BALL", "UFO", "SWING" };
    static constexpr int MODE_IDS[] = { SM_WAVE, SM_SHIP, SM_BALL, SM_UFO, SM_SWING };
    for (size_t i = 0; i < std::size(MODE_TAGS); ++i) {
        float columnX = CARD_X + (static_cast<float>(i) - 2.f) * 52.f;
        auto* label = CCLabelBMFont::create(MODE_NAMES[i], "chatFont.fnt");
        label->limitLabelWidth(46.f, 0.46f, 0.34f);
        label->setColor({ 215, 234, 243 });
        label->setPosition(pagePoint(columnX, y + 20.f));
        m_customPage->addChild(label, 1);
        auto* toggle = toggleItem(MODE_TAGS[i], menu_selector(BufferMenu::onAltToggle), studio.altMode[MODE_IDS[i]], 40.f, 24.f);
        toggle->setPosition(pagePoint(columnX, y + 44.f));
        m_customMenu->addChild(toggle);
        m_specialToggles.push_back(toggle);
    }
    y += 80.f;

    addSlider("Max correction", y, 1.f, 64.f, static_cast<float>(studio.altMaxTicks),
        [this](geode::SliderNode*, float value) { onAltSlider(value); });
    y += ROW;
    addSlider("Rhythm samples", y, 3.f, 7.f, static_cast<float>(studio.altSamples),
        [this](geode::SliderNode*, float value) { onAltSamplesSlider(value); });
    y += ROW;
    addSlider("Timing tolerance", y, 0.f, 8.f, static_cast<float>(studio.altTolerance),
        [this](geode::SliderNode*, float value) { onAltToleranceSlider(value); });
    y += ROW;
    addSlider("Safety margin", y, 0.f, 4.f, static_cast<float>(studio.altMargin),
        [this](geode::SliderNode*, float value) { onAltMarginSlider(value); });
    y += ROW;
    addSlider("Burst gap cutoff", y, 8.f, 48.f, static_cast<float>(studio.altBurstGap),
        [this](geode::SliderNode*, float value) { onAltBurstGapSlider(value); });
    y += ROW;
    addSlider("Wave look (ticks)", y, 4.f, 64.f, static_cast<float>(studio.altWaveLook),
        [this](geode::SliderNode*, float value) { onAltWaveLookSlider(value); });
    y += ROW;
    addSlider("Minimum gain", y, 1.f, 24.f, static_cast<float>(studio.altWaveMinGain),
        [this](geode::SliderNode*, float value) { onAltWaveMinGainSlider(value); });
    y += ROW;
    addToggle(116, "Plan 2 edges ahead", studio.altPlan, menu_selector(BufferMenu::onAltToggle), y);
    y += ROW;
    addToggle(117, "Keep the zigzag even", studio.altSmooth, menu_selector(BufferMenu::onAltToggle), y);


        // more tuning: every number that used to be fixed inside the search
        y += ROW;
        addSlider("Even zigzag (%)", y, 0.f, 100.f, static_cast<float>(studio.altSmoothPct),
            [this](geode::SliderNode*, float value) { onMoreSlider(0, value); });
        y += ROW;
        addSlider("Plan width", y, 1.f, 8.f, static_cast<float>(studio.altPlanWidth),
            [this](geode::SliderNode*, float value) { onMoreSlider(1, value); });
        y += ROW;
        addSlider("Safe choices kept", y, 2.f, 6.f, static_cast<float>(studio.altSafeKeep),
            [this](geode::SliderNode*, float value) { onMoreSlider(2, value); });
        y += ROW;
        addSlider("Room levels", y, 1.f, 6.f, static_cast<float>(studio.altRoomLevels),
            [this](geode::SliderNode*, float value) { onMoreSlider(3, value); });
    y += 48.f;
        addLabel("PROGRESS STAGE BANDS", 888.f, y, 220.f, true);
        y += 34.f;
        addToggle(114, "Auto switch by level %", studio.progressStageBands, menu_selector(BufferMenu::onPathToggle), y);
        y += 30.f;
        {
            auto* hint = CCLabelBMFont::create("Pick a stage for each % range. PER MODE = your Gamemodes ticks.", "chatFont.fnt");
            hint->limitLabelWidth(CARD_W - 16.f, 0.46f, 0.30f);
            hint->setColor({ 189, 150, 179 });
            hint->setPosition(pagePoint(CARD_X, y));
            m_customPage->addChild(hint, 1);
        }

        constexpr float BAND_H = 104.f;
        constexpr float BAND_PITCH = 112.f;
        float bandTop = y + 14.f;
        static constexpr char const* STEP_TEXT[4] = { "-5", "-1", "+1", "+5" };
        static constexpr float STEP_X[4] = { 972.f, 1006.f, 1098.f, 1132.f };
        for (int band = 0; band < PROGRESS_BANDS; ++band) {
            addPlate(bandTop + BAND_H / 2.f, ccc4(62, 28, 60, 255), BAND_H);

            // row 1: title and stage selector  [<]  stage name  [>]
            float rowY = bandTop + 20.f;
            auto* title = CCLabelBMFont::create(fmt::format("BAND {}", band + 1).c_str(), "bigFont.fnt");
            title->setAnchorPoint({ 0.f, 0.5f });
            title->limitLabelWidth(66.f, 0.50f, 0.34f);
            title->setColor({ 255, 238, 249 });
            title->setPosition(pagePoint(PAD_L, rowY));
            m_customPage->addChild(title, 1);
            m_bandLabels.push_back(title);
            for (int dir = 0; dir < 2; ++dir) {
                auto* arrow = actionItem(dir == 0 ? "<" : ">", menu_selector(BufferMenu::onBandStage), band * 10 + dir,
                    32.f, 24.f, ccc4(46, 56, 112, 255));
                arrow->setPosition(pagePoint(dir == 0 ? 972.f : 1132.f, rowY));
                m_customMenu->addChild(arrow);
            }
            auto* stageName = CCLabelBMFont::create("", "bigFont.fnt");
            stageName->setColor({ 255, 225, 244 });
            stageName->setPosition(pagePoint(1052.f, rowY));
            m_customPage->addChild(stageName, 1);
            m_bandLabels.push_back(stageName);

            // rows 2 and 3: from % and to %, each with -5 -1 [value] +1 +5
            for (int field = 0; field < 2; ++field) {
                float fieldY = bandTop + 54.f + 32.f * field;
                auto* caption = CCLabelBMFont::create(field == 0 ? "FROM %" : "TO %", "bigFont.fnt");
                caption->setAnchorPoint({ 0.f, 0.5f });
                caption->limitLabelWidth(56.f, 0.46f, 0.32f);
                caption->setColor({ 215, 234, 243 });
                caption->setPosition(pagePoint(PAD_L, fieldY));
                m_customPage->addChild(caption, 1);
                for (int k = 0; k < 4; ++k) {
                    auto* item = actionItem(STEP_TEXT[k], menu_selector(BufferMenu::onBandStep), band * 100 + field * 10 + k,
                        32.f, 24.f, ccc4(46, 56, 112, 255));
                    item->setPosition(pagePoint(STEP_X[k], fieldY));
                    m_customMenu->addChild(item);
                }
                // type the number straight in (the buttons still nudge it)
                auto* box = numberInput(44.f, "0", 3);
                box->setPosition(pagePoint(1052.f, fieldY));
                box->setCallback([this, band, field](std::string const& text) { onBandInput(band, field, text); });
                m_customPage->addChild(box, 21);
                m_customInputs.push_back(box);
                m_bandInputs.push_back(box);
            }
            bandTop += BAND_PITCH;
        }

    }
    if (onGap) {
        addLabel("GAP CENTERING (STRAIGHT FLY)", 888.f, y, 230.f, true);
        y += 34.f;
        addToggle(115, "Center me in 1-block gaps", studio.sfOn, menu_selector(BufferMenu::onAltToggle), y);
        y += ROW;
        addSlider("Center strength (%)", y, 10.f, 100.f, static_cast<float>(studio.sfStrength),
            [this](geode::SliderNode*, float value) { onSfSlider(0, value); });
        y += ROW;
        addSlider("Gap size", y, 12.f, 48.f, static_cast<float>(studio.sfGap),
            [this](geode::SliderNode*, float value) { onSfSlider(1, value); });
        y += ROW;
        addSlider("Dead zone (ticks)", y, 0.f, 6.f, static_cast<float>(studio.sfDead),
            [this](geode::SliderNode*, float value) { onSfSlider(2, value); });
        y += 34.f;
        addNote("Needs the alternate assist on. In a 1-block gap each edge waits", y);
        y += 20.f;
        addNote("so your zigzag turns at symmetric points around the middle.", y);

        y += 52.f;
        addLabel("SHIP GUARD (TIGHT SHIP CORRIDORS)", 888.f, y, 250.f, true);
        y += 34.f;
        addToggle(141, "Switch the ship for me", studio.shipGuard, menu_selector(BufferMenu::onStudioToggle), y);
        y += ROW;
        addSlider("From level (%)", y, 0.f, 99.f, static_cast<float>(studio.shipGuardFrom),
            [this](geode::SliderNode*, float value) { onMoreSlider(4, value); });
        y += ROW;
        addSlider("To level (%)", y, 1.f, 100.f, static_cast<float>(studio.shipGuardTo),
            [this](geode::SliderNode*, float value) { onMoreSlider(5, value); });
        y += 34.f;
        addNote("For a ship part like the one at about 40% of Tidal Wave: set", y);
        y += 20.f;
        addNote("From 35 and To 45. When holding or letting go would hit a wall,", y);
        y += 20.f;
        addNote("it switches at the last safe moment. Needs Practice or 'Allow in", y);
        y += 20.f;
        addNote("normal runs'. Your own input comes back as soon as it is safe.", y);
    }
    if (onSpam) {
        addLabel("FRAME SPAM (1-BLOCK GAPS)", 888.f, y, 230.f, true);
        y += 34.f;
        addToggle(137, "Spam for me in 1-block gaps", studio.spamOn, menu_selector(BufferMenu::onStudioToggle), y);
        y += ROW;
        addSlider("Clicks per second", y, 6.f, 40.f, static_cast<float>(studio.spamCps),
            [this](geode::SliderNode*, float value) { onMoreSlider(6, value); });
        y += 34.f;
        addNote("It starts when you spam the wave through a tight gap, sends an", y);
        y += 20.f;
        addNote("even spam at the clicks per second above (a click = press + release,", y);
        y += 20.f;
        addNote("so 16 = 32 edges a second) and keeps it centred in the gap.", y);
        y += 20.f;
        addNote("A gap too small for that rate gets faster spam, never slower.", y);
        y += 20.f;
        addNote("It stops when you stop clicking. Same centring as 'Straight fly'.", y);
        y += 20.f;
        addNote("Needs Practice or 'Allow in normal runs'.", y);
    }
    refreshSpecialPage();
}

void BufferMenu::buildFunPage() {
    constexpr float CARD_X = CONFIG_PANEL_X;
    constexpr float CARD_W = SETTINGS_W - 8.f;
    constexpr float PAD_L = 890.f;
    constexpr float TOGGLE_X = 1122.f;
    constexpr float ROW = 38.f;
    auto pagePoint = [&](float x, float y) {
        return CCPoint{ x - CONFIG_PANEL_X + SETTINGS_W / 2.f, pageHeightFor(m_tab) - y };
    };
    auto addPlate = [&](float y) {
        auto* plate = gelPanel(CARD_W, 32.f, ccc4(52, 24, 53, 255), 10.f);
        plate->setPosition(pagePoint(CARD_X, y));
        m_customPage->addChild(plate, -1);
    };
    auto addLabel = [&](char const* text, float x, float y, float width, bool caption) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setAnchorPoint({ 0.f, 0.5f });
        label->limitLabelWidth(width, caption ? 0.48f : 0.54f, 0.34f);
        label->setColor(caption ? ccColor3B{ 255, 157, 220 } : ccColor3B{ 215, 234, 243 });
        label->setPosition(pagePoint(x, y));
        m_customPage->addChild(label, 1);
    };
    auto addToggle = [&](int index, float y) {
        addPlate(y);
        addLabel(FUN_NAMES[index], PAD_L, y, 200.f, false);
        auto* toggle = toggleItem(300 + index, menu_selector(BufferMenu::onStudioToggle), studio.fun[index], 60.f, 26.f);
        toggle->setPosition(pagePoint(TOGGLE_X, y));
        m_customMenu->addChild(toggle);
        m_specialToggles.push_back(toggle);
    };
    float y = 160.f;
    addLabel("ON-SCREEN INFO (LEFT SIDE)", 888.f, y, 240.f, true);
    y += 34.f;
    for (int i = 0; i <= 12; ++i) {
        addToggle(i, y);
        y += ROW;
    }
    y += 10.f;
    addLabel("SOUNDS AND FLASHES", 888.f, y, 240.f, true);
    y += 34.f;
    for (int i = 13; i <= 16; ++i) {
        addToggle(i, y);
        y += ROW;
    }
    y += 10.f;
    addLabel("TEXT AND LOOK", 888.f, y, 240.f, true);
    y += 34.f;
    for (int i = 17; i <= 19; ++i) {
        addToggle(i, y);
        y += ROW;
    }
    refreshSpecialPage();
}

void BufferMenu::buildStudioPage() {
    constexpr float CARD_X = CONFIG_PANEL_X;
    constexpr float CARD_W = SETTINGS_W - 8.f;
    constexpr float PAD_L = 890.f;
    constexpr float TOGGLE_X = 1122.f;
    constexpr float ROW = 38.f;
    auto pagePoint = [&](float x, float y) {
        return CCPoint{ x - CONFIG_PANEL_X + SETTINGS_W / 2.f, STUDIO_PAGE_HEIGHT - y };
    };
    auto addPlate = [&](float y, ccColor4B color = ccc4(52, 24, 53, 255), float height = 32.f) {
        auto* plate = gelPanel(CARD_W, height, color, 10.f);
        plate->setPosition(pagePoint(CARD_X, y));
        m_customPage->addChild(plate, -1);
    };
    auto addLabel = [&](char const* text, float x, float y, float width, bool caption = false) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setAnchorPoint({ 0.f, 0.5f });
        label->limitLabelWidth(width, caption ? 0.48f : 0.54f, 0.34f);
        label->setColor(caption ? ccColor3B{ 255, 157, 220 } : ccColor3B{ 215, 234, 243 });
        label->setPosition(pagePoint(x, y));
        m_customPage->addChild(label, 1);
        return label;
    };
    auto addValue = [&](float x, float y) {
        auto* value = CCLabelBMFont::create("0", "bigFont.fnt");
        value->setColor({ 255, 225, 244 });
        value->setPosition(pagePoint(x, y));
        m_customPage->addChild(value, 1);
        m_studioLabels.push_back(value);
        return value;
    };
    auto addButton = [&](char const* text, SEL_MenuHandler handler, int tag, float x, float y, float w) {
        auto* item = actionItem(text, handler, tag, w, 24.f, ccc4(46, 56, 112, 255));
        item->setPosition(pagePoint(x, y));
        m_customMenu->addChild(item);
    };
    auto addToggle = [&](int tag, char const* text, bool value, float y) {
        addPlate(y);
        addLabel(text, PAD_L, y, 196.f);
        auto* toggle = toggleItem(tag, menu_selector(BufferMenu::onStudioToggle), value, 60.f, 26.f);
        toggle->setPosition(pagePoint(TOGGLE_X, y));
        m_customMenu->addChild(toggle);
        m_specialToggles.push_back(toggle);
    };
    auto addSlider = [&](char const* text, float y, float minimum, float maximum, float value, int which) {
        addPlate(y);
        addLabel(text, PAD_L, y, 106.f);
        constexpr float INPUT_W = 40.f;
        auto* input = numberInput(INPUT_W, "0", 3);
        auto* slider = styledSlider(88.f, minimum, maximum, value,
            [this, which](geode::SliderNode*, float v) { onStudioSlider(which, v); });
        slider->linkTextInput(input, 0);
        slider->setPosition(pagePoint(1064.f, y));
        input->setPosition(pagePoint(1132.f, y));
        m_customPage->addChild(slider, 20);
        m_customPage->addChild(input, 21);
        m_customSliders.push_back(slider);
        m_customInputs.push_back(input);
    };
    static constexpr float STEP_X[4] = { 972.f, 1006.f, 1098.f, 1132.f };

    // ---- built-in presets: each one configures the whole setup
    float y = 160.f;
    addLabel("BUILT-IN PRESETS", 888.f, y, 200.f, true);
    y += 34.f;
    addPlate(y + 16.f, ccc4(62, 28, 60, 255), 70.f);
    static constexpr char const* BUILTIN_NAMES[4] = { "SAFE", "BALANCED", "AGGRESSIVE", "PERFORMANCE" };
    for (int i = 0; i < 4; ++i) {
        float bx = i % 2 == 0 ? 951.f : 1089.f;
        float by = y + (i < 2 ? 0.f : 32.f);
        auto* item = actionItem(BUILTIN_NAMES[i], menu_selector(BufferMenu::onBuiltinPreset), i, 130.f, 26.f, ccc4(46, 56, 112, 255));
        item->setPosition(pagePoint(bx, by));
        m_customMenu->addChild(item);
    }
    y += 74.f;

    // ---- auto assists (the toggles also exist on the Smart tab)
    addLabel("AUTO ASSISTS", 888.f, y, 200.f, true);
    y += 34.f;
    addToggle(130, "Predict moving hazards", Mod::get()->getSettingValue<bool>(AUTO_TOGGLE_KEYS[0]), y);
    y += ROW;
    addToggle(136, "Auto-on in levels with movers", studio.moversAuto, y);
    y += ROW;
    addToggle(131, "Wave auto-release", Mod::get()->getSettingValue<bool>(AUTO_TOGGLE_KEYS[1]), y);
    y += ROW;
    addToggle(132, "Robot auto-release", Mod::get()->getSettingValue<bool>(AUTO_TOGGLE_KEYS[2]), y);
    y += ROW;
    addToggle(133, "Dash auto-release", Mod::get()->getSettingValue<bool>(AUTO_TOGGLE_KEYS[3]), y);
    y += ROW;
    addToggle(134, "Auto-click", Mod::get()->getSettingValue<bool>(AUTO_TOGGLE_KEYS[4]), y);
    y += ROW;
    addToggle(135, "Allow in normal runs", Mod::get()->getSettingValue<bool>(AUTO_TOGGLE_KEYS[5]), y);
    y += ROW;
    addToggle(139, "Hide the AUTO INPUTS ON text", studio.autoTagHidden, y);
    y += ROW;
    addToggle(138, "Auto tag: start / pause / end only", studio.autoTagCompact, y);
    y += ROW;
    addSlider("Auto tag opacity (%)", y, 25.f, 100.f, static_cast<float>(studio.autoTagOpacity), 4);
    y += 28.f;
    {
        auto* hint = CCLabelBMFont::create("Auto inputs work in Practice, or with 'Allow in normal runs'. The AUTO INPUTS ON text can be hidden, dimmed, or shown only at attempt start / pause / end.", "chatFont.fnt");
        hint->limitLabelWidth(CARD_W - 16.f, 0.46f, 0.28f);
        hint->setColor({ 189, 150, 179 });
        hint->setPosition(pagePoint(CARD_X, y));
        m_customPage->addChild(hint, 1);
    }
    y += 36.f;

    // ---- colour theme: [<]  NAME  [>]
    addLabel("COLOR THEME", 888.f, y, 170.f, true);
    y += 32.f;
    addPlate(y);
    addLabel("Theme", PAD_L, y, 70.f);
    addButton("<", menu_selector(BufferMenu::onTheme), 0, 972.f, y, 32.f);
    addButton(">", menu_selector(BufferMenu::onTheme), 1, 1132.f, y, 32.f);
    addValue(1052.f, y);   // m_studioLabels[0]: theme name

    // ---- HUD position: one card per element, X and Y with -10 -1 +1 +10
    y += 48.f;
    addLabel("HUD POSITION", 888.f, y, 170.f, true);
    float top = y + 18.f;
    static constexpr char const* HUD_NAMES[3] = { "WATERMARK", "CONSOLE PANEL", "ORB DOT" };
    static constexpr char const* STEP10[4] = { "-10", "-1", "+1", "+10" };
    for (int element = 0; element < 3; ++element) {
        addPlate(top + 52.f, ccc4(62, 28, 60, 255), 104.f);
        auto* title = addLabel(HUD_NAMES[element], PAD_L, top + 20.f, 112.f);
        title->setColor({ 255, 238, 249 });
        addButton("RESET", menu_selector(BufferMenu::onHudReset), element, 1116.f, top + 20.f, 66.f);
        for (int axis = 0; axis < 2; ++axis) {
            float rowY = top + 54.f + 32.f * axis;
            addLabel(axis == 0 ? "X (px)" : "Y (px)", PAD_L, rowY, 60.f);
            for (int k = 0; k < 4; ++k) {
                addButton(STEP10[k], menu_selector(BufferMenu::onHudStep), element * 100 + axis * 10 + k, STEP_X[k], rowY, 32.f);
            }
            addValue(1052.f, rowY);   // m_studioLabels[1 + element * 2 + axis]
        }
        top += 112.f;
    }

    // ---- presets
    y = top + 14.f;
    addLabel("PRESETS", 888.f, y, 170.f, true);
    y += 32.f;
    for (int slot = 0; slot < PRESET_SLOTS; ++slot) {
        addPlate(y);
        addLabel(fmt::format("PRESET {}", slot + 1).c_str(), PAD_L, y, 70.f);
        auto* status = CCLabelBMFont::create("empty", "chatFont.fnt");
        status->setAnchorPoint({ 0.f, 0.5f });
        status->limitLabelWidth(60.f, 0.50f, 0.30f);
        status->setPosition(pagePoint(966.f, y));
        m_customPage->addChild(status, 1);
        m_studioLabels.push_back(status);   // m_studioLabels[7 + slot]
        addButton("SAVE", menu_selector(BufferMenu::onPresetSave), slot, 1072.f, y, 50.f);
        addButton("LOAD", menu_selector(BufferMenu::onPresetLoad), slot, 1128.f, y, 50.f);
        y += ROW;
    }

    // ---- assist tuning
    y += 10.f;
    addLabel("ASSIST TUNING", 888.f, y, 200.f, true);
    y += 34.f;
    addPlate(y);
    addLabel("Click bias", PAD_L, y, 70.f);
    static constexpr char const* STEP2[4] = { "-2", "-1", "+1", "+2" };
    for (int k = 0; k < 4; ++k) {
        addButton(STEP2[k], menu_selector(BufferMenu::onTuneStep), k, STEP_X[k], y, 32.f);
    }
    addValue(1052.f, y);   // m_studioLabels[13]: click bias
    y += ROW;
    addSlider("Orb margin (ticks)", y, 0.f, 4.f, static_cast<float>(studio.orbMargin), 0);
    y += ROW;
    addSlider("Swing look (ticks)", y, 16.f, 96.f, static_cast<float>(studio.swingLook), 1);
    y += ROW;
    addToggle(120, "Orb confidence dot", studio.orbConfOn, y);
    y += ROW;
    addToggle(121, "Adaptive buffer", studio.adaptiveOn, y);
    y += ROW;
    addSlider("Adaptive max (ticks)", y, 1.f, 8.f, static_cast<float>(studio.adaptiveMax), 2);
    y += ROW;
    addToggle(122, "Performance guard", studio.perfGuardOn, y);
    y += ROW;
    addSlider("Guard limit (ms)", y, 8.f, 60.f, static_cast<float>(studio.perfGuardMs), 3);
    y += ROW;
    addToggle(123, "Own buffer for Player 2", studio.p2Buffer, y);

    // ---- what the level scan found in the current level
    y += 48.f;
    addLabel("THIS LEVEL (SCAN)", 888.f, y, 200.f, true);
    y += 34.f;
    addPlate(y + 12.f, ccc4(62, 28, 60, 255), 64.f);
    addValue(1020.f, y - 6.f);   // m_studioLabels[14]: level scan summary
    addButton("RESCAN", menu_selector(BufferMenu::onRescan), 0, 1020.f, y + 20.f, 100.f);
    y += 58.f;
    addToggle(140, "Show the next gamemode", studio.nextModeHud, y);
    y += ROW;
    // where you die most (index 15) and the gamemode timeline, up to 12 lines (16 .. 27)
    addPlate(y, ccc4(62, 28, 60, 255), 32.f);
    {
        auto* hardest = CCLabelBMFont::create("", "bigFont.fnt");
        hardest->setAnchorPoint({ 0.f, 0.5f });
        hardest->setColor({ 255, 190, 190 });
        hardest->setPosition(pagePoint(PAD_L, y));
        m_customPage->addChild(hardest, 1);
        m_studioLabels.push_back(hardest);
    }
    y += 48.f;
    addLabel("GAMEMODE TIMELINE", 888.f, y, 220.f, true);
    y += 30.f;
    constexpr int TIMELINE_LINES = 12;
    constexpr float LINE = 27.f;
    addPlate(y - 14.f + (TIMELINE_LINES * LINE + 10.f) / 2.f, ccc4(52, 24, 53, 255), TIMELINE_LINES * LINE + 10.f);
    for (int line = 0; line < TIMELINE_LINES; ++line) {
        auto* row = CCLabelBMFont::create("", "bigFont.fnt");
        row->setAnchorPoint({ 0.f, 0.5f });
        row->setColor({ 225, 240, 250 });
        row->setPosition(pagePoint(PAD_L, y - 4.f + LINE * line));
        m_customPage->addChild(row, 1);
        m_studioLabels.push_back(row);
    }
    refreshSpecialPage();
}

void BufferMenu::onBuiltinPreset(CCObject* sender) {
    int index = sender->getTag();
    if (index < 0 || index > 3) return;
    studioPresetApply(studioBuiltinPreset(index));
    studioReadSettings();
    resetAlternateRhythm();
    buf.lastPathUpdate = {};
    studioSetStage(STUDIO_STAGE_CUSTOM);   // the presets live in the per-gamemode stage; this also reloads every setting
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onBandInput(int band, int field, std::string const& text) {
    if (m_syncingInputs || text.empty() || band < 0 || band >= PROGRESS_BANDS || field < 0 || field > 1) return;
    int value = 0;
    try {
        value = std::stoi(text);
    }
    catch (...) {
        return;
    }
    value = std::clamp(value, 0, field == 0 ? 99 : 100);
    int current = field == 0 ? studio.bandStart[band] : studio.bandEnd[band];
    if (value == current) return;
    Mod::get()->setSavedValue(fmt::format("studio-band{}-{}", band + 1, field == 0 ? "start" : "end"), static_cast<int64_t>(value));
    studioReadSettings();
    buf.lastPathUpdate = {};
    if (auto* layer = GameManager::get()->m_playLayer) updateProgressStageBand(layer);
    refreshSpecialPage();
}

void BufferMenu::onRescan(CCObject*) {
    if (auto* layer = GameManager::get()->m_playLayer) {
        scanLevel(layer);   // look again right now
    }
    refreshSpecialPage();
}

void BufferMenu::onTheme(CCObject* sender) {
    int dir = sender->getTag() == 0 ? -1 : 1;
    studio.themeIndex = (studio.themeIndex + dir + THEME_COUNT) % THEME_COUNT;
    Mod::get()->setSavedValue("studio-theme", static_cast<int64_t>(studio.themeIndex));
    // The whole menu is rebuilt in the new colours, back on this tab.
    queueInMainThread([] {
        if (s_instance) s_instance->finishClose();
        BufferMenu::open();
        if (s_instance) s_instance->selectTab(8);
    });
}

void BufferMenu::onHudStep(CCObject* sender) {
    static constexpr int DELTA[4] = { -10, -1, 1, 10 };
    static constexpr char const* KEYS[6] = { "studio-hud-wm-x", "studio-hud-wm-y", "studio-hud-panel-x",
        "studio-hud-panel-y", "studio-hud-dot-x", "studio-hud-dot-y" };
    int tag = sender->getTag();
    int element = tag / 100;
    int axis = (tag / 10) % 10;
    int step = tag % 10;
    if (element < 0 || element > 2 || axis > 1 || step > 3) return;
    int index = element * 2 + axis;
    int next = std::clamp(studio.hudOff[index] + DELTA[step], -400, 400);
    if (next == studio.hudOff[index]) return;
    Mod::get()->setSavedValue(KEYS[index], static_cast<int64_t>(next));
    studioReadSettings();
    applyHudLayout();
    refreshSpecialPage();
}

void BufferMenu::onHudReset(CCObject* sender) {
    static constexpr char const* KEYS[6] = { "studio-hud-wm-x", "studio-hud-wm-y", "studio-hud-panel-x",
        "studio-hud-panel-y", "studio-hud-dot-x", "studio-hud-dot-y" };
    int element = sender->getTag();
    if (element < 0 || element > 2) return;
    for (int axis = 0; axis < 2; ++axis) {
        Mod::get()->setSavedValue(KEYS[element * 2 + axis], static_cast<int64_t>(0));
    }
    studioReadSettings();
    applyHudLayout();
    refreshSpecialPage();
}

void BufferMenu::onPresetSave(CCObject* sender) {
    int slot = sender->getTag();
    if (slot < 0 || slot >= PRESET_SLOTS) return;
    Mod::get()->setSavedValue(fmt::format("studio-preset-{}", slot + 1), studioPresetSnapshot());
    refreshSpecialPage();
}

void BufferMenu::onPresetLoad(CCObject* sender) {
    int slot = sender->getTag();
    if (slot < 0 || slot >= PRESET_SLOTS) return;
    std::string text = Mod::get()->getSavedValue<std::string>(fmt::format("studio-preset-{}", slot + 1), "");
    if (text.empty()) return;
    studioPresetApply(text);
    studioReadSettings();
    applyStage();
    readSettings();
    resetAlternateRhythm();
    refreshWatermark();
    applyHudLayout();
    buf.lastPathUpdate = {};
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onTuneStep(CCObject* sender) {
    static constexpr int DELTA[4] = { -2, -1, 1, 2 };
    int step = sender->getTag();
    if (step < 0 || step > 3) return;
    int next = std::clamp(studio.clickBias + DELTA[step], -8, 8);
    if (next == studio.clickBias) return;
    Mod::get()->setSavedValue("studio-click-bias", static_cast<int64_t>(next));
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onStudioSlider(int which, float value) {
    static constexpr char const* KEYS[5] = { "studio-orb-margin", "studio-swing-look", "studio-adaptive-max",
        "studio-perf-guard-ms", "studio-auto-tag-opacity" };
    static constexpr int LO[5] = { 0, 16, 1, 8, 25 };
    static constexpr int HI[5] = { 4, 96, 8, 60, 100 };
    if (which < 0 || which > 4) return;
    int ticks = std::clamp(static_cast<int>(std::lround(value)), LO[which], HI[which]);
    int current[5] = { studio.orbMargin, studio.swingLook, studio.adaptiveMax, studio.perfGuardMs, studio.autoTagOpacity };
    if (ticks == current[which]) return;
    Mod::get()->setSavedValue(KEYS[which], static_cast<int64_t>(ticks));
    studioReadSettings();
    if (which == 4) refreshAutoTag();   // the tag follows the opacity slider at once
    refreshSpecialPage();
}

// The newer numbers: 0 even zigzag %, 1 plan width, 2 safe choices kept, 3 room levels, 4 / 5 ship guard range, 6 frame spam CPS.
void BufferMenu::onMoreSlider(int which, float value) {
    static constexpr char const* KEYS[7] = { "studio-alt-smooth-pct", "studio-alt-plan-width", "studio-alt-safe-keep",
        "studio-alt-room-levels", "studio-ship-guard-from", "studio-ship-guard-to", "studio-frame-spam-cps" };
    static constexpr int LO[7] = { 0, 1, 2, 1, 0, 1, 6 };
    static constexpr int HI[7] = { 100, 8, 6, 6, 99, 100, 40 };
    if (which < 0 || which > 6) return;
    int number = std::clamp(static_cast<int>(std::lround(value)), LO[which], HI[which]);
    int current[7] = { studio.altSmoothPct, studio.altPlanWidth, studio.altSafeKeep, studio.altRoomLevels,
        studio.shipGuardFrom, studio.shipGuardTo, studio.spamCps };
    if (number == current[which]) return;
    Mod::get()->setSavedValue(KEYS[which], static_cast<int64_t>(number));
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onStudioToggle(CCObject* sender) {
    auto* mod = Mod::get();
    int tag = sender->getTag();
    if (tag >= 130 && tag <= 135) {
        char const* key = AUTO_TOGGLE_KEYS[tag - 130];
        mod->setSettingValue<bool>(key, !mod->getSettingValue<bool>(key));
        readSettings();
        refreshAutoTag();
    }
    else if (tag == 136) mod->setSavedValue("studio-movers-auto", !studio.moversAuto);
    else if (tag == 138) mod->setSavedValue("studio-auto-tag-compact", !studio.autoTagCompact);
    else if (tag == 139) mod->setSavedValue("studio-auto-tag-hidden", !studio.autoTagHidden);
    else if (tag >= 300 && tag < 300 + FUN_COUNT) {
        mod->setSavedValue(fmt::format("studio-fun-{}", tag - 300), !studio.fun[tag - 300]);
    }
    else if (tag == 118) mod->setSavedValue("studio-path-forecast", !studio.pathForecast);
    else if (tag == 141) mod->setSavedValue("studio-ship-guard", !studio.shipGuard);
    else if (tag == 140) mod->setSavedValue("studio-next-mode", !studio.nextModeHud);
    else if (tag == 137) {
        mod->setSavedValue("studio-frame-spam", !studio.spamOn);
        if (studio.spamOn) {
            // switching it off: hand the wave back at once
            if (auto* layer = GameManager::get()->m_playLayer) endFrameSpam(layer, "switched off");
        }
    }
    else if (tag == 120) mod->setSavedValue("studio-orb-conf", !studio.orbConfOn);
    else if (tag == 121) {
        mod->setSavedValue("studio-adaptive", !studio.adaptiveOn);
        adaptiveReset();
    }
    else if (tag == 122) {
        mod->setSavedValue("studio-perf-guard", !studio.perfGuardOn);
        perfGuard = {};
    }
    else if (tag == 123) {
        mod->setSavedValue("studio-p2-buffer", !studio.p2Buffer);
        g_p2.needsReset = true;
    }
    studioReadSettings();
    if (tag == 136) readSettings();   // the moving-hazard setting is re-evaluated with the new switch
    refreshAutoTag();
    buf.orbConfShown = -1;
    refreshSpecialPage();
}

void BufferMenu::onModeStep(CCObject* sender) {
    static constexpr int DELTA[4] = { -4, -1, 1, 4 };
    int tag = sender->getTag();
    int mode = tag / 10;
    if (mode < 0 || mode >= SM_COUNT) return;
    int ticks = std::clamp(studio.modeTicks[mode] + DELTA[tag % 10], 0, STUDIO_MODES[mode].maxTicks);
    Mod::get()->setSettingValue<int64_t>(STUDIO_MODES[mode].key, ticks);
    if (ticks > 0) Mod::get()->setSavedValue(std::string(STUDIO_MODES[mode].key) + "-last", static_cast<int64_t>(ticks));
    applyStage();
    readSettings();
    refreshWatermark();
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onModeSlider(int mode, float value) {
    if (mode < 0 || mode >= SM_COUNT) return;
    int ticks = std::clamp(static_cast<int>(std::lround(value)), 0, STUDIO_MODES[mode].maxTicks);
    if (ticks == studio.modeTicks[mode]) return;
    for (int i = 0; i < SM_COUNT; ++i) {
        if (i != mode && !studio.modesLinked) continue;
        int each = std::clamp(ticks, 0, STUDIO_MODES[i].maxTicks);   // linked: every mode follows (within its own limit)
        Mod::get()->setSettingValue<int64_t>(STUDIO_MODES[i].key, each);
        if (each > 0) Mod::get()->setSavedValue(std::string(STUDIO_MODES[i].key) + "-last", static_cast<int64_t>(each));
    }
    applyStage();
    readSettings();
    refreshWatermark();
    if (studio.modesLinked) refreshSpecialPage();
    refresh();
}

void BufferMenu::onModeLink(CCObject*) {
    bool linked = !studio.modesLinked;
    Mod::get()->setSavedValue("studio-modes-linked", linked);
    if (linked) {
        int ticks = studio.modeTicks[SM_CUBE];   // like the orb link: everything takes the first value
        for (int i = 0; i < SM_COUNT; ++i) {
            int each = std::clamp(ticks, 0, STUDIO_MODES[i].maxTicks);
            Mod::get()->setSettingValue<int64_t>(STUDIO_MODES[i].key, each);
            if (each > 0) Mod::get()->setSavedValue(std::string(STUDIO_MODES[i].key) + "-last", static_cast<int64_t>(each));
        }
    }
    studioReadSettings();
    applyStage();
    readSettings();
    refreshWatermark();
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onAssistPreset(CCObject* sender) {
    int index = sender->getTag();
    if (index < 0 || index > 2) return;
    studioPresetApply(studioAssistPreset(index));
    studioReadSettings();
    resetAlternateRhythm();
    buf.lastPathUpdate = {};
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onModePower(CCObject* sender) {
    int mode = sender->getTag();
    if (mode < 0 || mode >= SM_COUNT) return;
    auto const& info = STUDIO_MODES[mode];
    if (studio.modeTicks[mode] > 0) {
        Mod::get()->setSettingValue<int64_t>(info.key, 0);
    }
    else {
        int last = static_cast<int>(Mod::get()->getSavedValue<int64_t>(std::string(info.key) + "-last", info.fallback));
        Mod::get()->setSettingValue<int64_t>(info.key, std::max(1, last));
    }
    applyStage();
    readSettings();
    refreshWatermark();
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onAltToggle(CCObject* sender) {
    auto* mod = Mod::get();
    int tag = sender->getTag();
    if (tag == 100) {
        mod->setSavedValue("studio-alt-enabled", !studio.altOn);
    }
    else if (tag == 115) {
        mod->setSavedValue("studio-sf-enabled", !studio.sfOn);
    }
    else if (tag == 116) {
        mod->setSavedValue("studio-alt-plan", !studio.altPlan);
    }
    else if (tag == 117) {
        mod->setSavedValue("studio-alt-smooth", !studio.altSmooth);
    }
    else if (tag >= 101 && tag <= 105) {
        static constexpr int MODES[] = { SM_WAVE, SM_SHIP, SM_BALL, SM_UFO, SM_SWING };
        static constexpr char const* KEYS[] = {
            "studio-alt-wave", "studio-alt-ship", "studio-alt-ball", "studio-alt-ufo", "studio-alt-swing"
        };
        int index = tag - 101;
        mod->setSavedValue(KEYS[index], !studio.altMode[MODES[index]]);
    }
    studioReadSettings();
    resetAlternateRhythm();
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onPathToggle(CCObject* sender) {
    auto* mod = Mod::get();
    int tag = sender->getTag();
    if (tag == 110) {
        buf.showPath = !buf.showPath;
        studio.pathEnabled = buf.showPath;
        mod->setSavedValue("studio-path-enabled", studio.pathEnabled);
        buf.lastPathUpdate = {};
        if (buf.pathNode) buf.pathNode->clear();
    }
    else if (tag == 111) {
        studio.pathShowIdle = !studio.pathShowIdle;
        mod->setSavedValue("studio-path-show-idle", studio.pathShowIdle);
    }
    else if (tag == 112) {
        studio.pathShowClick = !studio.pathShowClick;
        mod->setSavedValue("studio-path-show-click", studio.pathShowClick);
    }
    else if (tag == 113) {
        mod->setSettingValue<bool>("wave-auto-release", !buf.autoRelease);
        readSettings();
    }
    else if (tag == 114) {
        bool enable = !studio.progressStageBands;
        if (enable) {
            studio.progressBaseStage = buf.stage;
            mod->setSavedValue("studio-progress-base-stage", static_cast<int64_t>(buf.stage));
        }
        mod->setSavedValue("studio-progress-stage-bands", enable);
        studioReadSettings();
        auto* layer = GameManager::get()->m_playLayer;
        if (enable && layer) {
            updateProgressStageBand(layer);
        }
        else if (!enable && buf.stage != studio.progressBaseStage) {
            buf.stage = studio.progressBaseStage;
            mod->setSavedValue("stage", buf.stage);
            resetPending();
            if (layer && !buf.autoReleased && buf.appliedInput != buf.realInput) {
                sendButton(layer, buf.realInput);
            }
            applyStage();
            readSettings();
            refreshAutoTag();
            refreshWatermark();
            refreshPanel();
        }
    }
    refreshSpecialPage();
}

void BufferMenu::onAltSlider(float value) {
    int ticks = std::clamp(static_cast<int>(std::lround(value)), 1, 64);
    if (ticks == studio.altMaxTicks) return;
    Mod::get()->setSavedValue("studio-alt-max-delay", static_cast<int64_t>(ticks));
    studioReadSettings();
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onUSKCPreset(CCObject*) {
    auto* mod = Mod::get();
    // USKC's uneven controlled spam gets a short phase history, per-edge wave look,
    // and a one-tick gain threshold.  At this pace a correction can be valuable even
    // when it only saves the current direction change by one simulation tick.
    mod->setSavedValue("studio-alt-enabled", true);
    mod->setSavedValue("studio-alt-wave", true);
    mod->setSavedValue("studio-alt-ship", false);
    mod->setSavedValue("studio-alt-ball", false);
    mod->setSavedValue("studio-alt-ufo", false);
    mod->setSavedValue("studio-alt-swing", false);
    mod->setSavedValue("studio-alt-max-delay", static_cast<int64_t>(8));
    mod->setSavedValue("studio-alt-samples", static_cast<int64_t>(3));
    mod->setSavedValue("studio-alt-tolerance", static_cast<int64_t>(3));
    mod->setSavedValue("studio-alt-margin", static_cast<int64_t>(1));
    mod->setSavedValue("studio-alt-burst-gap", static_cast<int64_t>(30));
    mod->setSavedValue("studio-alt-wave-look", static_cast<int64_t>(12));
    mod->setSavedValue("studio-alt-wave-min-gain", static_cast<int64_t>(1));
    mod->setSavedValue("studio-alt-wave-horizon", static_cast<int64_t>(96));
    mod->setSavedValue("studio-sf-enabled", false);   // gap centering stays an extra you can switch on
    mod->setSavedValue("studio-sf-strength", static_cast<int64_t>(100));
    mod->setSavedValue("studio-sf-gap", static_cast<int64_t>(26));
    mod->setSavedValue("studio-sf-dead", static_cast<int64_t>(1));
    // Trajectory like the c5least version: where you end up if you keep your input, plus the
    // route of the last delayed click.
    mod->setSavedValue("studio-path-enabled", true);
    mod->setSavedValue("studio-path-show-idle", true);
    mod->setSavedValue("studio-path-show-click", false);
    mod->setSavedValue("studio-path-horizon", static_cast<int64_t>(120));
    mod->setSavedValue("studio-path-refresh-ms", static_cast<int64_t>(24));
    // Keep separate automatic input helpers off in this preset; they remain opt-in controls.
    mod->setSettingValue<bool>("wave-auto-release", false);
    mod->setSettingValue<bool>("robot-auto-release", false);
    mod->setSettingValue<bool>("dash-auto-release", false);
    mod->setSettingValue<bool>("auto-click", false);
    mod->setSettingValue<bool>("auto-release-normal-runs", false);
    buf.showPath = true;
    buf.lastPathUpdate = {};
    if (buf.pathNode) buf.pathNode->clear();
    readSettings();
    resetAlternateRhythm();
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onAltSamplesSlider(float value) {
    int samples = std::clamp(static_cast<int>(std::lround(value)), 3, 7);
    if (samples == studio.altSamples) return;
    Mod::get()->setSavedValue("studio-alt-samples", static_cast<int64_t>(samples));
    studioReadSettings();
    resetAlternateRhythm();
    refreshSpecialPage();
}

void BufferMenu::onAltToleranceSlider(float value) {
    int tolerance = std::clamp(static_cast<int>(std::lround(value)), 0, 8);
    if (tolerance == studio.altTolerance) return;
    Mod::get()->setSavedValue("studio-alt-tolerance", static_cast<int64_t>(tolerance));
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onAltMarginSlider(float value) {
    int margin = std::clamp(static_cast<int>(std::lround(value)), 0, 4);
    if (margin == studio.altMargin) return;
    Mod::get()->setSavedValue("studio-alt-margin", static_cast<int64_t>(margin));
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onAltBurstGapSlider(float value) {
    int ticks = std::clamp(static_cast<int>(std::lround(value)), 8, 48);
    if (ticks == studio.altBurstGap) return;
    Mod::get()->setSavedValue("studio-alt-burst-gap", static_cast<int64_t>(ticks));
    studioReadSettings();
    resetAlternateRhythm();
    refreshSpecialPage();
}

void BufferMenu::onAltWaveHorizonSlider(float value) {
    int ticks = std::clamp(static_cast<int>(std::lround(value)), 60, 480);
    if (ticks == studio.altWaveHorizon) return;
    Mod::get()->setSavedValue("studio-alt-wave-horizon", static_cast<int64_t>(ticks));
    studioReadSettings();
    resetAlternateRhythm();
    buf.lastPathUpdate = {};
    refreshSpecialPage();
}

void BufferMenu::onPathRefreshSlider(float value) {
    int ms = std::clamp(static_cast<int>(std::lround(value)), 8, 100);
    if (ms == studio.pathRefreshMs) return;
    Mod::get()->setSavedValue("studio-path-refresh-ms", static_cast<int64_t>(ms));
    studioReadSettings();
    buf.lastPathUpdate = {};
    refreshSpecialPage();
}

void BufferMenu::onAltWaveLookSlider(float value) {
    int ticks = std::clamp(static_cast<int>(std::lround(value)), 4, 64);
    if (ticks == studio.altWaveLook) return;
    Mod::get()->setSavedValue("studio-alt-wave-look", static_cast<int64_t>(ticks));
    studioReadSettings();
    resetAlternateRhythm();
    refreshSpecialPage();
}

void BufferMenu::onAltWaveMinGainSlider(float value) {
    int ticks = std::clamp(static_cast<int>(std::lround(value)), 1, 24);
    if (ticks == studio.altWaveMinGain) return;
    Mod::get()->setSavedValue("studio-alt-wave-min-gain", static_cast<int64_t>(ticks));
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onSfSlider(int which, float value) {
    static constexpr char const* KEYS[] = { "studio-sf-strength", "studio-sf-gap", "studio-sf-dead" };
    static constexpr int LO[] = { 10, 12, 0 };
    static constexpr int HI[] = { 100, 48, 6 };
    if (which < 0 || which > 2) return;
    int ticks = std::clamp(static_cast<int>(std::lround(value)), LO[which], HI[which]);
    int current[] = { studio.sfStrength, studio.sfGap, studio.sfDead };
    if (ticks == current[which]) return;
    Mod::get()->setSavedValue(KEYS[which], static_cast<int64_t>(ticks));
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onPathHorizonSlider(float value) {
    int ticks = std::clamp(static_cast<int>(std::lround(value)), 24, 480);
    if (ticks == studio.pathHorizon) return;
    Mod::get()->setSavedValue("studio-path-horizon", static_cast<int64_t>(ticks));
    studioReadSettings();
    buf.lastPathUpdate = {};
    refreshSpecialPage();
}

void BufferMenu::onBandStep(CCObject* sender) {
    static constexpr int DELTA[4] = { -5, -1, 1, 5 };
    int tag = sender->getTag();
    int band = tag / 100;
    int field = (tag / 10) % 10;   // 0 = from, 1 = to
    int step = tag % 10;
    if (band < 0 || band >= PROGRESS_BANDS || field > 1 || step > 3) return;
    int& value = field == 0 ? studio.bandStart[band] : studio.bandEnd[band];
    int next = std::clamp(value + DELTA[step], 0, field == 0 ? 99 : 100);
    if (next == value) return;
    Mod::get()->setSavedValue(fmt::format("studio-band{}-{}", band + 1, field == 0 ? "start" : "end"), static_cast<int64_t>(next));
    studioReadSettings();
    buf.lastPathUpdate = {};
    if (auto* layer = GameManager::get()->m_playLayer) updateProgressStageBand(layer);
    refreshSpecialPage();
}

void BufferMenu::onBandStage(CCObject* sender) {
    int tag = sender->getTag();
    int band = tag / 10;
    int dir = tag % 10 == 0 ? -1 : 1;
    if (band < 0 || band >= PROGRESS_BANDS) return;
    int stage = (studio.bandStage[band] + dir + STAGE_COUNT) % STAGE_COUNT;
    Mod::get()->setSavedValue(fmt::format("studio-band{}-stage", band + 1), static_cast<int64_t>(stage));
    studioReadSettings();
    if (auto* layer = GameManager::get()->m_playLayer) updateProgressStageBand(layer);
    refreshSpecialPage();
}

void BufferMenu::onModePreset(CCObject* sender) {
    static constexpr int GROUND[3] = { 8, 16, 32 };
    static constexpr int WAVE[3] = { 8, 8, 16 };
    static constexpr int SHIP[3] = { 2, 4, 8 };
    int preset = std::clamp(sender->getTag(), 0, 2);
    for (int i = 0; i < SM_COUNT; ++i) {
        int ticks = i == SM_WAVE ? WAVE[preset] : i == SM_SHIP ? SHIP[preset] : GROUND[preset];
        ticks = std::clamp(ticks, 0, STUDIO_MODES[i].maxTicks);
        Mod::get()->setSettingValue<int64_t>(STUDIO_MODES[i].key, ticks);
        Mod::get()->setSavedValue(std::string(STUDIO_MODES[i].key) + "-last", static_cast<int64_t>(ticks));
    }
    applyStage();
    readSettings();
    refreshWatermark();
    refreshSpecialPage();
}

void BufferMenu::onModeUseCustom(CCObject*) {
    studioSetStage(STUDIO_STAGE_CUSTOM);
    refresh();
    refreshSpecialPage();
}

void BufferMenu::onOrbStep(CCObject* sender) {
    static constexpr int DELTA[4] = { -4, -1, 1, 4 };
    int tag = sender->getTag();
    int orb = tag / 10;
    if (orb < 0 || orb >= SO_COUNT) return;
    int ticks = std::clamp(studio.orbTicks[orb] + DELTA[tag % 10], 0, 32);
    if (studio.orbTicksLinked) {
        for (int i = 0; i < SO_COUNT; ++i) Mod::get()->setSettingValue<int64_t>(STUDIO_ORBS[i].key, ticks);
    }
    else {
        Mod::get()->setSettingValue<int64_t>(STUDIO_ORBS[orb].key, ticks);
    }
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onOrbSlider(int orb, float value) {
    if (orb < 0 || orb >= SO_COUNT) return;
    int ticks = std::clamp(static_cast<int>(std::lround(value)), 0, 32);
    if (ticks == studio.orbTicks[orb]) return;
    if (studio.orbTicksLinked) {
        for (int i = 0; i < SO_COUNT; ++i) Mod::get()->setSettingValue<int64_t>(STUDIO_ORBS[i].key, ticks);
    }
    else {
        Mod::get()->setSettingValue<int64_t>(STUDIO_ORBS[orb].key, ticks);
    }
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onReleaseStep(CCObject* sender) {
    static constexpr char const* KEYS[3] = { "wave-release-window", "robot-release-window", "dash-release-window" };
    static constexpr int DELTA[4] = { -4, -1, 1, 4 };
    int tag = sender->getTag();
    int row = tag / 10;
    if (row < 0 || row >= 3) return;
    int ticks = row == 0 ? buf.waveReleaseWindow : row == 1 ? buf.robotReleaseWindow : buf.dashReleaseWindow;
    ticks = std::clamp(ticks + DELTA[tag % 10], 2, 64);
    Mod::get()->setSettingValue<int64_t>(KEYS[row], ticks);
    readSettings();
    refreshSpecialPage();
}

void BufferMenu::onReleaseSlider(int row, float value) {
    static constexpr char const* KEYS[3] = { "wave-release-window", "robot-release-window", "dash-release-window" };
    if (row < 0 || row >= 3) return;
    int ticks = std::clamp(static_cast<int>(std::lround(value)), 2, 64);
    int current = row == 0 ? buf.waveReleaseWindow : row == 1 ? buf.robotReleaseWindow : buf.dashReleaseWindow;
    if (ticks == current) return;
    Mod::get()->setSettingValue<int64_t>(KEYS[row], ticks);
    readSettings();
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onOrbLink(CCObject*) {
    bool linked = !studio.orbTicksLinked;
    Mod::get()->setSettingValue<bool>("orb-ticks-linked", linked);
    if (linked) {
        int ticks = studio.orbTicks[SO_YELLOW];
        for (int i = 0; i < SO_COUNT; ++i) Mod::get()->setSettingValue<int64_t>(STUDIO_ORBS[i].key, ticks);
    }
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onOrbLinkAll(CCObject*) {
    int ticks = studio.orbTicks[SO_YELLOW];
    for (int i = 0; i < SO_COUNT; ++i) Mod::get()->setSettingValue<int64_t>(STUDIO_ORBS[i].key, ticks);
    Mod::get()->setSettingValue<bool>("orb-ticks-linked", true);
    studioReadSettings();
    refreshSpecialPage();
}

void BufferMenu::onTpsStep(CCObject* sender) {
    int tps = std::clamp(buf.tps + sender->getTag(), TPS_MIN, TPS_MAX);
    Mod::get()->setSettingValue<int64_t>("tps", tps);
    readSettings();
    refreshWatermark();
    refreshSpecialPage();
    refresh();
}

void BufferMenu::onTpsSlider(float value) {
    if (m_syncingSlider) return;
    int tps = std::clamp(static_cast<int>(std::lround(value / 10.f)) * 10, TPS_MIN, TPS_MAX);
    if (tps == buf.tps) return;
    Mod::get()->setSettingValue<int64_t>("tps", tps);
    readSettings();
    refreshWatermark();
    refresh();
}

void BufferMenu::onCaptureKey(CCObject* sender) {
    m_captureKeyIndex = sender->getTag();
    refreshSpecialPage();
}

void BufferMenu::acceptCapturedKey(enumKeyCodes key) {
    if (m_captureKeyIndex < 0 || m_captureKeyIndex >= KEYBIND_COUNT) return;
    if (key == KEY_Escape) {
        m_captureKeyIndex = -1;
        refreshSpecialPage();
        return;
    }
    if (key == KEY_None) return;

    geode::KeyboardModifier modifiers;
    auto* keyboard = CCKeyboardDispatcher::get();
    if (keyboard->getShiftKeyPressed()) modifiers |= static_cast<uint8_t>(geode::KeyboardModifier::Shift);
    if (keyboard->getControlKeyPressed()) modifiers |= static_cast<uint8_t>(geode::KeyboardModifier::Control);
    if (keyboard->getAltKeyPressed()) modifiers |= static_cast<uint8_t>(geode::KeyboardModifier::Alt);
    if (keyboard->getCommandKeyPressed()) modifiers |= static_cast<uint8_t>(geode::KeyboardModifier::Super);
    auto setting = std::dynamic_pointer_cast<KeybindSettingV3>(Mod::get()->getSetting(KEYBIND_KEYS[m_captureKeyIndex]));
    if (setting) setting->setValue({ geode::Keybind(key, modifiers) });
    // warn when the same key now drives two actions
    m_keyWarning.clear();
    if (setting && !setting->getValue().empty()) {
        static constexpr char const* ACTION_NAMES[KEYBIND_COUNT] = { "Buffer on / off", "Panic raw input",
            "Change stage", "Open Synca menu", "Path view", "Console", "Copy log", "Forget level", "Wave direct input" };
        std::string mine = setting->getValue().front().toString();
        for (int i = 0; i < KEYBIND_COUNT; ++i) {
            if (i == m_captureKeyIndex) continue;
            auto other = std::dynamic_pointer_cast<KeybindSettingV3>(Mod::get()->getSetting(KEYBIND_KEYS[i]));
            if (other && !other->getValue().empty() && other->getValue().front().toString() == mine) {
                m_keyWarning = fmt::format("{} is also used by {}", mine, ACTION_NAMES[i]);
                break;
            }
        }
    }
    m_captureKeyIndex = -1;
    refreshSpecialPage();
}
