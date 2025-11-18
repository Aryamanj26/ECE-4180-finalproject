#pragma once
#include <Arduino.h>
#include <SD.h>
#include <ESP_I2S.h>
#include <string.h> // for memcmp

namespace Speaker {

  // ========= WAV header parsing =========

  struct WavInfo {
    uint32_t sampleRate    = 0;
    uint16_t numChannels   = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataOffset    = 0;
    uint32_t dataSize      = 0;
  };

  inline bool parseWavHeader(File &f, WavInfo &info) {
    if (!f) return false;

    f.seek(0);
    uint8_t header[44];
    if (f.read(header, sizeof(header)) != sizeof(header)) return false;

    if (memcmp(header + 0, "RIFF", 4) != 0) return false;
    if (memcmp(header + 8, "WAVE", 4) != 0) return false;
    if (memcmp(header + 12, "fmt ", 4) != 0) return false;

    uint32_t fmtChunkSize =
      (uint32_t)header[16] |
      ((uint32_t)header[17] << 8) |
      ((uint32_t)header[18] << 16) |
      ((uint32_t)header[19] << 24);

    uint16_t audioFormat =
      (uint16_t)header[20] |
      ((uint16_t)header[21] << 8);
    uint16_t numChannels =
      (uint16_t)header[22] |
      ((uint16_t)header[23] << 8);
    uint32_t sampleRate =
      (uint32_t)header[24] |
      ((uint32_t)header[25] << 8) |
      ((uint32_t)header[26] << 16) |
      ((uint32_t)header[27] << 24);
    uint16_t bitsPerSample =
      (uint16_t)header[34] |
      ((uint16_t)header[35] << 8);

    if (audioFormat != 1) return false; // PCM only

    info.sampleRate    = sampleRate;
    info.numChannels   = numChannels;
    info.bitsPerSample = bitsPerSample;

    // Find "data" chunk (fmt chunk can be >16 bytes)
    uint32_t pos = 12 + 8 + fmtChunkSize;
    if (!f.seek(pos)) return false;

    while (true) {
      uint8_t chunkHdr[8];
      if (f.read(chunkHdr, 8) != 8) return false;

      uint32_t chunkSize =
        (uint32_t)chunkHdr[4] |
        ((uint32_t)chunkHdr[5] << 8) |
        ((uint32_t)chunkHdr[6] << 16) |
        ((uint32_t)chunkHdr[7] << 24);

      if (memcmp(chunkHdr, "data", 4) == 0) {
        info.dataOffset = f.position();
        info.dataSize   = chunkSize;
        return true;
      }

      pos = f.position() + chunkSize;
      if (!f.seek(pos)) return false;
    }
  }

  // ========= I2S backend → MAX98357A =========

  static I2SClass g_i2s;
  static bool     g_i2sInited = false;
  static uint32_t g_i2sRate   = 44100;

  inline bool initMax98357A(int bclkPin, int lrckPin, int dataPin,
                            uint32_t defaultRate = 44100) {
    g_i2sRate = defaultRate;
    g_i2s.setPins(bclkPin, lrckPin, dataPin); // BCLK, WS, DOUT

    bool ok = g_i2s.begin(I2S_MODE_STD,
                          g_i2sRate,
                          I2S_DATA_BIT_WIDTH_16BIT,
                          I2S_SLOT_MODE_MONO);
    g_i2sInited = ok;
    if (!ok) {
      Serial.println("Speaker::initMax98357A: i2s.begin failed");
    }
    return ok;
  }

  inline bool ensureSampleRate(uint32_t rate) {
    if (!g_i2sInited) return false;
    if (rate == 0 || rate == g_i2sRate) return true;

    if (!g_i2s.configureTX(rate,
                           I2S_DATA_BIT_WIDTH_16BIT,
                           I2S_SLOT_MODE_MONO)) {
      Serial.println("Speaker::ensureSampleRate: configureTX failed");
      return false;
    }
    g_i2sRate = rate;
    return true;
  }

  // ========= Simple blocking one-shot player (good for tests) =========

  inline bool playWavI2S(const char *path) {
    if (!g_i2sInited) {
      Serial.println("playWavI2S: I2S not initialized");
      return false;
    }

    File f = SD.open(path);
    if (!f) {
      Serial.print("playWavI2S: failed to open ");
      Serial.println(path);
      return false;
    }

    WavInfo info;
    if (!parseWavHeader(f, info)) {
      Serial.println("playWavI2S: invalid WAV header");
      f.close();
      return false;
    }

    // Accept mono or stereo, 16-bit only
    if (info.bitsPerSample != 16 ||
        (info.numChannels != 1 && info.numChannels != 2)) {
      Serial.print("playWavI2S: unsupported format (ch=");
      Serial.print(info.numChannels);
      Serial.print(", bits=");
      Serial.print(info.bitsPerSample);
      Serial.println(")");
      f.close();
      return false;
    }

    if (!ensureSampleRate(info.sampleRate)) {
      Serial.println("playWavI2S: failed to set sample rate");
      f.close();
      return false;
    }

    if (!f.seek(info.dataOffset)) {
      Serial.println("playWavI2S: failed to seek to data");
      f.close();
      return false;
    }

    Serial.print("playWavI2S: sampleRate=");
    Serial.print(info.sampleRate);
    Serial.print(" Hz, channels=");
    Serial.println(info.numChannels);

    const uint8_t  ch          = info.numChannels;
    const uint8_t  bytesPerSam = 2 * ch;
    uint32_t       remaining   = info.dataSize;

    const size_t   MAX_FRAMES = 256;
    int16_t        inBuf [MAX_FRAMES * 2];
    int16_t        outBuf[MAX_FRAMES];

    while (remaining > 0) {
      uint32_t bytesLeft  = remaining;
      size_t   maxBytes   = MAX_FRAMES * bytesPerSam;
      size_t   toRead     = (bytesLeft > maxBytes) ? maxBytes : bytesLeft;

      size_t n = f.read((uint8_t*)inBuf, toRead);
      if (!n) break;

      size_t framesRead = n / bytesPerSam;
      for (size_t i = 0; i < framesRead; ++i) {
        int16_t monoSample;
        if (ch == 1) {
          monoSample = inBuf[i];
        } else {
          int16_t left  = inBuf[2 * i + 0];
          int16_t right = inBuf[2 * i + 1];
          int32_t mix   = (int32_t)left + (int32_t)right;
          monoSample    = (int16_t)(mix / 2);
        }
        outBuf[i] = monoSample;
      }

      size_t outBytes = framesRead * 2;
      size_t written  = 0;
      while (written < outBytes) {
        written += g_i2s.write(
          ((uint8_t*)outBuf) + written,
          outBytes - written
        );
      }

      remaining -= n;
      yield();
    }

    f.close();
    return true;
  }

  inline bool playWavI2S(const String &path) {
    return playWavI2S(path.c_str());
  }

  // ========= Background player: playlist + controls =========

  // Max number of tracks in playlist
  static const size_t MAX_TRACKS = 16;

  static const char* g_playlist[MAX_TRACKS];
  static size_t      g_playlistCount = 0;
  static size_t      g_currentIndex  = 0;

  // Player commands & state (safe from gesture code)
  static volatile bool g_cmdNext        = false;
  static volatile bool g_cmdPrev        = false;
  static volatile bool g_cmdPauseToggle = false;
  static volatile int  g_cmdVolDelta    = 0; // +/- steps
  static volatile bool g_stopRequested  = false;
  static volatile bool g_paused         = false;

  // Volume (software gain)
  static float g_volume = 1.0f; // 1.0 = unity, 0.0 = mute, up to ~2.0

  // Audio task handle
  static TaskHandle_t g_audioTaskHandle = nullptr;

  inline void setPlaylist(const char* const* files, size_t count) {
    if (count > MAX_TRACKS) count = MAX_TRACKS;
    for (size_t i = 0; i < count; ++i) {
      g_playlist[i] = files[i];
    }
    g_playlistCount = count;
    g_currentIndex  = 0;
  }

  // Control API – call these from your gesture code
  inline void nextTrack()       { g_cmdNext        = true; }
  inline void prevTrack()       { g_cmdPrev        = true; }
  inline void pauseToggle()     { g_cmdPauseToggle = true; }
  inline void stopPlayback()    { g_stopRequested  = true; }
  inline void volumeUp()        { g_cmdVolDelta++; }
  inline void volumeDown()      { g_cmdVolDelta--; }

  // ==== internal helpers / task ====

  inline int16_t clamp16(int32_t x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
  }

  static void audioTask(void* /*arg*/) {
    Serial.println("Speaker::audioTask: started");

    for (;;) {
      if (g_stopRequested) break;

      if (g_playlistCount == 0 || !g_i2sInited) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }

      const char* path = g_playlist[g_currentIndex];
      Serial.print("Speaker::audioTask: opening ");
      Serial.println(path);

      File f = SD.open(path);
      if (!f) {
        Serial.println("Speaker::audioTask: failed to open file, skipping");
        g_currentIndex = (g_currentIndex + 1) % g_playlistCount;
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      WavInfo info;
      if (!parseWavHeader(f, info)) {
        Serial.println("Speaker::audioTask: invalid WAV header, skipping");
        f.close();
        g_currentIndex = (g_currentIndex + 1) % g_playlistCount;
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      if (info.bitsPerSample != 16 ||
          (info.numChannels != 1 && info.numChannels != 2)) {
        Serial.print("Speaker::audioTask: unsupported format (ch=");
        Serial.print(info.numChannels);
        Serial.print(", bits=");
        Serial.print(info.bitsPerSample);
        Serial.println("), skipping");
        f.close();
        g_currentIndex = (g_currentIndex + 1) % g_playlistCount;
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      if (!ensureSampleRate(info.sampleRate)) {
        Serial.println("Speaker::audioTask: failed to set sample rate, skipping");
        f.close();
        g_currentIndex = (g_currentIndex + 1) % g_playlistCount;
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      if (!f.seek(info.dataOffset)) {
        Serial.println("Speaker::audioTask: seek to data failed, skipping");
        f.close();
        g_currentIndex = (g_currentIndex + 1) % g_playlistCount;
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      Serial.print("Speaker::audioTask: playing, rate=");
      Serial.print(info.sampleRate);
      Serial.print(" Hz, channels=");
      Serial.println(info.numChannels);

      const uint8_t ch          = info.numChannels;
      const uint8_t bytesPerSam = 2 * ch;
      uint32_t      remaining   = info.dataSize;

      const size_t  MAX_FRAMES = 256;
      int16_t       inBuf [MAX_FRAMES * 2];
      int16_t       outBuf[MAX_FRAMES];

      bool trackDone   = false;
      bool advanceNext = false;
      bool advancePrev = false;

      while (!trackDone && !g_stopRequested) {
        // Handle pause
        if (g_paused) {
          vTaskDelay(pdMS_TO_TICKS(10));
          // Volume changes still apply
          if (g_cmdVolDelta != 0) {
            int delta = g_cmdVolDelta;
            g_cmdVolDelta = 0;
            g_volume += 0.1f * delta;
            if (g_volume < 0.0f) g_volume = 0.0f;
            if (g_volume > 2.0f) g_volume = 2.0f;
            Serial.print("Speaker::volume=");
            Serial.println(g_volume);
          }
          if (g_cmdPauseToggle) {
            g_cmdPauseToggle = false;
            g_paused = false;
            Serial.println("Speaker::unpause");
          }
          if (g_cmdNext) {
            g_cmdNext = false;
            advanceNext = true;
            break;
          }
          if (g_cmdPrev) {
            g_cmdPrev = false;
            advancePrev = true;
            break;
          }
          continue;
        }

        if (remaining == 0) {
          trackDone = true;
          break;
        }

        // Handle control commands
        if (g_cmdPauseToggle) {
          g_cmdPauseToggle = false;
          g_paused = !g_paused;
          Serial.println(g_paused ? "Speaker::pause" : "Speaker::unpause");
          continue;
        }
        if (g_cmdNext) {
          g_cmdNext = false;
          advanceNext = true;
          break;
        }
        if (g_cmdPrev) {
          g_cmdPrev = false;
          advancePrev = true;
          break;
        }
        if (g_cmdVolDelta != 0) {
          int delta = g_cmdVolDelta;
          g_cmdVolDelta = 0;
          g_volume += 0.1f * delta;
          if (g_volume < 0.0f) g_volume = 0.0f;
          if (g_volume > 2.0f) g_volume = 2.0f;
          Serial.print("Speaker::volume=");
          Serial.println(g_volume);
        }

        uint32_t bytesLeft = remaining;
        size_t   maxBytes  = MAX_FRAMES * bytesPerSam;
        size_t   toRead    = (bytesLeft > maxBytes) ? maxBytes : bytesLeft;

        size_t n = f.read((uint8_t*)inBuf, toRead);
        if (!n) {
          trackDone = true;
          break;
        }

        size_t framesRead = n / bytesPerSam;
        for (size_t i = 0; i < framesRead; ++i) {
          int16_t monoSample;
          if (ch == 1) {
            monoSample = inBuf[i];
          } else {
            int16_t left  = inBuf[2 * i + 0];
            int16_t right = inBuf[2 * i + 1];
            int32_t mix   = (int32_t)left + (int32_t)right;
            monoSample    = (int16_t)(mix / 2);
          }
          int32_t scaled = (int32_t)(monoSample * g_volume);
          outBuf[i] = clamp16(scaled);
        }

        size_t outBytes = framesRead * 2;
        size_t written  = 0;
        while (written < outBytes) {
          written += g_i2s.write(
            ((uint8_t*)outBuf) + written,
            outBytes - written
          );
        }

        remaining -= n;
        taskYIELD();
      }

      f.close();

      if (g_stopRequested) break;

      if (advanceNext) {
        g_currentIndex = (g_currentIndex + 1) % g_playlistCount;
      } else if (advancePrev) {
        g_currentIndex = (g_currentIndex + g_playlistCount - 1) % g_playlistCount;
      } else {
        // Normal end-of-track → replay SAME track
        // g_currentIndex unchanged
      }

      vTaskDelay(pdMS_TO_TICKS(10));

    }

    Serial.println("Speaker::audioTask: exiting");
    g_audioTaskHandle = nullptr;
    vTaskDelete(nullptr);
  }

  inline void startPlayer() {
    if (!g_i2sInited || g_playlistCount == 0) {
      Serial.println("Speaker::startPlayer: I2S not inited or playlist empty");
      return;
    }
    if (g_audioTaskHandle) {
      Serial.println("Speaker::startPlayer: already running");
      return;
    }

    g_stopRequested  = false;
    g_cmdNext        = false;
    g_cmdPrev        = false;
    g_cmdPauseToggle = false;
    g_cmdVolDelta    = 0;
    g_paused         = false;
    g_volume         = 0.05f;

    xTaskCreate(
      audioTask,
      "audioPlayer",
      4096,
      nullptr,
      1,
      &g_audioTaskHandle
    );
  }

} // namespace Speaker
