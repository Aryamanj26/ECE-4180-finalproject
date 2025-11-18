#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_VL53L0X.h>

#include <GesturePreprocessor.hpp>
#include <GestureClassifier.hpp>
#include <Speaker.hpp>

// I2C + XSHUT wiring
#define SDA_PIN   6
#define SCL_PIN   7
#define XSHUT_L   2
#define XSHUT_R   3
#define XSHUT_T   4
#define ADDR_L    0x30
#define ADDR_R    0x31
#define ADDR_T    0x32

#define SD_CS     9

Adafruit_VL53L0X L;
Adafruit_VL53L0X R;
Adafruit_VL53L0X T;

GesturePreprocessor gp;

const char* kTracks[] = {
  "/Rick-Roll-Sound-Effect.wav",
  "/afro-11-324020.wav",
  "/memphis-trap-wav-349366.wav"
};
const size_t kNumTracks = sizeof(kTracks) / sizeof(kTracks[0]);

bool initSensor(Adafruit_VL53L0X &sensor, int xshutPin, uint8_t newAddr) {
  pinMode(xshutPin, OUTPUT);
  digitalWrite(xshutPin, LOW);
  delay(5);
  digitalWrite(xshutPin, HIGH);
  delay(5);
  if (!sensor.begin(newAddr, false, &Wire)) {
    Serial.print("Failed to init VL53L0X at addr 0x");
    Serial.println(newAddr, HEX);
    return false;
  }
  return true;
}

uint16_t readVL(Adafruit_VL53L0X &s) {
  VL53L0X_RangingMeasurementData_t measure;
  s.rangingTest(&measure, false);
  if (measure.RangeStatus != 4) {
    return measure.RangeMilliMeter;
  }
  return 0xFFFF;
}

void gestureTask(void* arg) {
  for (;;) {
    uint32_t now = millis();

    uint16_t dL = readVL(L);
    uint16_t dR = readVL(R);
    uint16_t dT = readVL(T);

    GestureEvent ev = gp.update(dL, dR, dT, now);
    if (ev == GestureEvent::EpisodeReady) {
      const GestureEpisode &ep = gp.lastEpisode();

      uint32_t dur = ep.tEndMs - ep.tStartMs;

      auto swingOf = [&](int i)->uint16_t {
        if (ep.dMin[i] == 0xFFFF) return 0;
        return ep.dMax[i] - ep.dMin[i];
      };

      uint16_t swingL = swingOf(0);
      uint16_t swingR = swingOf(1);
      uint16_t swingT = swingOf(2);

      GestureDir dir = classifyEpisode(ep);

      Serial.print("EPISODE dur=");
      Serial.print(dur);
      Serial.print("ms  swing(L,R,T)=");
      Serial.print(swingL); Serial.print(",");
      Serial.print(swingR); Serial.print(",");
      Serial.print(swingT); Serial.print("  maxV(L,R,T)=");
      Serial.print(ep.maxApproachVel[0]); Serial.print(",");
      Serial.print(ep.maxApproachVel[1]); Serial.print(",");
      Serial.print(ep.maxApproachVel[2]);
      Serial.print("  -> ");

      switch (dir) {
        case GestureDir::Left:
          Serial.println("LEFT -> prevTrack()");
          Speaker::prevTrack();
          break;

        case GestureDir::Right:
          Serial.println("RIGHT -> nextTrack()");
          Speaker::nextTrack();
          break;

        case GestureDir::Up:
          Serial.println("UP -> volumeUp()");
          Speaker::volumeUp();
          break;

        case GestureDir::Down:
          Serial.println("DOWN -> volumeDown()");
          Speaker::volumeDown();
          break;

        case GestureDir::Tap:
          Serial.println("TAP -> pauseToggle()");
          Speaker::pauseToggle();
          break;

        default:
          Serial.println("NONE");
          break;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));  // ~50 Hz sensing
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(SDA_PIN, SCL_PIN);

  pinMode(XSHUT_L, OUTPUT);
  pinMode(XSHUT_R, OUTPUT);
  pinMode(XSHUT_T, OUTPUT);
  digitalWrite(XSHUT_L, LOW);
  digitalWrite(XSHUT_R, LOW);
  digitalWrite(XSHUT_T, LOW);
  delay(10);

  initSensor(L, XSHUT_L, ADDR_L);
  initSensor(R, XSHUT_R, ADDR_R);
  initSensor(T, XSHUT_T, ADDR_T);

  Serial.println("VL53L0X triangle + gesture episode detector ready");

  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS, SPI, 10000000)) {
  Serial.println("SD init failed");
  while (true) vTaskDelay(portMAX_DELAY);
  }

  if (!Speaker::initMax98357A(8, 22, 15, 44100)) {
    Serial.println("I2S init failed");
    while (true) vTaskDelay(portMAX_DELAY);
  }

  Speaker::setPlaylist(kTracks, kNumTracks);
  Speaker::startPlayer();  // spawns audio FreeRTOS task inside Speaker

  xTaskCreate(
    gestureTask,
    "gestureTask",
    4096,
    nullptr,
    1,
    nullptr
  );
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
