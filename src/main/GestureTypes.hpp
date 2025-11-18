#pragma once

// High-level classified gesture
enum class GestureDir {
    None,
    Left,
    Right,
    Up,
    Down,
    Tap
};


// Low-level preprocessor event
enum class GestureEvent {
    None = 0,
    EpisodeReady
};

// Forward declaration; full definition is in GesturePreprocessor.hpp
struct GestureEpisode;
