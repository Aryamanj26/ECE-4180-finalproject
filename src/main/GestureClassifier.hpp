/*
 * Gesture Classifier
 * 
 * Analyzes preprocessed gesture episodes to classify hand movements as specific gestures.
 * Uses timing information and sensor activation patterns to distinguish between
 * left/right swipes, up/down swipes, and tap gestures. The classifier looks at which
 * sensors were triggered first and the velocity of the hand movement.
 */

#pragma once
#include <Arduino.h>
#include "GestureTypes.hpp"
#include "GesturePreprocessor.hpp"

/*
 * Classifies a gesture episode into a recognized direction
 * Analyzes sensor timing, swing magnitude, and velocity to determine the gesture type.
 * Returns the classified gesture direction or None if no clear gesture is detected.
 */
GestureDir classifyEpisode(const GestureEpisode& ep)
{
    // calculate how much each sensor value changed during the gesture
    // swing is just the difference between max and min readings
    uint16_t swingL= 0;
    if (ep.dMin[0] != 0xFFFF)
    {
        swingL= ep.dMax[0] - ep.dMin[0];
    }
    
    uint16_t swingR= 0;
    if (ep.dMin[1] != 0xFFFF)
    {
        swingR= ep.dMax[1] - ep.dMin[1];
    }
    
    uint16_t swingT= 0;
    if (ep.dMin[2] != 0xFFFF)
    {
        swingT= ep.dMax[2] - ep.dMin[2];
    }

    bool activeL= (swingL > 0);
    bool activeR= (swingR > 0);
    bool activeT= (swingT > 0);

    uint32_t dur= ep.tEndMs - ep.tStartMs;

    // find the highest approach velocity from any of the three sensors
    int16_t maxV= ep.maxApproachVel[0];
    if (ep.maxApproachVel[1] > maxV)
    {
        maxV= ep.maxApproachVel[1];
    }
    if (ep.maxApproachVel[2] > maxV)
    {
        maxV= ep.maxApproachVel[2];
    }

    // detect tap gesture
    // a tap shows large simultaneous movement on both left and right sensors
    // with significant approach velocity indicating a quick hand motion toward sensors
    if (activeL && activeR)
    {
        if (swingL > 20 && swingR > 20 && maxV >= 60)
        {
            return GestureDir::Tap;
        }
    }

    // detect left or right swipe gestures
    // a horizontal swipe is detected by checking which sensor left or right sees
    // the hand first with a time gap between activations indicating direction
    const uint32_t GAP_MIN= 5;
    const uint32_t GAP_MAX= 1500;

    uint32_t tL= ep.firstSeenMs[0];
    uint32_t tR= ep.firstSeenMs[1];

    if (activeL && activeR && tL && tR && (swingL > 5 || swingR > 5) && !activeT)
    {
        // if right sensor triggered after left sensor then hand moved left to right
        if (tR > tL && (tR - tL) >= GAP_MIN && (tR - tL) <= GAP_MAX)
        {
            return GestureDir::Right;
        }
        // if left sensor triggered after right sensor then hand moved right to left
        if (tL > tR && (tL - tR) >= GAP_MIN && (tL - tR) <= GAP_MAX)
        {
            return GestureDir::Left;
        }
    }

    // detect up or down swipe gestures
    // vertical swipes are detected by comparing when the bottom sensors left and right
    // versus the top sensor first detect the hand
    // the timing difference indicates whether the hand moved upward or downward
    
    // find the earliest time either bottom sensor triggered
    uint32_t tBottom= 0;
    if (activeL && ep.firstSeenMs[0])
    {
        tBottom= ep.firstSeenMs[0];
    }
    if (activeR && ep.firstSeenMs[1])
    {
        if (tBottom== 0 || ep.firstSeenMs[1] < tBottom)
        {
            tBottom= ep.firstSeenMs[1];
        }
    }
    uint32_t tTop= ep.firstSeenMs[2];

    if (tBottom && tTop && (swingL > 5 || swingR > 5 || swingT > 5))
    {
        // if top sensor triggered after bottom sensors then hand moved bottom to top
        if (tTop > tBottom && (tTop - tBottom) >= GAP_MIN && (tTop - tBottom) <= GAP_MAX)
        {
            return GestureDir::Up;
        }
        // if bottom sensors triggered after top sensor then hand moved top to bottom
        if (tBottom > tTop && (tBottom - tTop) >= GAP_MIN && (tBottom - tTop) <= GAP_MAX)
        {
            return GestureDir::Down;
        }
    }

    return GestureDir::None;
}