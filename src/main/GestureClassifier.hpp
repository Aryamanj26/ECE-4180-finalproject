#pragma once
#include <Arduino.h>
#include "GestureTypes.hpp"
#include "GesturePreprocessor.hpp"  // for full GestureEpisode

GestureDir classifyEpisode(const GestureEpisode& ep) {
    // sensor indices: 0 = Left, 1 = Right, 2 = Top
    auto swingOf = [&](int i)->uint16_t {
        if (ep.dMin[i] == 0xFFFF) return 0;
        return ep.dMax[i] - ep.dMin[i];
    };

    uint16_t swingL = swingOf(0);
    uint16_t swingR = swingOf(1);
    uint16_t swingT = swingOf(2);

    bool activeL = (swingL > 0);
    bool activeR = (swingR > 0);
    bool activeT = (swingT > 0);

    uint32_t dur = ep.tEndMs - ep.tStartMs;

    // max velocity over all sensors
    int16_t maxV = ep.maxApproachVel[0];
    if (ep.maxApproachVel[1] > maxV) maxV = ep.maxApproachVel[1];
    if (ep.maxApproachVel[2] > maxV) maxV = ep.maxApproachVel[2];

    // ---------- TAP ----------
    // Your palm taps show:
    //  - large swing on L/R (40–65mm)
    //  - maxV around 140–170 mm/s
    // We can say: "big swing on both L and R" + "velocity above ~130" = Tap.
    if (activeL && activeR) {
        if (swingL > 20 && swingR > 20 && maxV >= 60) {
            return GestureDir::Tap;
        }
    }

    // ---------- LEFT/RIGHT swipes ----------
    // Use firstSeen times on L and R.
    const uint32_t GAP_MIN = 5;   // ms, tune if needed
    const uint32_t GAP_MAX = 1500;  // ms, upper bound to avoid weird slow stuff

    uint32_t tL = ep.firstSeenMs[0];
    uint32_t tR = ep.firstSeenMs[1];

    if (activeL && activeR && tL && tR && (swingL > 5 || swingR > 5) && !activeT) {
        if (tR > tL && (tR - tL) >= GAP_MIN && (tR - tL) <= GAP_MAX) {
            // Left first then Right -> swipe towards Right
            return GestureDir::Right;
        }
        if (tL > tR && (tL - tR) >= GAP_MIN && (tL - tR) <= GAP_MAX) {
            // Right first then Left -> swipe towards Left
            return GestureDir::Left;
        }
    }

    // ---------- UP/DOWN swipes ----------
    // bottom = min(firstSeen(L), firstSeen(R))
    uint32_t tBottom = 0;
    if (activeL && ep.firstSeenMs[0]) tBottom = ep.firstSeenMs[0];
    if (activeR && ep.firstSeenMs[1]) {
        if (tBottom == 0 || ep.firstSeenMs[1] < tBottom)
            tBottom = ep.firstSeenMs[1];
    }
    uint32_t tTop = ep.firstSeenMs[2];

    if (tBottom && tTop && (swingL > 5 || swingR > 5 || swingT > 5)) {
        if (tTop > tBottom && (tTop - tBottom) >= GAP_MIN && (tTop - tBottom) <= GAP_MAX) {
            // bottom first then top
            return GestureDir::Up;
        }
        if (tBottom > tTop && (tBottom - tTop) >= GAP_MIN && (tBottom - tTop) <= GAP_MAX) {
            // top first then bottom
            return GestureDir::Down;
        }
    }

    return GestureDir::None;
}

