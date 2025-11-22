/*
 * Gesture Type Definitions
 * 
 * Defines the core enumerations and forward declarations used throughout
 * the gesture recognition system to represent detected gestures and events.
 */

#pragma once

/*
 * Represents the classified direction of a recognized gesture
 * Used by the classifier to communicate which gesture was detected.
 */
enum class GestureDir {
    None,
    Left,
    Right,
    Up,
    Down,
    Tap
};

/*
 * Events emitted by the gesture preprocessor state machine
 * Signals when a complete gesture episode is ready for classification.
 */
enum class GestureEvent {
    None = 0,
    EpisodeReady
};

// Forward declaration; full definition is in GesturePreprocessor.hpp
struct GestureEpisode;
