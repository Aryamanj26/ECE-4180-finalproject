/*
 * wifi file manager for esp32
 * 
 * provides a web based interface for managing files on the sd card
 * creates a wifi access point and hosts a simple web server that allows
 * uploading downloading and deleting wav files through a browser
 * useful for updating the music library without removing the sd card
 * 
 * web server implementation uses standard esp32 webserver library
 */

#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>

#include <Logger.hpp>

extern "C"
{
	#include "freertos/FreeRTOS.h"
	#include "freertos/semphr.h"
}

namespace WebFileManager
{

// internal shared state using singleton pattern

inline WebServer& getServer()
{
	static WebServer theServer(80);
	return theServer;
}

inline SemaphoreHandle_t& getSdMutex()
{
	static SemaphoreHandle_t theMutex= nullptr;
	return theMutex;
}

inline File& getUploadFile()
{
	static File theFile;
	return theFile;
}

inline const char*& getSsid()
{
	static const char* theSsid= "ESP32-Music";
	return theSsid;
}

inline const char*& getPassword()
{
	static const char* thePassword= "12345678";
	return thePassword;
}

// converts a file size in bytes to a human readable string
// automatically selects appropriate units based on size
inline String humanSize(uint64_t someBytes)
{
	if (someBytes < 1024)
	{
		return String(someBytes) + " B";
	}
	
	double theKilobytes= someBytes / 1024.0;
	if (theKilobytes < 1024)
	{
		return String(theKilobytes, 1) + " KB";
	}
	
	double theMegabytes= theKilobytes / 1024.0;
	if (theMegabytes < 1024)
	{
		return String(theMegabytes, 1) + " MB";
	}
	
	double theGigabytes= theMegabytes / 1024.0;
	return String(theGigabytes, 1) + " GB";
}

// generates an html table listing all files on the sd card
// includes file names sizes and action buttons for each file
inline String makeFileTable()
{
	String theHtml;

	SemaphoreHandle_t theMutex= getSdMutex();
	if (!theMutex || xSemaphoreTake(theMutex, pdMS_TO_TICKS(200)) != pdTRUE)
	{
		return "<p>SD busy or not available.</p>";
	}

	File theRoot= SD.open("/");
	if (!theRoot)
	{
		xSemaphoreGive(theMutex);
		return "<p>Failed to open SD root.</p>";
	}

	theHtml+= "<table border='1' cellpadding='4' cellspacing='0'>";
	theHtml+= "<tr><th>Name</th><th>Size</th><th>Actions</th></tr>";

	// iterate through all files in root directory
	while (true)
	{
		File theFile= theRoot.openNextFile();
		if (!theFile)
		{
			break;
		}
		
		String theName= String(theFile.name());
		uint64_t theSize= theFile.size();
		theFile.close();

		String theDisplayName= theName;
		// remove leading slash for display
		if (theDisplayName.startsWith("/"))
		{
			theDisplayName.remove(0, 1);
		}

		theHtml+= "<tr>";
		theHtml+= "<td>" + theDisplayName + "</td>";
		theHtml+= "<td>" + humanSize(theSize) + "</td>";
		theHtml+= "<td>";

		// download button form
		theHtml+= "<form style='display:inline' method='GET' action='/download'>";
		theHtml+= "<input type='hidden' name='name' value='" + theDisplayName + "'>";
		theHtml+= "<input type='submit' value='Download'>";
		theHtml+= "</form>";

		theHtml+= "&nbsp;";

		// delete button form with confirmation
		theHtml+= "<form style='display:inline' method='POST' action='/delete' ";
		theHtml+= "onsubmit='return confirm(\"Delete " + theDisplayName + " ?\");'>";
		theHtml+= "<input type='hidden' name='name' value='" + theDisplayName + "'>";
		theHtml+= "<input type='submit' value='Delete'>";
		theHtml+= "</form>";

		theHtml+= "</td>";
		theHtml+= "</tr>";
	}

	theHtml+= "</table>";

	xSemaphoreGive(theMutex);
	return theHtml;
}

// serves the main file manager page
// displays the upload form and lists all files currently on the sd card
inline void handleRoot()
{
	String thePage;
	thePage+= "<html><head><title>ESP32 SD File Manager</title></head><body>";
	thePage+= "<h2>ESP32 SD File Manager</h2>";

	// upload form
	thePage+= "<h3>Upload file</h3>";
	thePage+= "<form method='POST' action='/upload' enctype='multipart/form-data'>";
	thePage+= "File: <input type='file' name='upload'><br><br>";
	thePage+= "<input type='submit' value='Upload'>";
	thePage+= "</form>";

	// file list
	thePage+= "<h3>Files on SD</h3>";
	thePage+= makeFileTable();

	thePage+= "<br><hr><small>Connect to WiFi \"";
	thePage+= getSsid();
	thePage+= "\" and open http://192.168.4.1/</small>";
	thePage+= "</body></html>";

	getServer().send(200, "text/html", thePage);
}

// handles file uploads from the web interface
// receives file data in chunks and writes it to the sd card
inline void handleUpload()
{
	HTTPUpload& theUpload= getServer().upload();
	File& theUploadFile= getUploadFile();

	SemaphoreHandle_t theMutex= getSdMutex();

	if (theUpload.status== UPLOAD_FILE_START)
	{
		String theFilename= "/" + theUpload.filename;
		Serial.print("Upload start: ");
		Serial.println(theFilename);

		// acquire mutex before sd operations
		if (theMutex && xSemaphoreTake(theMutex, pdMS_TO_TICKS(500))== pdTRUE)
		{
			// remove existing file if it exists
			if (SD.exists(theFilename))
			{
				SD.remove(theFilename);
			}
			theUploadFile= SD.open(theFilename, FILE_WRITE);
			xSemaphoreGive(theMutex);
		}
	}
	else if (theUpload.status== UPLOAD_FILE_WRITE)
	{
		// write incoming data chunks to file
		if (theUploadFile)
		{
			theUploadFile.write(theUpload.buf, theUpload.currentSize);
		}
	}
	else if (theUpload.status== UPLOAD_FILE_END)
	{
		if (theUploadFile)
		{
			theUploadFile.close();
			Serial.print("Upload end, size= ");
			Serial.println(theUpload.totalSize);
		}
		else
		{
			Logger::log(Logger::Level::Error, "Upload failed: file not open");
			LOGGER_DEBUG(Serial.println("Upload failed: file not open"));
		}

		String thePage;
		thePage+= "<html><body>";
		thePage+= "<p>Upload finished: " + String(theUpload.filename) + "</p>";
		thePage+= "<a href='/'>Back to file manager</a>";
		thePage+= "</body></html>";

		getServer().send(200, "text/html", thePage);
	}
}

// handles file deletion requests from the web interface
// removes the specified file from the sd card and redirects back to the main page
inline void handleDelete()
{
	if (!getServer().hasArg("name"))
	{
		getServer().send(400, "text/plain", "Missing 'name' parameter");
		return;
	}

	String theShortName= getServer().arg("name");
	String theFullPath= "/" + theShortName;

	Serial.print("Delete request: ");
	Serial.println(theFullPath);

	SemaphoreHandle_t theMutex= getSdMutex();
	if (theMutex && xSemaphoreTake(theMutex, pdMS_TO_TICKS(200))== pdTRUE)
	{
		if (SD.exists(theFullPath))
		{
			SD.remove(theFullPath);
			Serial.println("File deleted.");
		}
		else
		{
			Logger::logf(Logger::Level::Warn, "Delete request failed: %s not found", theFullPath.c_str());
			LOGGER_DEBUG(Serial.println("File not found."));
		}
		xSemaphoreGive(theMutex);
	}

	// redirect back to main page after deletion
	getServer().sendHeader("Location", "/", true);
	getServer().send(303);
}

// handles file download requests from the web interface
// streams the requested file from sd card to the client browser
inline void handleDownload()
{
	if (!getServer().hasArg("name"))
	{
		getServer().send(400, "text/plain", "Missing 'name' parameter");
		return;
	}

	String theShortName= getServer().arg("name");
	String theFullPath= "/" + theShortName;

	Serial.print("Download request: ");
	Serial.println(theFullPath);

	SemaphoreHandle_t theMutex= getSdMutex();
	if (!theMutex || xSemaphoreTake(theMutex, pdMS_TO_TICKS(200)) != pdTRUE)
	{
		getServer().send(503, "text/plain", "SD busy");
		return;
	}

	if (!SD.exists(theFullPath))
	{
		xSemaphoreGive(theMutex);
		getServer().send(404, "text/plain", "File not found");
		return;
	}

	File theFile= SD.open(theFullPath, FILE_READ);
	if (!theFile)
	{
		xSemaphoreGive(theMutex);
		getServer().send(500, "text/plain", "Failed to open file");
		return;
	}

	getServer().streamFile(theFile, "application/octet-stream");
	theFile.close();
	xSemaphoreGive(theMutex);
}

// empty handler for upload post endpoint
// actual upload handling is done in handleUpload function
inline void handleUploadPost()
{
	// nothing here actual handling is in handleUpload
}

// initializes and starts the wifi file manager web server
// creates a wifi access point and sets up http request handlers
// connect to the specified ssid and navigate to http://192.168.4.1/
inline void begin(SemaphoreHandle_t anSdMutex, const char* anSsid= "ESP32-Music", const char* aPassword= "12345678")
{
	getSdMutex()= anSdMutex;
	getSsid()= anSsid;
	getPassword()= aPassword;

	WiFi.mode(WIFI_AP);
	WiFi.softAP(getSsid(), getPassword());

	Serial.println("SoftAP started");
	Serial.print("SSID: ");
	Serial.println(getSsid());
	Serial.print("Password: ");
	Serial.println(getPassword());
	Serial.println("Open http://192.168.4.1/ in your browser.");

	getServer().on("/", HTTP_GET, handleRoot);
	getServer().on("/upload", HTTP_POST, handleUploadPost, handleUpload);
	getServer().on("/delete", HTTP_POST, handleDelete);
	getServer().on("/download", HTTP_GET, handleDownload);

	getServer().begin();
}

// processes incoming web requests
// call regularly from main loop to handle pending http requests from clients
inline void loopOnce()
{
	getServer().handleClient();
}

// shuts down the web server and disables wifi
// call this when the file manager is no longer needed
inline void stop()
{
	getServer().stop();
	WiFi.mode(WIFI_OFF);
}

}