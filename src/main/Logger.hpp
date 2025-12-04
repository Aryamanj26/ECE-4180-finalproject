/*
 * System Logger and LED Status Indicator
 * 
 * Thread-safe logging to SD card with RGB LED status indication.
 * Uses FreeRTOS semaphores for safe concurrent access.
 * LED colors: green (idle), blue (gesture detected), yellow (weak gesture),
 * cyan (WiFi), red (error/disabled).
 */

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

extern "C"
{
	#include "freertos/FreeRTOS.h"
	#include "freertos/semphr.h"
}

namespace Logger
{

enum class Level : uint8_t
{
	Info,
	Warn,
	Error
};

// internal state using function local statics

inline SemaphoreHandle_t& sdMutexRef()
{
	static SemaphoreHandle_t theHandle = nullptr;
	return theHandle;
}

inline const char*& logPathRef()
{
	static const char* thePath = "/system.log";
	return thePath;
}

inline int& pinRRef()
{
	static int theValue = -1;
	return theValue;
}

inline int& pinGRef()
{
	static int theValue = -1;
	return theValue;
}

inline int& pinBRef()
{
	static int theValue = -1;
	return theValue;
}

inline bool& initializedRef()
{
	static bool isInitialized = false;
	return isInitialized;
}

// led control functions

inline void setLed(bool aRedState, bool aGreenState, bool aBlueState)
{
	int thePinR = pinRRef();
	int thePinG = pinGRef();
	int thePinB = pinBRef();
	if (thePinR < 0 || thePinG < 0 || thePinB < 0)
	{
		return;
	}

	digitalWrite(thePinR, aRedState ? HIGH : LOW);
	digitalWrite(thePinG, aGreenState ? HIGH : LOW);
	digitalWrite(thePinB, aBlueState ? HIGH : LOW);
}

// led status indicators
inline void ledIdle()
{
	setLed(false, true, false);
}

inline void ledBusy()
{
	setLed(false, false, true);
}

inline void ledWarn()
{
	setLed(true, true, false);
}

inline void ledWifi()
{
	setLed(false, true, true);
}

inline void ledError()
{
	setLed(true, false, false);
}

// initialize logger with sd mutex and led pins
inline void init(SemaphoreHandle_t anSdMutex, const char* aLogPath, int aPinR, int aPinG, int aPinB)
{
	sdMutexRef() = anSdMutex;
	if (aLogPath && aLogPath[0] != '\0')
	{
		logPathRef() = aLogPath;
	}

	pinRRef() = aPinR;
	pinGRef() = aPinG;
	pinBRef() = aPinB;

	pinMode(aPinR, OUTPUT);
	pinMode(aPinG, OUTPUT);
	pinMode(aPinB, OUTPUT);

	ledIdle();

	// write startup message
	SemaphoreHandle_t theMutex = sdMutexRef();
	if (theMutex && xSemaphoreTake(theMutex, pdMS_TO_TICKS(10)) == pdTRUE)
	{
		File theFile = SD.open(logPathRef(), FILE_WRITE);
		if (theFile)
		{
			theFile.println("=== Logger started ===");
			theFile.close();
		}
		xSemaphoreGive(theMutex);
	}

	initializedRef() = true;
}

/*
 * Internal helper that writes a timestamped log line to the SD card
 * Uses non-blocking semaphore acquisition to avoid interfering with audio playback.
 */
inline void writeLine(Level aLevel, const char* aLine)
{
	if (!initializedRef() || !aLine)
	{
		return;
	}

	SemaphoreHandle_t theMutex = sdMutexRef();
	if (!theMutex)
	{
		return;
	}

	// dont block long if sd is busy with audio, drop the log
	if (xSemaphoreTake(theMutex, pdMS_TO_TICKS(5)) != pdTRUE)
	{
		return;
	}

	File theFile = SD.open(logPathRef(), FILE_WRITE);
	if (theFile)
	{
		uint32_t theTimestamp = millis();
		const char* theLevelStr =
			(aLevel == Level::Info)  ? "INFO"  :
			(aLevel == Level::Warn)  ? "WARN"  :
			                           "ERROR";

		theFile.print('[');
		theFile.print(theTimestamp);
		theFile.print(" ms][");
		theFile.print(theLevelStr);
		theFile.print("] ");
		theFile.println(aLine);
		theFile.close();
	}
	else
	{
		ledError();
	}

	xSemaphoreGive(theMutex);
}

// public logging api

// log a message with specified severity level
inline void log(Level aLevel, const char* aMessage)
{
	if (!initializedRef() || !aMessage)
	{
		return;
	}

	if (aLevel == Level::Error)
	{
		ledError();
	}

	writeLine(aLevel, aMessage);
}

// log formatted message printf style
inline void logf(Level aLevel, const char* aFormat, ...)
{
	if (!initializedRef() || !aFormat)
	{
		return;
	}

	char theBuffer[160];
	va_list theArgs;
	va_start(theArgs, aFormat);
	vsnprintf(theBuffer, sizeof(theBuffer), aFormat, theArgs);
	va_end(theArgs);

	log(aLevel, theBuffer);
}

}