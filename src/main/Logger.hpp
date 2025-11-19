#pragma once
#include <Arduino.h>
#include <SD.h>
#include <stdarg.h>

#ifndef LOGGER_ENABLE_SERIAL_DEBUG
#define LOGGER_ENABLE_SERIAL_DEBUG 0
#endif

#if LOGGER_ENABLE_SERIAL_DEBUG
#define LOGGER_DEBUG(code) do { code; } while (0)
#else
#define LOGGER_DEBUG(code) do { (void)0; } while (0)
#endif

extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/semphr.h"
}

namespace Logger {

enum class Level : uint8_t {
  Info,
  Warn,
  Error
};

// -------- internal state (function-local statics to avoid multiple defs) --------

inline SemaphoreHandle_t& sdMutexRef() {
  static SemaphoreHandle_t h = nullptr;
  return h;
}

inline const char*& logPathRef() {
  static const char* p = "/system.log";   // keep it out of /tracks
  return p;
}

inline int& pinRRef() { static int v = -1; return v; }
inline int& pinGRef() { static int v = -1; return v; }
inline int& pinBRef() { static int v = -1; return v; }

inline bool& initializedRef() {
  static bool b = false;
  return b;
}

// -------- LED helpers --------

inline void setLed(bool r, bool g, bool b) {
  int pinR = pinRRef();
  int pinG = pinGRef();
  int pinB = pinBRef();
  if (pinR < 0 || pinG < 0 || pinB < 0) return;

  digitalWrite(pinR, r ? HIGH : LOW);
  digitalWrite(pinG, g ? HIGH : LOW);
  digitalWrite(pinB, b ? HIGH : LOW);
}

inline void ledIdle() { setLed(false, true,  false); } // green
inline void ledBusy() { setLed(false, false, true ); } // blue
inline void ledWarn() { setLed(true,  true,  false); } // yellow/orange
inline void ledWifi() { setLed(false, true,  true ); } // cyan
inline void ledError(){ setLed(true,  false, false); } // red

// -------- init --------

inline void init(SemaphoreHandle_t sdMutex,
                 const char* logPath,
                 int pinR, int pinG, int pinB)
{
  sdMutexRef() = sdMutex;
  if (logPath && logPath[0] != '\0') {
    logPathRef() = logPath;
  }

  pinRRef() = pinR;
  pinGRef() = pinG;
  pinBRef() = pinB;

  pinMode(pinR, OUTPUT);
  pinMode(pinG, OUTPUT);
  pinMode(pinB, OUTPUT);

  ledIdle();

  // Write a header line, best effort (short timeout)
  SemaphoreHandle_t m = sdMutexRef();
  if (m && xSemaphoreTake(m, pdMS_TO_TICKS(10)) == pdTRUE) {
    File f = SD.open(logPathRef(), FILE_WRITE);
    if (f) {
      f.println("=== Logger started ===");
      f.close();
    }
    xSemaphoreGive(m);
  }

  initializedRef() = true;
}

// -------- low-level write helper --------

inline void writeLine(Level level, const char* line) {
  if (!initializedRef() || !line) return;

  SemaphoreHandle_t m = sdMutexRef();
  if (!m) return;

  // Don’t block long – if SD is busy (audio), drop the log
  if (xSemaphoreTake(m, pdMS_TO_TICKS(5)) != pdTRUE) {
    return;
  }

  File f = SD.open(logPathRef(), FILE_WRITE);
  if (f) {
    uint32_t t = millis();
    const char* lvlStr =
      (level == Level::Info)  ? "INFO"  :
      (level == Level::Warn)  ? "WARN"  :
                                "ERROR";

    f.print('[');
    f.print(t);
    f.print(" ms][");
    f.print(lvlStr);
    f.print("] ");
    f.println(line);
    f.close();
  }
  else {
    ledError();
  }

  xSemaphoreGive(m);
}

// -------- public logging API --------

inline void log(Level level, const char* msg) {
  if (!initializedRef() || !msg) return;

  if (level == Level::Error) {
    ledError();
  }

  writeLine(level, msg);
}

inline void logf(Level level, const char* fmt, ...) {
  if (!initializedRef() || !fmt) return;

  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  log(level, buf);
}

} // namespace Logger
