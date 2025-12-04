/*
 simple sd card file manager over wifi
 creates an access point and simple web page to upload download and delete files
 mostly used for loading music onto the card without pulling it out
*/

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>

const char *aWifiSsid = "ESP32-Music";
const char *aWifiPassword = "12345678";

#define SD_CS 9

WebServer aWebServer(80);
File aUploadFile;

// turn a raw byte size into something human readable like kb or mb
String makeHumanSize(uint64_t aByteCount)
{
	if (aByteCount < 1024)
	{
		return String(aByteCount) + " B";
	}
	double aKiloBytes = aByteCount / 1024.0;
	if (aKiloBytes < 1024)
	{
		return String(aKiloBytes, 1) + " KB";
	}
	double aMegaBytes = aKiloBytes / 1024.0;
	if (aMegaBytes < 1024)
	{
		return String(aMegaBytes, 1) + " MB";
	}
	double aGigaBytes = aMegaBytes / 1024.0;
	return String(aGigaBytes, 1) + " GB";
}

// build a simple html table for every file we find on the sd card
String makeFileTable()
{
	String aHtml;
	File aRootDirectory = SD.open("/");
	if (!aRootDirectory)
	{
		return "<p>failed to open sd root</p>";
	}

	aHtml += "<table border='1' cellpadding='4' cellspacing='0'>";
	aHtml += "<tr><th>name</th><th>size</th><th>actions</th></tr>";

	while (true)
	{
		File aCurrentFile = aRootDirectory.openNextFile();
		if (!aCurrentFile)
		{
			break;
		}

		String aRawName = String(aCurrentFile.name());
		uint64_t aFileSizeBytes = aCurrentFile.size();
		aCurrentFile.close();

		// drop the leading slash so it looks cleaner in the table
		String aDisplayName = aRawName;
		if (aDisplayName.startsWith("/"))
		{
			aDisplayName.remove(0, 1);
		}

		aHtml += "<tr>";
		aHtml += "<td>" + aDisplayName + "</td>";
		aHtml += "<td>" + makeHumanSize(aFileSizeBytes) + "</td>";
		aHtml += "<td>";

		// basic download button form
		aHtml += "<form style='display:inline' method='GET' action='/download'>";
		aHtml += "<input type='hidden' name='name' value='" + aDisplayName + "'>";
		aHtml += "<input type='submit' value='Download'>";
		aHtml += "</form>";

		aHtml += "&nbsp;";

		// delete button that asks for a quick confirm in the browser
		aHtml += "<form style='display:inline' method='POST' action='/delete' onsubmit='return confirm(\"Delete "
		        + aDisplayName + " ?\");'>";
		aHtml += "<input type='hidden' name='name' value='" + aDisplayName + "'>";
		aHtml += "<input type='submit' value='Delete'>";
		aHtml += "</form>";

		aHtml += "</td>";
		aHtml += "</tr>";
	}

	aHtml += "</table>";
	return aHtml;
}

// send the main html page that shows upload form and current files
void handleRoot()
{
	String aPageHtml;
	aPageHtml += "<html><head><title>ESP32 SD File Manager</title></head><body>";
	aPageHtml += "<h2>ESP32 SD File Manager</h2>";

	aPageHtml += "<h3>Upload file</h3>";
	aPageHtml += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
	aPageHtml += "File: <input type='file' name='upload'><br><br>";
	aPageHtml += "<input type='submit' value='Upload'>";
	aPageHtml += "</form>";

	aPageHtml += "<h3>Files on SD</h3>";
	aPageHtml += makeFileTable();

	aPageHtml += "<br><hr><small>connect to wifi \"" + String(aWifiSsid) +
	        "\" and open http://192.168.4.1/</small>";
	aPageHtml += "</body></html>";

	aWebServer.send(200, "text/html", aPageHtml);
}

// receive an upload from the browser in chunks and write it to sd
void handleUpload()
{
	HTTPUpload &aUploadStatus = aWebServer.upload();

	if (aUploadStatus.status == UPLOAD_FILE_START)
	{
		String aFileNamePath = "/" + aUploadStatus.filename;
		Serial.print("upload start ");
		Serial.println(aFileNamePath);

		if (SD.exists(aFileNamePath))
		{
			SD.remove(aFileNamePath);
		}
		aUploadFile = SD.open(aFileNamePath, FILE_WRITE);
	}
	else if (aUploadStatus.status == UPLOAD_FILE_WRITE)
	{
		if (aUploadFile)
		{
			aUploadFile.write(aUploadStatus.buf, aUploadStatus.currentSize);
		}
	}
	else if (aUploadStatus.status == UPLOAD_FILE_END)
	{
		if (aUploadFile)
		{
			aUploadFile.close();
			Serial.print("upload end size = ");
			Serial.println(aUploadStatus.totalSize);
		}
		else
		{
			Serial.println("upload failed because file was not open");
		}

		String aPageHtml;
		aPageHtml += "<html><body>";
		aPageHtml += "<p>upload finished: " + String(aUploadStatus.filename) + "</p>";
		aPageHtml += "<a href='/'>back to file manager</a>";
		aPageHtml += "</body></html>";

		aWebServer.send(200, "text/html", aPageHtml);
	}
}

// handle a delete button click from the web ui
void handleDelete()
{
	if (!aWebServer.hasArg("name"))
	{
		aWebServer.send(400, "text/plain", "missing name parameter");
		return;
	}

	String aShortName = aWebServer.arg("name");
	String aFullPath = "/" + aShortName;

	Serial.print("delete request for ");
	Serial.println(aFullPath);

	if (SD.exists(aFullPath))
	{
		SD.remove(aFullPath);
		Serial.println("file deleted");
	}
	else
	{
		Serial.println("file not found");
	}

	aWebServer.sendHeader("Location", "/", true);
	aWebServer.send(303);
}

// send a file back to the browser when user clicks download
void handleDownload()
{
	if (!aWebServer.hasArg("name"))
	{
		aWebServer.send(400, "text/plain", "missing name parameter");
		return;
	}

	String aShortName = aWebServer.arg("name");
	String aFullPath = "/" + aShortName;

	Serial.print("download request for ");
	Serial.println(aFullPath);

	if (!SD.exists(aFullPath))
	{
		aWebServer.send(404, "text/plain", "file not found");
		return;
	}

	File aFileToSend = SD.open(aFullPath, FILE_READ);
	if (!aFileToSend)
	{
		aWebServer.send(500, "text/plain", "failed to open file");
		return;
	}

	aWebServer.streamFile(aFileToSend, "application/octet-stream");
	aFileToSend.close();
}

void setup()
{
	Serial.begin(115200);
	delay(1000);

	Serial.println("starting sd file manager");

	// get sd card ready using the same pins as the main project
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

	// bring up a small wifi access point so phone or laptop can connect
	WiFi.mode(WIFI_AP);
	WiFi.softAP(aWifiSsid, aWifiPassword);

	Serial.println("softap started");
	Serial.print("ssid: ");
	Serial.println(aWifiSsid);
	Serial.print("password: ");
	Serial.println(aWifiPassword);
	Serial.println("open http://192.168.4.1/ in your browser");

	// normal style handlers instead of lambdas so it reads simpler
	aWebServer.on("/", HTTP_GET, handleRoot);
	aWebServer.on("/upload", HTTP_POST, handleUpload);
	aWebServer.on("/delete", HTTP_POST, handleDelete);
	aWebServer.on("/download", HTTP_GET, handleDownload);

	aWebServer.begin();
}

void loop()
{
	aWebServer.handleClient();
}
