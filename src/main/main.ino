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

Adafruit_VL53L0X theSensorL;
Adafruit_VL53L0X theSensorR;
Adafruit_VL53L0X theSensorT;

GesturePreprocessor theGesturePreprocessor;

const char* kTracks[] =
{
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
void IRAM_ATTR buttonISR()
{
  uint32_t theNow = millis();
  if (theNow - g_lastButtonPressMs < 300)
  {
    return;
  }
  g_lastButtonPressMs = theNow;

  g_systemEnabled = !g_systemEnabled;

  if (g_systemEnabled)
  {
    Speaker::pauseToggle();
  }
  else
  {
    Speaker::pauseToggle();
  }
}

/*
 * Initializes a VL53L0X Time-of-Flight sensor with a specific I2C address
 * Uses the XSHUT pin to power cycle the sensor before setting its new address.
 * This allows multiple sensors on the same I2C bus with unique addresses.
 */
bool initSensor(Adafruit_VL53L0X &aSensor, int anXshutPin, uint8_t aNewAddr)
{
  pinMode(anXshutPin, OUTPUT);
  digitalWrite(anXshutPin, LOW);
  delay(5);
  digitalWrite(anXshutPin, HIGH);
  delay(5);
  if (!aSensor.begin(aNewAddr, false, &Wire))
  {
    Logger::logf(Logger::Level::Error,
                 "Failed to init VL53L0X at addr 0x%02X",
                 aNewAddr);
    LOGGER_DEBUG(
      Serial.print("Failed to init VL53L0X at addr 0x");
      Serial.println(aNewAddr, HEX);
    );
    return false;
  }
  return true;
}

/*
 * Reads distance measurement from a VL53L0X sensor
 * Returns the distance in millimeters, or 0xFFFF if the reading is invalid
 */
uint16_t readVL(Adafruit_VL53L0X &aSensor)
{
  VL53L0X_RangingMeasurementData_t theMeasure;
  aSensor.rangingTest(&theMeasure, false);
  if (theMeasure.RangeStatus != 4)
  {
    return theMeasure.RangeMilliMeter;
  }
  return 0xFFFF;
}

/*
 * FreeRTOS task that continuously reads sensor data and processes gestures
 * Runs at approximately 50 Hz to capture hand movements. When a gesture episode
 * is detected and classified, it triggers the corresponding music control action.
 */
void gestureTask(void* anArg)
{
  for (;;)
  {
    if (!g_systemEnabled)
    {
      Logger::ledError();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    uint32_t theNow = millis();

    uint16_t theDistanceL = readVL(theSensorL);
    uint16_t theDistanceR = readVL(theSensorR);
    uint16_t theDistanceT = readVL(theSensorT);

    GestureEvent theEvent = theGesturePreprocessor.update(theDistanceL, theDistanceR, theDistanceT, theNow);
    if (theEvent == GestureEvent::EpisodeReady)
    {
      const GestureEpisode &theEpisode = theGesturePreprocessor.lastEpisode();

      uint32_t theDuration = theEpisode.tEndMs - theEpisode.tStartMs;

      // calculate how much each sensor value changed during the gesture
      uint16_t theSwingL = 0;
      if (theEpisode.dMin[0] != 0xFFFF)
      {
        theSwingL = theEpisode.dMax[0] - theEpisode.dMin[0];
      }

      uint16_t theSwingR = 0;
      if (theEpisode.dMin[1] != 0xFFFF)
      {
        theSwingR = theEpisode.dMax[1] - theEpisode.dMin[1];
      }

      uint16_t theSwingT = 0;
      if (theEpisode.dMin[2] != 0xFFFF)
      {
        theSwingT = theEpisode.dMax[2] - theEpisode.dMin[2];
      }

      GestureDir theDirection = classifyEpisode(theEpisode);

      LOGGER_DEBUG(
        Serial.print("EPISODE dur=");
        Serial.print(theDuration);
        Serial.print("ms  swing(L,R,T)=");
        Serial.print(theSwingL); Serial.print(",");
        Serial.print(theSwingR); Serial.print(",");
        Serial.print(theSwingT); Serial.print("  maxV(L,R,T)=");
        Serial.print(theEpisode.maxApproachVel[0]); Serial.print(",");
        Serial.print(theEpisode.maxApproachVel[1]); Serial.print(",");
        Serial.print(theEpisode.maxApproachVel[2]);
        Serial.print("  -> ");
      );

      if (theDirection == GestureDir::None)
      {
        Logger::ledWarn();
      }
      else
      {
        Logger::ledBusy();
      }

      switch (theDirection)
      {
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
void setup()
{
  Serial.begin(115200);
  delay(1000);

  if (!g_sdMutex)
  {
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

  initSensor(theSensorL, XSHUT_L, ADDR_L);
  initSensor(theSensorR, XSHUT_R, ADDR_R);
  initSensor(theSensorT, XSHUT_T, ADDR_T);

  LOGGER_DEBUG(Serial.println("VL53L0X triangle + gesture episode detector ready"));

  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS, SPI, 10000000))
  {
    Logger::log(Logger::Level::Error, "SD init failed");
    LOGGER_DEBUG(Serial.println("SD init failed"));
    while (true)
    {
      vTaskDelay(portMAX_DELAY);
    }
  }

  if (!Speaker::initMax98357A(8, 22, 15, 44100))
  {
    Logger::log(Logger::Level::Error, "I2S init failed");
    LOGGER_DEBUG(Serial.println("I2S init failed"));
    while (true)
    {
      vTaskDelay(portMAX_DELAY);
    }
  }

  Speaker::setPlaylist(kTracks, kNumTracks);
  Speaker::startPlayer();  // spawns audio FreeRTOS task inside Speaker

  // setup button interrupt with highest priority
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
 * Arduino main loop. runs indefinitely
 * All work is done in FreeRTOS tasks, so this just sleeps forever.
 */
void loop()
{
  vTaskDelay(portMAX_DELAY);
}