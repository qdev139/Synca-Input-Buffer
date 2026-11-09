// ---------------------------------------------------------------------------------------------------------
// Graph tab: the rhythm the alternate assist has learned (and how good its forecast is) + manual calibration.
// (included by menu.cpp, right after studio_ui.inc)
// ---------------------------------------------------------------------------------------------------------

void BufferMenu::buildGraphPage() {
    constexpr float CARD_X = CONFIG_PANEL_X;
    constexpr float CARD_W = SETTINGS_W - 8.f;
    constexpr float PAD_L = 890.f;
    auto pagePoint = [&](float x, float y) {
        return CCPoint{ x - CONFIG_PANEL_X + SETTINGS_W / 2.f, pageHeightFor(m_tab) - y };
    };
    auto addLabel = [&](char const* text, float x, float y, float width, bool caption, float scale = 0.5f) {
        auto* label = CCLabelBMFont::create(text, caption ? "bigFont.fnt" : "chatFont.fnt");
        label->setAnchorPoint({ 0.f, 0.5f });
        label->limitLabelWidth(width, caption ? 0.48f : scale, 0.28f);
        label->setColor(caption ? ccColor3B{ 255, 157, 220 } : ccColor3B{ 215, 234, 243 });
        label->setPosition(pagePoint(x, y));
        m_customPage->addChild(label, 1);
        return label;
    };
    auto addCard = [&](float y, float height) {
        auto* card = gelPanel(CARD_W, height, ccc4(52, 24, 53, 255), 12.f);
        card->setPosition(pagePoint(CARD_X, y + height / 2.f));
        m_customPage->addChild(card, -1);
    };

    float y = 150.f;
    addLabel("RHYTHM THE ASSIST LEARNED", 888.f, y, 250.f, true);
    y += 14.f;
    addCard(y, 160.f);
    m_graphNode = CCDrawNode::create();
    m_graphNode->setPosition(pagePoint(PAD_L + 2.f, y + 128.f));
    m_customPage->addChild(m_graphNode, 2);
    addLabel("DOWN", PAD_L + 4.f, y + 142.f, 60.f, false, 0.42f);
    addLabel("UP", PAD_L + 148.f, y + 142.f, 60.f, false, 0.42f);
    y += 174.f;
    m_graphInfo = addLabel("", 888.f, y, 262.f, false, 0.5f);
    y += 22.f;
    m_graphForecast = addLabel("", 888.f, y, 262.f, false, 0.5f);
    y += 34.f;

    addLabel("MANUAL CALIBRATION", 888.f, y, 250.f, true);
    y += 14.f;
    addCard(y, 270.f);
    addLabel("Press START. Rings shrink onto the target:", PAD_L, y + 14.f, 262.f, false, 0.46f);
    addLabel("tap TAP the moment a ring lands on it.", PAD_L, y + 30.f, 262.f, false, 0.46f);
    m_calibDraw = CCDrawNode::create();
    m_calibDraw->setPosition(pagePoint(1020.f, y + 112.f));
    m_customPage->addChild(m_calibDraw, 2);
    m_calibBeat = nullptr;
    auto addButton = [&](char const* text, SEL_MenuHandler handler, float x, float by, float w) {
        auto* item = actionItem(text, handler, 0, w, 32.f, ccc4(98, 34, 78, 255));
        item->setPosition(pagePoint(x, by));
        m_customMenu->addChild(item);
    };
    addButton("START", menu_selector(BufferMenu::onCalibStart), 934.f, y + 202.f, 74.f);
    addButton("TAP", menu_selector(BufferMenu::onCalibTap), 1020.f, y + 202.f, 86.f);
    addButton("APPLY", menu_selector(BufferMenu::onCalibApply), 1106.f, y + 202.f, 74.f);
    m_calibText = addLabel("Not started.", PAD_L, y + 244.f, 262.f, false, 0.46f);
    if (m_calibDone) {
        m_calibText->setString(fmt::format("You click {:.0f} ms {} = {:+.1f} ticks. APPLY sets Click bias.", std::abs(m_calibMs),
            m_calibMs >= 0.0 ? "late" : "early", m_calibMs * 0.24).c_str());
        m_calibText->limitLabelWidth(262.f, 0.48f, 0.28f);
    }
    drawRhythmGraph();
    schedule(schedule_selector(BufferMenu::onCalibFrame));
}

void BufferMenu::drawRhythmGraph() {
    if (!m_graphNode || m_tab != 6) return;
    m_graphNode->clear();
    constexpr float W = 276.f, H = 100.f;
    float scale = std::max(0.25f, stepScale());
    auto inTicks = [scale](int steps) { return static_cast<float>(steps) / scale; };
    float maxTicks = 20.f;
    for (int i = 0; i < buf.altDownSampleCount; ++i) maxTicks = std::max(maxTicks, inTicks(buf.altDownSamples[i]));
    for (int i = 0; i < buf.altUpSampleCount; ++i) maxTicks = std::max(maxTicks, inTicks(buf.altUpSamples[i]));
    maxTicks = std::max(maxTicks, std::max(inTicks(buf.altDownEvery), inTicks(buf.altUpEvery)));
    ccColor4F none { 0.f, 0.f, 0.f, 0.f };
    m_graphNode->drawRect({ 0.f, 0.f }, { W, 1.5f }, neonPremul({ 1.f, 1.f, 1.f, 0.30f }), 0.f, none);
    auto bars = [&](std::array<int, 7> const& samples, int count, int estimate, float x0, ccColor4F color) {
        for (int i = 0; i < count && i < 7; ++i) {
            float h = std::max(3.f, inTicks(samples[i]) / maxTicks * (H - 6.f));
            float x = x0 + i * 17.f;
            m_graphNode->drawRect({ x, 0.f }, { x + 13.f, h }, neonPremul(color), 0.f, none);
        }
        if (count > 0) {
            float eh = inTicks(estimate) / maxTicks * (H - 6.f);
            m_graphNode->drawRect({ x0 - 3.f, eh - 1.f }, { x0 + 7 * 17.f, eh + 1.f }, neonPremul({ 1.f, 0.92f, 0.45f, 0.95f }), 0.f, none);
        }
    };
    bars(buf.altDownSamples, buf.altDownSampleCount, buf.altDownEvery, 4.f, { 0.75f, 0.4f, 1.f, 0.95f });
    bars(buf.altUpSamples, buf.altUpSampleCount, buf.altUpEvery, 148.f, { 1.f, 0.5f, 0.8f, 0.95f });
    if (m_graphInfo) {
        m_graphInfo->setString(buf.altDownSampleCount + buf.altUpSampleCount == 0
            ? "Spam the wave with the alternate assist on to fill this."
            : fmt::format("DOWN {:.0f}t  UP {:.0f}t  ({} / {} samples)", inTicks(buf.altDownEvery), inTicks(buf.altUpEvery),
                  buf.altDownSampleCount, buf.altUpSampleCount).c_str());
        m_graphInfo->limitLabelWidth(262.f, 0.5f, 0.28f);
    }
    if (m_graphForecast) {
        m_graphForecast->setString(forecastText().c_str());
        m_graphForecast->limitLabelWidth(262.f, 0.5f, 0.28f);
    }
}

// Manual calibration, osu style: a ring shrinks onto a target once a second; tap TAP the moment it lands. The offset of each tap
// from the landing moment (in ms; + = late) is measured, and the median of 12 taps becomes the suggested Click bias.
static constexpr double CALIB_PERIOD = 1.0;    // seconds between landings
static constexpr double CALIB_APPROACH = 0.9;  // how long a ring takes to shrink

static void calibRing(CCDrawNode* draw, float radius, float width, ccColor4F color) {
    constexpr int SEGMENTS = 40;
    for (int i = 0; i < SEGMENTS; ++i) {
        float a0 = 6.2831853f * i / SEGMENTS, a1 = 6.2831853f * (i + 1) / SEGMENTS;
        draw->drawSegment({ std::cos(a0) * radius, std::sin(a0) * radius }, { std::cos(a1) * radius, std::sin(a1) * radius }, width, neonPremul(color));
    }
}

void BufferMenu::onCalibFrame(float) {
    if (m_tab != 6 || !m_calibDraw) {   // left the Graph tab: the drawing is gone
        m_calibDraw = nullptr;
        m_calibOn = false;
        unschedule(schedule_selector(BufferMenu::onCalibFrame));
        return;
    }
    m_calibDraw->clear();
    // the target
    calibRing(m_calibDraw, 22.f, 2.4f, { 1.f, 1.f, 1.f, 0.9f });
    calibRing(m_calibDraw, 26.f, 4.f, { 0.75f, 0.38f, 1.f, 0.25f });
    if (!m_calibOn) return;
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_calibStart).count();
    double landing = std::ceil(t - 0.12);             // the next beat (a ring just past it stays for a moment)
    if (landing < 1.0) landing = 1.0;
    double left = landing * CALIB_PERIOD - t;
    int beatIndex = static_cast<int>(std::floor(t / CALIB_PERIOD));
    if (beatIndex >= 1 && beatIndex > m_calibBeatPlayed) {   // the landing: a click sound
        m_calibBeatPlayed = beatIndex;
        FMODAudioEngine::sharedEngine()->playEffect("playSound_01.ogg");
    }
    if (left < CALIB_APPROACH) {
        float p = static_cast<float>(1.0 - left / CALIB_APPROACH);          // 0 far, 1 landed
        float radius = 22.f + (1.f - std::clamp(p, 0.f, 1.f)) * 52.f;
        float alpha = left < 0.0 ? std::max(0.f, 1.f + static_cast<float>(left) / 0.12f) : 0.25f + 0.75f * p;
        calibRing(m_calibDraw, radius, 3.2f, { 0.82f, 0.5f, 1.f, alpha });
        calibRing(m_calibDraw, radius, 7.f, { 0.75f, 0.38f, 1.f, alpha * 0.25f });
    }
}

void BufferMenu::onCalibStart(CCObject*) {
    unschedule(schedule_selector(BufferMenu::onCalibFrame));
    m_calibOn = !m_calibOn;
    m_calibOffsets.clear();
    m_calibDone = false;
    m_calibBeatPlayed = 0;
    if (m_calibOn) {
        m_calibStart = std::chrono::steady_clock::now();
        schedule(schedule_selector(BufferMenu::onCalibFrame));
        if (m_calibText) m_calibText->setString("Tap when the ring lands: 0 / 12");
    }
    else {
        schedule(schedule_selector(BufferMenu::onCalibFrame));   // keeps drawing the target
        if (m_calibText) m_calibText->setString("Stopped.");
    }
}

void BufferMenu::onCalibTap(CCObject*) {
    if (!m_calibOn) return;
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_calibStart).count();
    double nearest = std::max(1.0, std::round(t / CALIB_PERIOD)) * CALIB_PERIOD;
    double ms = (t - nearest) * 1000.0;               // + late, - early
    if (std::abs(ms) > 450.0) return;                 // far from any landing: not a real attempt
    m_calibOffsets.push_back(ms);
    if (m_calibOffsets.size() < 12) {
        if (m_calibText) {
            m_calibText->setString(fmt::format("{:+.0f} ms ({}). {} / 12", ms, ms >= 0.0 ? "late" : "early", m_calibOffsets.size()).c_str());
            m_calibText->limitLabelWidth(262.f, 0.46f, 0.28f);
        }
        return;
    }
    m_calibOn = false;
    auto sorted = m_calibOffsets;
    std::sort(sorted.begin(), sorted.end());
    m_calibMs = sorted[sorted.size() / 2];
    m_calibDone = true;
    if (m_calibText) {
        m_calibText->setString(fmt::format("You click {:.0f} ms {} = {:+.1f} ticks. APPLY sets Click bias.", std::abs(m_calibMs),
            m_calibMs >= 0.0 ? "late" : "early", m_calibMs * 0.24).c_str());
        m_calibText->limitLabelWidth(262.f, 0.46f, 0.28f);
    }
}

void BufferMenu::onCalibApply(CCObject*) {
    if (!m_calibDone) {
        if (m_calibText) m_calibText->setString("Do a calibration first (START, then 12 taps).");
        return;
    }
    int bias = std::clamp(static_cast<int>(std::lround(m_calibMs * 0.24)), -8, 8);
    Mod::get()->setSavedValue("studio-click-bias", static_cast<int64_t>(bias));
    studioReadSettings();
    if (m_calibText) m_calibText->setString(fmt::format("Click bias is now {:+}t.", bias).c_str());
}
