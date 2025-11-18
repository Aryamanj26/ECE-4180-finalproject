#include <SPI.h>
#include <SD.h>
#include "Speaker.hpp"

// SD wiring you already verified
#define SD_CS 9

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("MAX98357A I2S speaker test starting...");

  // ---- SD init ----
  SPI.begin(18, 19, 23, SD_CS); // SCK=18, MISO=19, MOSI=23, SS=SD_CS
  if (!SD.begin(SD_CS)) {
    Serial.println("SD init failed");
    while (true) delay(1000);
  }
  Serial.println("SD init OK");

  // List files just for sanity
  File root = SD.open("/");
  Serial.println("Files on SD:");
  while (true) {
    File f = root.openNextFile();
    if (!f) break;
    Serial.print("  ");
    Serial.print(f.name());
    Serial.print("  ");
    Serial.println(f.size());
    f.close();
  }
  Serial.println("----");

  // ---- I2S / MAX98357A init ----
  // Your wiring: BCLK=8, LRC=22, DIN=15
  if (!Speaker::initMax98357A(
        8,    // BCLK  -> MAX BCLK
        22,   // LRC   -> MAX LRC
        15,   // DIN   -> MAX DIN
        44100 // default sample rate
      )) {
    Serial.println("I2S / MAX98357A init failed");
    while (true) delay(1000);
  }

  // ---- Play WAV file from SD over I2S ----
  const char *file = "/afro-11-324020.wav"; // or any of your .wav files
  Serial.print("Playing ");
  Serial.println(file);

  bool ok = Speaker::playWavI2S(file);
  if (!ok) {
    Serial.println("playWavI2S failed");
  } else {
    Serial.println("Done playing");
  }
}

void loop() {
  // Nothing here; playback happened in setup()
}
