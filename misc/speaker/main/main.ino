/*
/*
 simple i2s audio test
 checks sd card and max98357a speaker by playing one wav file
 this is mainly for hardware bring up before gesture control
*/

#include <SPI.h>
#include <SD.h>
#include "Speaker.hpp"

#define SD_CS 9

void setup()
{
	Serial.begin(115200);
	delay(1000);
	Serial.println("max98357a i2s speaker test starting");

	// set up sd card interface on the known working pins
	SPI.begin(18, 19, 23, SD_CS);
	if (!SD.begin(SD_CS))
	{
		Serial.println("sd init failed so we stop here");
		while (true)
		{
			delay(1000);
		}
	}
	Serial.println("sd init ok");

	// list all files on sd so we know the card is readable
	File aRootDirectory = SD.open("/");
	Serial.println("files on sd");
	while (true)
	{
		File aCurrentFile = aRootDirectory.openNextFile();
		if (!aCurrentFile)
		{
			break;
		}
		Serial.print("  ");
		Serial.print(aCurrentFile.name());
		Serial.print("  ");
		Serial.println(aCurrentFile.size());
		aCurrentFile.close();
	}
	Serial.println("----");

	// set up i2s to talk to the max98357a board
	// wiring is bclk on 8 lrc on 22 and data on 15
	bool aI2sInitOk = Speaker::initMax98357A(
		8,
		22,
		15,
		44100
	);
	if (!aI2sInitOk)
	{
		Serial.println("i2s or max98357a init failed so we stop here");
		while (true)
		{
			delay(1000);
		}
	}
	Serial.println("i2s init ok");

	// choose one test wav file to play from the sd card
	const char *aTestFilePath = "/afro-11-324020.wav";
	Serial.print("playing ");
	Serial.println(aTestFilePath);

	bool aPlaybackOk = Speaker::playWavI2S(aTestFilePath);
	if (!aPlaybackOk)
	{
		Serial.println("playWavI2S failed so audio did not play");
	}
	else
	{
		Serial.println("done playing test file");
	}
}

void loop()
{
	// nothing here playback happens once inside setup
}
