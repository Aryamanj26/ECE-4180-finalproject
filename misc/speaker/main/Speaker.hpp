/*
 * i2s audio playback library for esp32
 * 
 * handles wav file parsing and playback over i2s to max98357a amplifier
 * supports playlist management volume control and gesture based track navigation
 * 
 * wav format specification http://soundfile.sapp.org/doc/WaveFormat/
 * i2s implementation uses esp32 i2s library
 */

#pragma once
#include <Arduino.h>
#include <SD.h>
#include <ESP_I2S.h>
#include <string.h>

namespace Speaker
{

	struct WavInfo
	{
		uint32_t sampleRate = 0;
		uint16_t numChannels = 0;
		uint16_t bitsPerSample = 0;
		uint32_t dataOffset = 0;
		uint32_t dataSize = 0;
	};

	// parse wav file header and extract audio parameters
	// based on riff wave format specification
	inline bool parseWavHeader(File &aFile, WavInfo &anInfo)
	{
		if (!aFile)
		{
			return false;
		}

		aFile.seek(0);
		uint8_t theHeader[44];
		if (aFile.read(theHeader, sizeof(theHeader)) != sizeof(theHeader))
		{
			return false;
		}

		// verify riff wave format markers
		if (memcmp(theHeader + 0, "RIFF", 4) != 0)
		{
			return false;
		}
		if (memcmp(theHeader + 8, "WAVE", 4) != 0)
		{
			return false;
		}
		if (memcmp(theHeader + 12, "fmt ", 4) != 0)
		{
			return false;
		}

		uint32_t theFmtChunkSize =
			(uint32_t)theHeader[16] |
			((uint32_t)theHeader[17] << 8) |
			((uint32_t)theHeader[18] << 16) |
			((uint32_t)theHeader[19] << 24);

		uint16_t theAudioFormat =
			(uint16_t)theHeader[20] |
			((uint16_t)theHeader[21] << 8);
		uint16_t theNumChannels =
			(uint16_t)theHeader[22] |
			((uint16_t)theHeader[23] << 8);
		uint32_t theSampleRate =
			(uint32_t)theHeader[24] |
			((uint32_t)theHeader[25] << 8) |
			((uint32_t)theHeader[26] << 16) |
			((uint32_t)theHeader[27] << 24);
		uint16_t theBitsPerSample =
			(uint16_t)theHeader[34] |
			((uint16_t)theHeader[35] << 8);

		// only pcm format is supported
		if (theAudioFormat != 1)
		{
			return false;
		}

		anInfo.sampleRate = theSampleRate;
		anInfo.numChannels = theNumChannels;
		anInfo.bitsPerSample = theBitsPerSample;

		// find data chunk since fmt chunk can be larger than 16 bytes
		uint32_t thePosition = 12 + 8 + theFmtChunkSize;
		if (!aFile.seek(thePosition))
		{
			return false;
		}

		// keep searching through chunks until we find the data chunk
		while (true)
		{
			uint8_t theChunkHeader[8];
			if (aFile.read(theChunkHeader, 8) != 8)
			{
				return false;
			}

			uint32_t theChunkSize =
				(uint32_t)theChunkHeader[4] |
				((uint32_t)theChunkHeader[5] << 8) |
				((uint32_t)theChunkHeader[6] << 16) |
				((uint32_t)theChunkHeader[7] << 24);

			if (memcmp(theChunkHeader, "data", 4) == 0)
			{
				anInfo.dataOffset = aFile.position();
				anInfo.dataSize = theChunkSize;
				return true;
			}

			thePosition = aFile.position() + theChunkSize;
			if (!aFile.seek(thePosition))
			{
				return false;
			}
		}
	}

	static I2SClass gI2s;
	static bool gI2sInitialized = false;
	static uint32_t gI2sRate = 44100;

	// initialize i2s interface for max98357a amplifier
	// sets up pins and configures for 16 bit mono audio output
	inline bool initMax98357A(int aBclkPin, int aLrckPin, int aDataPin, uint32_t aDefaultRate = 44100)
	{
		gI2sRate = aDefaultRate;
		gI2s.setPins(aBclkPin, aLrckPin, aDataPin);

		bool isOk = gI2s.begin(I2S_MODE_STD, gI2sRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
		gI2sInitialized = isOk;
		if (!isOk)
		{
			Serial.println("Speaker::initMax98357A: i2s.begin failed");
		}
		return isOk;
	}

	// reconfigure i2s sample rate if different from current rate
	inline bool ensureSampleRate(uint32_t aRate)
	{
		if (!gI2sInitialized)
		{
			return false;
		}
		if (aRate == 0 || aRate == gI2sRate)
		{
			return true;
		}

		if (!gI2s.configureTX(aRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO))
		{
			Serial.println("Speaker::ensureSampleRate: configureTX failed");
			return false;
		}
		gI2sRate = aRate;
		return true;
	}

	// play a single wav file blocking mode for testing
	inline bool playWavI2S(const char *aPath)
	{
		if (!gI2sInitialized)
		{
			Serial.println("playWavI2S: I2S not initialized");
			return false;
		}

		File theFile = SD.open(aPath);
		if (!theFile)
		{
			Serial.print("playWavI2S: failed to open ");
			Serial.println(aPath);
			return false;
		}

		WavInfo theInfo;
		if (!parseWavHeader(theFile, theInfo))
		{
			Serial.println("playWavI2S: invalid WAV header");
			theFile.close();
			return false;
		}

		// accept mono or stereo 16 bit only
		if (theInfo.bitsPerSample != 16 || (theInfo.numChannels != 1 && theInfo.numChannels != 2))
		{
			Serial.print("playWavI2S: unsupported format (ch=");
			Serial.print(theInfo.numChannels);
			Serial.print(", bits=");
			Serial.print(theInfo.bitsPerSample);
			Serial.println(")");
			theFile.close();
			return false;
		}

		if (!ensureSampleRate(theInfo.sampleRate))
		{
			Serial.println("playWavI2S: failed to set sample rate");
			theFile.close();
			return false;
		}

		if (!theFile.seek(theInfo.dataOffset))
		{
			Serial.println("playWavI2S: failed to seek to data");
			theFile.close();
			return false;
		}

		Serial.print("playWavI2S: sampleRate=");
		Serial.print(theInfo.sampleRate);
		Serial.print(" Hz, channels=");
		Serial.println(theInfo.numChannels);

		const uint8_t theChannels = theInfo.numChannels;
		const uint8_t theBytesPerSample = 2 * theChannels;
		uint32_t theRemainingBytes = theInfo.dataSize;

		const size_t MAX_FRAMES = 256;
		int16_t theInputBuffer[MAX_FRAMES * 2];
		int16_t theOutputBuffer[MAX_FRAMES];

		while (theRemainingBytes > 0)
		{
			uint32_t theBytesLeft = theRemainingBytes;
			size_t theMaxBytes = MAX_FRAMES * theBytesPerSample;
			size_t theBytesToRead = (theBytesLeft > theMaxBytes) ? theMaxBytes : theBytesLeft;

			size_t theBytesRead = theFile.read((uint8_t*)theInputBuffer, theBytesToRead);
			if (!theBytesRead)
			{
				break;
			}

			// convert stereo to mono if needed by averaging channels
			size_t theFramesRead = theBytesRead / theBytesPerSample;
			for (size_t i = 0; i < theFramesRead; i++)
			{
				int16_t theMonoSample;
				if (theChannels == 1)
				{
					theMonoSample = theInputBuffer[i];
				}
				else
				{
					int16_t theLeftChannel = theInputBuffer[2 * i + 0];
					int16_t theRightChannel = theInputBuffer[2 * i + 1];
					int32_t theMixedSample = (int32_t)theLeftChannel + (int32_t)theRightChannel;
					theMonoSample = (int16_t)(theMixedSample / 2);
				}
				theOutputBuffer[i] = theMonoSample;
			}

			size_t theOutputBytes = theFramesRead * 2;
			size_t theBytesWritten = 0;
			// keep writing until all bytes are sent to i2s
			while (theBytesWritten < theOutputBytes)
			{
				theBytesWritten += gI2s.write(((uint8_t*)theOutputBuffer) + theBytesWritten, theOutputBytes - theBytesWritten);
			}

			theRemainingBytes -= theBytesRead;
			yield();
		}

		theFile.close();
		return true;
	}

	inline bool playWavI2S(const String &aPath)
	{
		return playWavI2S(aPath.c_str());
	}

	// background playback globals
	static const size_t MAX_TRACKS = 16;

	static const char* gPlaylist[MAX_TRACKS];
	static size_t gPlaylistCount = 0;
	static size_t gCurrentTrackIndex = 0;

	// player control commands set by gesture code
	static volatile bool gCommandNext = false;
	static volatile bool gCommandPrev = false;
	static volatile bool gCommandPauseToggle = false;
	static volatile int gCommandVolumeDelta = 0;
	static volatile bool gStopRequested = false;
	static volatile bool gIsPaused = false;

	static float gVolume = 1.0f;

	static TaskHandle_t gAudioTaskHandle = nullptr;

	// set the playlist of wav files to play
	inline void setPlaylist(const char* const* someFiles, size_t aCount)
	{
		if (aCount > MAX_TRACKS)
		{
			aCount = MAX_TRACKS;
		}
		for (size_t i = 0; i < aCount; i++)
		{
			gPlaylist[i] = someFiles[i];
		}
		gPlaylistCount = aCount;
		gCurrentTrackIndex = 0;
	}

	// control functions to call from gesture code
	inline void nextTrack()
	{
		gCommandNext = true;
	}
	
	inline void prevTrack()
	{
		gCommandPrev = true;
	}
	
	inline void pauseToggle()
	{
		gCommandPauseToggle = true;
	}
	
	inline void stopPlayback()
	{
		gStopRequested = true;
	}
	
	inline void volumeUp()
	{
		gCommandVolumeDelta++;
	}
	
	inline void volumeDown()
	{
		gCommandVolumeDelta--;
	}

	// clamps 32 bit value to 16 bit range to prevent overflow
	inline int16_t clamp16(int32_t aValue)
	{
		if (aValue > 32767)
		{
			return 32767;
		}
		if (aValue < -32768)
		{
			return -32768;
		}
		return (int16_t)aValue;
	}

	// background task for continuous playlist playback
	// runs in freertos task and handles all audio streaming
	static void audioTask(void* /*anArgument*/)
	{
		Serial.println("Speaker::audioTask: started");

		for (;;)
		{
			if (gStopRequested)
			{
				break;
			}

			if (gPlaylistCount == 0 || !gI2sInitialized)
			{
				vTaskDelay(pdMS_TO_TICKS(100));
				continue;
			}

			const char* theCurrentPath = gPlaylist[gCurrentTrackIndex];
			Serial.print("Speaker::audioTask: opening ");
			Serial.println(theCurrentPath);

			File theFile = SD.open(theCurrentPath);
			if (!theFile)
			{
				Serial.println("Speaker::audioTask: failed to open file, skipping");
				gCurrentTrackIndex = (gCurrentTrackIndex + 1) % gPlaylistCount;
				vTaskDelay(pdMS_TO_TICKS(50));
				continue;
			}

			WavInfo theInfo;
			if (!parseWavHeader(theFile, theInfo))
			{
				Serial.println("Speaker::audioTask: invalid WAV header, skipping");
				theFile.close();
				gCurrentTrackIndex = (gCurrentTrackIndex + 1) % gPlaylistCount;
				vTaskDelay(pdMS_TO_TICKS(50));
				continue;
			}

			if (theInfo.bitsPerSample != 16 || (theInfo.numChannels != 1 && theInfo.numChannels != 2))
			{
				Serial.print("Speaker::audioTask: unsupported format (ch=");
				Serial.print(theInfo.numChannels);
				Serial.print(", bits=");
				Serial.print(theInfo.bitsPerSample);
				Serial.println("), skipping");
				theFile.close();
				gCurrentTrackIndex = (gCurrentTrackIndex + 1) % gPlaylistCount;
				vTaskDelay(pdMS_TO_TICKS(50));
				continue;
			}

			if (!ensureSampleRate(theInfo.sampleRate))
			{
				Serial.println("Speaker::audioTask: failed to set sample rate, skipping");
				theFile.close();
				gCurrentTrackIndex = (gCurrentTrackIndex + 1) % gPlaylistCount;
				vTaskDelay(pdMS_TO_TICKS(50));
				continue;
			}

			if (!theFile.seek(theInfo.dataOffset))
			{
				Serial.println("Speaker::audioTask: seek to data failed, skipping");
				theFile.close();
				gCurrentTrackIndex = (gCurrentTrackIndex + 1) % gPlaylistCount;
				vTaskDelay(pdMS_TO_TICKS(50));
				continue;
			}

			Serial.print("Speaker::audioTask: playing, rate=");
			Serial.print(theInfo.sampleRate);
			Serial.print(" Hz, channels=");
			Serial.println(theInfo.numChannels);

			const uint8_t theChannels = theInfo.numChannels;
			const uint8_t theBytesPerSample = 2 * theChannels;
			uint32_t theRemainingBytes = theInfo.dataSize;

			const size_t MAX_FRAMES = 256;
			int16_t theInputBuffer[MAX_FRAMES * 2];
			int16_t theOutputBuffer[MAX_FRAMES];

			bool isTrackDone = false;
			bool shouldAdvanceNext = false;
			bool shouldAdvancePrev = false;

			while (!isTrackDone && !gStopRequested)
			{
				// handle pause state while still processing volume and track changes
				if (gIsPaused)
				{
					vTaskDelay(pdMS_TO_TICKS(10));
					
					// volume changes still work while paused
					if (gCommandVolumeDelta != 0)
					{
						int theDelta = gCommandVolumeDelta;
						gCommandVolumeDelta = 0;
						gVolume += 0.1f * theDelta;
						if (gVolume < 0.0f)
						{
							gVolume = 0.0f;
						}
						if (gVolume > 2.0f)
						{
							gVolume = 2.0f;
						}
						Serial.print("Speaker::volume=");
						Serial.println(gVolume);
					}
					
					if (gCommandPauseToggle)
					{
						gCommandPauseToggle = false;
						gIsPaused = false;
						Serial.println("Speaker::unpause");
					}
					
					if (gCommandNext)
					{
						gCommandNext = false;
						shouldAdvanceNext = true;
						break;
					}
					
					if (gCommandPrev)
					{
						gCommandPrev = false;
						shouldAdvancePrev = true;
						break;
					}
					
					continue;
				}

				if (theRemainingBytes == 0)
				{
					isTrackDone = true;
					break;
				}

				// handle control commands while playing
				if (gCommandPauseToggle)
				{
					gCommandPauseToggle = false;
					gIsPaused = !gIsPaused;
					Serial.println(gIsPaused ? "Speaker::pause" : "Speaker::unpause");
					continue;
				}
				
				if (gCommandNext)
				{
					gCommandNext = false;
					shouldAdvanceNext = true;
					break;
				}
				
				if (gCommandPrev)
				{
					gCommandPrev = false;
					shouldAdvancePrev = true;
					break;
				}
				
				if (gCommandVolumeDelta != 0)
				{
					int theDelta = gCommandVolumeDelta;
					gCommandVolumeDelta = 0;
					gVolume += 0.1f * theDelta;
					if (gVolume < 0.0f)
					{
						gVolume = 0.0f;
					}
					if (gVolume > 2.0f)
					{
						gVolume = 2.0f;
					}
					Serial.print("Speaker::volume=");
					Serial.println(gVolume);
				}

				uint32_t theBytesLeft = theRemainingBytes;
				size_t theMaxBytes = MAX_FRAMES * theBytesPerSample;
				size_t theBytesToRead = (theBytesLeft > theMaxBytes) ? theMaxBytes : theBytesLeft;

				size_t theBytesRead = theFile.read((uint8_t*)theInputBuffer, theBytesToRead);
				if (!theBytesRead)
				{
					isTrackDone = true;
					break;
				}

				// convert to mono and apply volume scaling
				size_t theFramesRead = theBytesRead / theBytesPerSample;
				for (size_t i = 0; i < theFramesRead; i++)
				{
					int16_t theMonoSample;
					if (theChannels == 1)
					{
						theMonoSample = theInputBuffer[i];
					}
					else
					{
						int16_t theLeftChannel = theInputBuffer[2 * i + 0];
						int16_t theRightChannel = theInputBuffer[2 * i + 1];
						int32_t theMixedSample = (int32_t)theLeftChannel + (int32_t)theRightChannel;
						theMonoSample = (int16_t)(theMixedSample / 2);
					}
					int32_t theScaledSample = (int32_t)(theMonoSample * gVolume);
					theOutputBuffer[i] = clamp16(theScaledSample);
				}

				size_t theOutputBytes = theFramesRead * 2;
				size_t theBytesWritten = 0;
				// keep writing until all bytes are sent to i2s
				while (theBytesWritten < theOutputBytes)
				{
					theBytesWritten += gI2s.write(((uint8_t*)theOutputBuffer) + theBytesWritten, theOutputBytes - theBytesWritten);
				}

				theRemainingBytes -= theBytesRead;
				taskYIELD();
			}

			theFile.close();

			if (gStopRequested)
			{
				break;
			}

			// advance to next or previous track based on commands
			if (shouldAdvanceNext)
			{
				gCurrentTrackIndex = (gCurrentTrackIndex + 1) % gPlaylistCount;
			}
			else if (shouldAdvancePrev)
			{
				// wrap around to end if at beginning
				gCurrentTrackIndex = (gCurrentTrackIndex + gPlaylistCount - 1) % gPlaylistCount;
			}
			else
			{
				// normal end of track plays next track
				gCurrentTrackIndex = (gCurrentTrackIndex + 1) % gPlaylistCount;
			}

			vTaskDelay(pdMS_TO_TICKS(10));
		}

		Serial.println("Speaker::audioTask: exiting");
		gAudioTaskHandle = nullptr;
		vTaskDelete(nullptr);
	}

	// start the background audio task
	// creates freertos task that continuously plays through playlist
	inline void startPlayer()
	{
		if (!gI2sInitialized || gPlaylistCount == 0)
		{
			Serial.println("Speaker::startPlayer: I2S not inited or playlist empty");
			return;
		}
		
		if (gAudioTaskHandle)
		{
			Serial.println("Speaker::startPlayer: already running");
			return;
		}

		gStopRequested = false;
		gCommandNext = false;
		gCommandPrev = false;
		gCommandPauseToggle = false;
		gCommandVolumeDelta = 0;
		gIsPaused = false;
		gVolume = 1.0f;

		xTaskCreate(audioTask, "audioPlayer", 4096, nullptr, 1, &gAudioTaskHandle);
	}

}