/*
 * Main Arduino sketch for gesture-controlled music player
 * 
 * This system uses three VL53L0X Time-of-Flight sensors arranged in a triangle
 * to detect hand gestures (left, right, up, down, tap) for controlling music playback.
 * Gestures trigger actions like volume control, track navigation, and play/pause.
 * Audio playback is handled via I2S to a MAX98357A amplifier with WAV files from SD card.
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_VL53L0X.h>

#include <Logger.hpp>
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

#define LED_G  1
#define LED_B  13
#define LED_R  12
#define BUTTON_PIN 21


SemaphoreHandle_t g_sdMutex = nullptr;
volatile bool     g_systemEnabled = true;

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

volatile uint32_t g_lastButtonPressMs = 0;

/*
 * Interrupt service routine for the physical button press
 * Toggles the system between enabled and disabled states with debouncing.
 * When disabled, gesture recognition pauses and audio playback is paused.
 */
void IRAM_ATTR buttonISR() {
  uint32_t now = millis();
  if (now - g_lastButtonPressMs < 300) {
    return;
  }
  g_lastButtonPressMs = now;

  g_systemEnabled = !g_systemEnabled;

  if (g_systemEnabled) {
    Speaker::pauseToggle();
  } else {
    Speaker::pauseToggle();
  }
}

/*
 * Initializes a VL53L0X Time-of-Flight sensor with a specific I2C address
 * Uses the XSHUT pin to power cycle the sensor before setting its new address.
 * This allows multiple sensors on the same I2C bus with unique addresses.
 */
bool initSensor(Adafruit_VL53L0X &sensor, int xshutPin, uint8_t newAddr) {
  pinMode(xshutPin, OUTPUT);
  digitalWrite(xshutPin, LOW);
  delay(5);
  digitalWrite(xshutPin, HIGH);
  delay(5);
  if (!sensor.begin(newAddr, false, &Wire)) {
    Logger::logf(Logger::Level::Error,
                 "Failed to init VL53L0X at addr 0x%02X",
                 newAddr);
    LOGGER_DEBUG(
      Serial.print("Failed to init VL53L0X at addr 0x");
      Serial.println(newAddr, HEX);
    );
    return false;
  }
  return true;
}

/*
 * Reads distance measurement from a VL53L0X sensor
 * Returns the distance in millimeters, or 0xFFFF if the reading is invalid
 * (e.g., out of range or sensor error).
 */
uint16_t readVL(Adafruit_VL53L0X &s) {
  VL53L0X_RangingMeasurementData_t measure;
  s.rangingTest(&measure, false);
  if (measure.RangeStatus != 4) {
    return measure.RangeMilliMeter;
  }
  return 0xFFFF;
}

/*
 * FreeRTOS task that continuously reads sensor data and processes gestures
 * Runs at approximately 50 Hz to capture hand movements. When a gesture episode
 * is detected and classified, it triggers the corresponding music control action.
 */
void gestureTask(void* arg) {
  for (;;) {
    if (!g_systemEnabled) {
      Logger::ledError();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

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

      LOGGER_DEBUG(
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
      );

      if (dir == GestureDir::None) {
        Logger::ledWarn();
      } else {
        Logger::ledBusy();
      }

      switch (dir) {
        case GestureDir::Left:
          Logger::log(Logger::Level::Info, "Gesture recognized: LEFT");
          LOGGER_DEBUG(Serial.println("LEFT -> prevTrack()"));
          Speaker::prevTrack();
          break;

        case GestureDir::Right:
          Logger::log(Logger::Level::Info, "Gesture recognized: RIGHT");
          LOGGER_DEBUG(Serial.println("RIGHT -> nextTrack()"));
          Speaker::nextTrack();
          break;

        case GestureDir::Up:
          Logger::log(Logger::Level::Info, "Gesture recognized: UP");
          LOGGER_DEBUG(Serial.println("UP -> volumeUp()"));
          Speaker::volumeUp();
          break;

        case GestureDir::Down:
          Logger::log(Logger::Level::Info, "Gesture recognized: DOWN");
          LOGGER_DEBUG(Serial.println("DOWN -> volumeDown()"));
          Speaker::volumeDown();
          break;

        case GestureDir::Tap:
          Logger::log(Logger::Level::Info, "Gesture recognized: TAP");
          LOGGER_DEBUG(Serial.println("TAP -> pauseToggle()"));
          Speaker::pauseToggle();
          break;

        default:
          LOGGER_DEBUG(Serial.println("NONE"));
          break;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/*
 * Arduino setup function - initializes all hardware and starts tasks
 * Sets up sensors, SD card, audio output, WiFi file manager, and gesture recognition.
 */
void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!g_sdMutex) {
    g_sdMutex = xSemaphoreCreateMutex();
  }

  Logger::init(g_sdMutex, "/system.log", LED_R, LED_G, LED_B);

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

  LOGGER_DEBUG(Serial.println("VL53L0X triangle + gesture episode detector ready"));

  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS, SPI, 10000000)) {
    Logger::log(Logger::Level::Error, "SD init failed");
    LOGGER_DEBUG(Serial.println("SD init failed"));
    while (true) vTaskDelay(portMAX_DELAY);
  }

  if (!Speaker::initMax98357A(8, 22, 15, 44100)) {
    Logger::log(Logger::Level::Error, "I2S init failed");
    LOGGER_DEBUG(Serial.println("I2S init failed"));
    while (true) vTaskDelay(portMAX_DELAY);
  }

  Speaker::setPlaylist(kTracks, kNumTracks);
  Speaker::startPlayer();  // spawns audio FreeRTOS task inside Speaker

  // Setup button interrupt with highest priority
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);

  xTaskCreate(
    gestureTask,
    "gestureTask",
    4096,
    nullptr,
    1,
    nullptr
  );
}

/*
 * Arduino main loop - runs indefinitely
 * All work is done in FreeRTOS tasks, so this just sleeps forever.
 */
void loop() {
  vTaskDelay(portMAX_DELAY);
}
