#pragma once
#include <Arduino.h>
#include <GestureTypes.hpp>
#include <Logger.hpp>

struct GestureEpisode {
    uint32_t tStartMs = 0;
    uint32_t tEndMs   = 0;

    uint16_t dMin[3]  = { 0xFFFF, 0xFFFF, 0xFFFF };
    uint16_t dMax[3]  = { 0, 0, 0 };

    uint8_t  sampleCount   = 0;
    uint8_t  winnerChanges = 0;

    // when each sensor first/last saw the object in this episode
    uint32_t firstSeenMs[3] = { 0, 0, 0 };
    uint32_t lastSeenMs[3]  = { 0, 0, 0 };

    // peak approach velocity (mm/s) toward sensors per sensor
    int16_t  maxApproachVel[3] = { 0, 0, 0 };
};

class GesturePreprocessor {
public:
    GesturePreprocessor() { reset(); }

    /**
     *  WHat this function does is:
     *  1) take raw distance readings from the three sensors,
     *  2) process them through filtering and gating, and update the internal FSM state.
     * 
     *  Data processing includes:
     * - smoothing/filtering of raw distance readings using ema filter. low pass smoothing goal.
     * - nearest-depth gating
     * - validation of data.
     * 
     * 3) 
     */
    GestureEvent update(uint16_t d0, uint16_t d1, uint16_t d2, uint32_t nowMs) {
    uint16_t raw[3] = { d0, d1, d2 };
    filterDistances(raw);

    // Per-frame nearest-depth gating
    uint16_t zMinFrame = 0xFFFF;
    for (int i = 0; i < 3; ++i) {
        if (inBand(filt[i]) && filt[i] < zMinFrame) {
            zMinFrame = filt[i];
        }
    }

    bool valid[3] = { false, false, false };
    if (zMinFrame != 0xFFFF) {
        uint16_t zMaxAllowed = (uint16_t)(zMinFrame + NEAR_LAYER_TH_MM);
        for (int i = 0; i < 3; ++i) {
            if (inBand(filt[i]) && filt[i] <= zMaxAllowed) {
                valid[i] = true;
            }
        }
    }

    bool anyValid = valid[0] || valid[1] || valid[2];

    switch (state) {
        case State::Idle:
            Logger::ledIdle();
            if (anyValid) {
                if (++enterCount >= ENTER_COUNT) {
                    LOGGER_DEBUG(Serial.println("[FSM] Idle -> Tracking"));
                    startEpisode(nowMs);
                    appendSample(valid, nowMs);
                    state = State::Tracking;
                    enterCount = 0;
                }
            } else {
                enterCount = 0;
            }
            exitCount = 0;
            break;

        case State::Tracking:
            if (anyValid) {
                exitCount = 0;
                appendSample(valid, nowMs);

                // time-based ending
                if (nowMs - ep.tStartMs > MAX_EPISODE_MS) {
                    LOGGER_DEBUG(Serial.println("[FSM] Tracking timeout -> finalizeEpisode()"));
                    if (finalizeEpisode(nowMs)) {
                        LOGGER_DEBUG(Serial.println("[FSM] Tracking -> Cooldown (timeout)"));
                        state = State::Cooldown;
                        cooldownUntil = nowMs + COOLDOWN_MS;
                        return GestureEvent::EpisodeReady;
                    } else {
                        LOGGER_DEBUG(Serial.println("[FSM] finalize FAIL -> Idle"));
                        reset();
                    }
                }
            } else {
                if (++exitCount >= EXIT_COUNT) {
                    LOGGER_DEBUG(Serial.println("[FSM] Tracking exitCount reached -> finalizeEpisode()"));
                    if (finalizeEpisode(nowMs)) {
                        LOGGER_DEBUG(Serial.println("[FSM] Tracking -> Cooldown (hand left)"));
                        state = State::Cooldown;
                        cooldownUntil = nowMs + COOLDOWN_MS;
                        return GestureEvent::EpisodeReady;
                    } else {
                        LOGGER_DEBUG(Serial.println("[FSM] finalize FAIL -> Idle"));
                        reset();
                    }
                }
            }
            break;

        case State::Cooldown:
            if (!anyValid && nowMs >= cooldownUntil) {
                LOGGER_DEBUG(Serial.println("[FSM] Cooldown -> Idle"));
                reset();
            }
            break;
    }

    return GestureEvent::None;
}


    const GestureEpisode &lastEpisode() const { return ep; }

private:
    enum class State { Idle, Tracking, Cooldown };

    // Distance band for your ~8–14 cm gesture plane
    static constexpr uint16_t D_MIN_MM        = 30;
    static constexpr uint16_t D_MAX_MM        = 140;

    // Loosen start/stop so episodes actually happen
    static constexpr uint8_t  ENTER_COUNT     = 1;   // was 2
    static constexpr uint8_t  EXIT_COUNT      = 2;   // was 3

    // Looser episode duration
    static constexpr uint32_t MIN_EPISODE_MS  = 20;   // was 80
    static constexpr uint32_t MAX_EPISODE_MS  = 2000; // was 1500

    // Looser radial movement requirement (we can tighten later)
    static constexpr uint16_t MIN_SWING_MM    = 5;    // was 15

    // nearest-layer gating (how far behind the nearest object we still accept)
    static constexpr uint16_t NEAR_LAYER_TH_MM = 20;

    static constexpr uint32_t COOLDOWN_MS     = 5;

    // how many consecutive invalid frames before we clear filt[i]
    static constexpr uint8_t  INVALID_RESET_COUNT = 50;

    State    state;
    uint8_t  enterCount;
    uint8_t  exitCount;
    uint32_t cooldownUntil;

    uint16_t rawHist[3][3];
    uint8_t  rawIdx;
    uint16_t filt[3];
    uint8_t  invalidCount[3];

    // For velocity estimation
    uint16_t lastFiltForVel[3] = { 0, 0, 0 };
    uint32_t lastTimeForVel[3] = { 0, 0, 0 };

    GestureEpisode ep;
    int8_t  lastWinner;

    void reset() {
        state = State::Idle;
        enterCount = exitCount = 0;
        cooldownUntil = 0;
        rawIdx = 0;
        lastWinner = -1;

        for (int i = 0; i < 3; ++i) {
            filt[i] = 0;
            invalidCount[i] = 0;
            lastFiltForVel[i] = 0;
            lastTimeForVel[i] = 0;
            for (int j = 0; j < 3; ++j) rawHist[i][j] = 0;
            ep.dMin[i] = 0xFFFF;
            ep.dMax[i] = 0;
            ep.firstSeenMs[i]    = 0;
            ep.lastSeenMs[i]     = 0;
            ep.maxApproachVel[i] = 0;
        }
        ep.sampleCount   = 0;
        ep.winnerChanges = 0;
        ep.tStartMs = ep.tEndMs = 0;
    }

    static bool inBand(uint16_t d) {
        return d >= D_MIN_MM && d <= D_MAX_MM;
    }

    static uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
        if (a > b) { uint16_t t=a; a=b; b=t; }
        if (b > c) { uint16_t t=b; b=c; c=t; }
        if (a > b) { uint16_t t=a; a=b; b=t; }
        return b;
    }

    /**
     * This function performs smoothening and filtering of raw data from the sensors. 
     * It first checks if a value received is valid, if not it uses the median of the last
     * three readings to replace it to keep the values reliable for inference. 
     * 
     * 
     */
    void filterDistances(const uint16_t raw[3]) {
        rawIdx = (rawIdx + 1) % 3;//circular buffer index

        uint16_t m[3];

        // First, compute m[i] with preference for current valid raw
        for (int i = 0; i < 3; ++i) {
            rawHist[i][rawIdx] = raw[i];

            if (raw[i] != 0 && raw[i] != 0xFFFF) {//valid reading
                m[i] = raw[i];
            } else {//invalid reading, use median of history
                m[i] = median3(rawHist[i][0], rawHist[i][1], rawHist[i][2]);
            }
        }

        //find the closest valid reading aka closest object to the sensors such that the sensors are not 
        // influenced by the background/noise when making a gesture. 
        uint16_t zMinFrame = 0xFFFF;
        for (int i = 0; i < 3; ++i) {
            if (m[i] == 0 || m[i] == 0xFFFF) continue; // invalid
            if (m[i] < zMinFrame) zMinFrame = m[i];
        }

        // If no valid medians at all, treat everything as invalid and decay filters and reset
        if (zMinFrame == 0xFFFF) {
            for (int i = 0; i < 3; ++i) {
                if (invalidCount[i] < 255) invalidCount[i]++;
                if (invalidCount[i] >= INVALID_RESET_COUNT) {
                    filt[i] = 0;
                }
            }
            return;
        }

        // for each sensor only keep values in the global band determined above and within its area determined by near_layer_th_mm
        // ignore everything else beyond the closest sensed threshold
        uint16_t zMaxAllowed = (uint16_t)(zMinFrame + NEAR_LAYER_TH_MM);

        //validate each sensor reading and update EMA filter
        for (int i = 0; i < 3; ++i) {
            uint16_t mi = m[i];

            bool thisValid =
                (mi != 0 && mi != 0xFFFF) &&
                (mi >= D_MIN_MM && mi <= D_MAX_MM) &&
                (mi <= zMaxAllowed);

            if (!thisValid) {
                if (invalidCount[i] < 255) invalidCount[i]++;
                if (invalidCount[i] >= INVALID_RESET_COUNT) {
                    filt[i] = 0;              // no valid near-layer target -> clear
                }
                continue;
            }

            // valid near-layer reading -> reset invalid counter and update EMA
            invalidCount[i] = 0;

            if (filt[i] == 0) {
                filt[i] = mi;
            } else {
                // EMA with alpha = 1/4
                filt[i] = (uint16_t)((3u * filt[i] + mi) / 4u);
            }
        }
    }

    void startEpisode(uint32_t nowMs) {
        ep.tStartMs = nowMs;
        ep.sampleCount   = 0;
        ep.winnerChanges = 0;
        for (int i = 0; i < 3; ++i) {
            ep.dMin[i] = 0xFFFF;
            ep.dMax[i] = 0;
            ep.firstSeenMs[i]    = 0;
            ep.lastSeenMs[i]     = 0;
            ep.maxApproachVel[i] = 0;
        }
        lastWinner = -1;
    }

    void appendSample(const bool valid[3], uint32_t nowMs) {
        ep.sampleCount++;

        uint16_t best = 0xFFFF;
        int8_t winner = -1;

        for (int i = 0; i < 3; ++i) {
            uint16_t d = filt[i];

            if (valid[i]) {
                // mark first/last seen times for this sensor in this episode
                if (ep.firstSeenMs[i] == 0) ep.firstSeenMs[i] = nowMs;
                ep.lastSeenMs[i] = nowMs;

                // compute per-sample approach velocity if we have a previous sample
                if (lastFiltForVel[i] != 0 && d != 0 && lastTimeForVel[i] != 0) {
                    uint32_t dt = nowMs - lastTimeForVel[i];
                    if (dt > 0) {
                        int16_t dv = (int16_t)lastFiltForVel[i] - (int16_t)d; // >0 = moving closer
                        if (dv > 0) {
                            int16_t v = (int16_t)((dv * 1000) / (int32_t)dt); // mm/s
                            if (v > ep.maxApproachVel[i]) ep.maxApproachVel[i] = v;
                        }
                    }
                }

                // update radial swing stats
                if (d < ep.dMin[i]) ep.dMin[i] = d;
                if (d > ep.dMax[i]) ep.dMax[i] = d;

                if (d < best) { best = d; winner = i; }
            }

            // update velocity history for next time
            lastFiltForVel[i] = d;
            lastTimeForVel[i] = nowMs;
        }

        if (winner >= 0) {
            if (lastWinner >= 0 && winner != lastWinner) {
                ep.winnerChanges++;
            }
            lastWinner = winner;
        }
    }

bool finalizeEpisode(uint32_t nowMs) {
    ep.tEndMs = nowMs;

    LOGGER_DEBUG(Serial.println("---- finalizeEpisode ----"));

    // sample count
    if (ep.sampleCount < 2) {
        Logger::log(Logger::Level::Warn,
                    "Episode finalize failed: sampleCount < 2");
        LOGGER_DEBUG(Serial.println("FAIL: sampleCount < 2"));
        return false;
    }

    // duration check (minimum only)
    uint32_t dur = ep.tEndMs - ep.tStartMs;
    if (dur < MIN_EPISODE_MS) {
        Logger::log(Logger::Level::Warn,
                    "Episode finalize failed: duration too short");
        LOGGER_DEBUG(Serial.println("FAIL: duration too short"));
        return false;
    }

    // compute swings
    uint16_t maxSwing = 0;
    for (int i = 0; i < 3; ++i) {
        if (ep.dMin[i] != 0xFFFF) {
            uint16_t swing = ep.dMax[i] - ep.dMin[i];
            if (swing > maxSwing) maxSwing = swing;
        }
    }

    // compute max velocity
    int16_t maxV = ep.maxApproachVel[0];
    if (ep.maxApproachVel[1] > maxV) maxV = ep.maxApproachVel[1];
    if (ep.maxApproachVel[2] > maxV) maxV = ep.maxApproachVel[2];

    // final decision
    if (maxSwing < MIN_SWING_MM && maxV < 200) {
        Logger::log(Logger::Level::Warn,
                "Episode finalize failed: weak swing + weak velocity");
        LOGGER_DEBUG(Serial.println("FAIL: weak swing + weak velocity"));
        return false;
    }

    LOGGER_DEBUG(Serial.println("PASS: Episode finalized!"));
    LOGGER_DEBUG(Serial.println("------------------------"));
    return true;
}


};
