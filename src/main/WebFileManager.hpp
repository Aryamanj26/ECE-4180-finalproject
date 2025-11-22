/*
 * WiFi File Manager
 * 
 * Provides a web-based interface for managing files on the SD card.
 * Creates a WiFi access point and hosts a simple web server that allows
 * uploading, downloading, and deleting WAV files through a browser.
 * Useful for updating the music library without removing the SD card.
 */

#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>

#include <Logger.hpp>

extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/semphr.h"
}

namespace WebFileManager {

  // ------- internal shared state --------

  inline WebServer& server() {
    static WebServer s(80);
    return s;
  }

  inline SemaphoreHandle_t& sdMutexRef() {
    static SemaphoreHandle_t m = nullptr;
    return m;
  }

  inline File& uploadFileRef() {
    static File f;
    return f;
  }

  inline const char*& ssidRef() {
    static const char* s = "ESP32-Music";
    return s;
  }

  inline const char*& passwordRef() {
    static const char* p = "12345678";
    return p;
  }

  // -------- helpers --------

  /*
   * Converts a file size in bytes to a human-readable string
   * Automatically selects appropriate units (B, KB, MB, GB).
   */
  inline String humanSize(uint64_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024) return String(kb, 1) + " KB";
    double mb = kb / 1024.0;
    if (mb < 1024) return String(mb, 1) + " MB";
    double gb = mb / 1024.0;
    return String(gb, 1) + " GB";
  }

  /*
   * Generates an HTML table listing all files on the SD card
   * Includes file names, sizes, and action buttons (download/delete) for each file.
   */
  inline String makeFileTable() {
    String html;

    SemaphoreHandle_t m = sdMutexRef();
    if (!m || xSemaphoreTake(m, pdMS_TO_TICKS(200)) != pdTRUE) {
      return "<p>SD busy or not available.</p>";
    }

    File root = SD.open("/");
    if (!root) {
      xSemaphoreGive(m);
      return "<p>Failed to open SD root.</p>";
    }

    html += "<table border='1' cellpadding='4' cellspacing='0'>"
            "<tr><th>Name</th><th>Size</th><th>Actions</th></tr>";

    while (true) {
      File f = root.openNextFile();
      if (!f) break;
      String name = String(f.name());
      uint64_t size = f.size();
      f.close();

      String displayName = name;
      if (displayName.startsWith("/")) displayName.remove(0, 1);

      html += "<tr>";
      html += "<td>" + displayName + "</td>";
      html += "<td>" + humanSize(size) + "</td>";
      html += "<td>";

      // Download
      html += "<form style='display:inline' method='GET' action='/download'>";
      html += "<input type='hidden' name='name' value='" + displayName + "'>";
      html += "<input type='submit' value='Download'>";
      html += "</form>";

      html += "&nbsp;";

      // Delete
      html += "<form style='display:inline' method='POST' action='/delete' ";
      html += "onsubmit='return confirm(\"Delete " + displayName + " ?\");'>";
      html += "<input type='hidden' name='name' value='" + displayName + "'>";
      html += "<input type='submit' value='Delete'>";
      html += "</form>";

      html += "</td>";
      html += "</tr>";
    }

    html += "</table>";

    xSemaphoreGive(m);
    return html;
  }

  // -------- HTTP handlers --------

  /*
   * Serves the main file manager page
   * Displays the upload form and lists all files currently on the SD card.
   */
  inline void handleRoot() {
    String page;
    page += "<html><head><title>ESP32 SD File Manager</title></head><body>";
    page += "<h2>ESP32 SD File Manager</h2>";

    // Upload form
    page += "<h3>Upload file</h3>";
    page += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
    page += "File: <input type='file' name='upload'><br><br>";
    page += "<input type='submit' value='Upload'>";
    page += "</form>";

    // File list
    page += "<h3>Files on SD</h3>";
    page += makeFileTable();

    page += "<br><hr><small>Connect to WiFi \"";
    page += ssidRef();
    page += "\" and open http://192.168.4.1/</small>";
    page += "</body></html>";

    server().send(200, "text/html", page);
  }

  /*
   * Handles file uploads from the web interface
   * Receives file data in chunks and writes it to the SD card.
   */
  inline void handleUpload() {
    HTTPUpload& upload = server().upload();
    File& uploadFile = uploadFileRef();

    SemaphoreHandle_t m = sdMutexRef();

    if (upload.status == UPLOAD_FILE_START) {
      String filename = "/" + upload.filename;
      Serial.print("Upload start: ");
      Serial.println(filename);

      if (m && xSemaphoreTake(m, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (SD.exists(filename)) SD.remove(filename);
        uploadFile = SD.open(filename, FILE_WRITE);
        xSemaphoreGive(m);
      }
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
      if (uploadFile) {
        uploadFile.write(upload.buf, upload.currentSize);
      }
    }
    else if (upload.status == UPLOAD_FILE_END) {
      if (uploadFile) {
        uploadFile.close();
        Serial.print("Upload end, size = ");
        Serial.println(upload.totalSize);
      } else {
        Logger::log(Logger::Level::Error, "Upload failed: file not open");
        LOGGER_DEBUG(Serial.println("Upload failed: file not open"));
      }

      String page;
      page += "<html><body>";
      page += "<p>Upload finished: " + String(upload.filename) + "</p>";
      page += "<a href='/'>Back to file manager</a>";
      page += "</body></html>";

      server().send(200, "text/html", page);
    }
  }

  /*
   * Handles file deletion requests from the web interface
   * Removes the specified file from the SD card and redirects back to the main page.
   */
  inline void handleDelete() {
    if (!server().hasArg("name")) {
      server().send(400, "text/plain", "Missing 'name' parameter");
      return;
    }

    String shortName = server().arg("name");
    String fullPath = "/" + shortName;

    Serial.print("Delete request: ");
    Serial.println(fullPath);

    SemaphoreHandle_t m = sdMutexRef();
    if (m && xSemaphoreTake(m, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (SD.exists(fullPath)) {
        SD.remove(fullPath);
        Serial.println("File deleted.");
      } else {
        Logger::logf(Logger::Level::Warn,
                     "Delete request failed: %s not found",
                     fullPath.c_str());
        LOGGER_DEBUG(Serial.println("File not found."));
      }
      xSemaphoreGive(m);
    }

    server().sendHeader("Location", "/", true);
    server().send(303);
  }

  /*
   * Handles file download requests from the web interface
   * Streams the requested file from SD card to the client browser.
   */
  inline void handleDownload() {
    if (!server().hasArg("name")) {
      server().send(400, "text/plain", "Missing 'name' parameter");
      return;
    }

    String shortName = server().arg("name");
    String fullPath = "/" + shortName;

    Serial.print("Download request: ");
    Serial.println(fullPath);

    SemaphoreHandle_t m = sdMutexRef();
    if (!m || xSemaphoreTake(m, pdMS_TO_TICKS(200)) != pdTRUE) {
      server().send(503, "text/plain", "SD busy");
      return;
    }

    if (!SD.exists(fullPath)) {
      xSemaphoreGive(m);
      server().send(404, "text/plain", "File not found");
      return;
    }

    File f = SD.open(fullPath, FILE_READ);
    if (!f) {
      xSemaphoreGive(m);
      server().send(500, "text/plain", "Failed to open file");
      return;
    }

    server().streamFile(f, "application/octet-stream");
    f.close();
    xSemaphoreGive(m);
  }

  // -------- public API for your main code --------

  /*
   * Initializes and starts the WiFi file manager web server
   * Creates a WiFi access point and sets up HTTP request handlers.
   * Connect to the specified SSID and navigate to http://192.168.4.1/
   */
  inline void begin(SemaphoreHandle_t sdMutex,
                    const char* ssid = "ESP32-Music",
                    const char* password = "12345678")
  {
    sdMutexRef() = sdMutex;
    ssidRef() = ssid;
    passwordRef() = password;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssidRef(), passwordRef());

    Serial.println("SoftAP started");
    Serial.print("SSID: ");
    Serial.println(ssidRef());
    Serial.print("Password: ");
    Serial.println(passwordRef());
    Serial.println("Open http://192.168.4.1/ in your browser.");

    server().on("/", HTTP_GET, handleRoot);

    server().on(
      "/upload",
      HTTP_POST,
      []() { /* nothing, handled in handleUpload */ },
      handleUpload
    );

    server().on("/delete", HTTP_POST, handleDelete);
    server().on("/download", HTTP_GET, handleDownload);

    server().begin();
  }

  /*
   * Processes incoming web requests (call regularly from main loop)
   * Checks for and handles any pending HTTP requests from clients.
   */
  inline void loopOnce() {
    server().handleClient();
  }

  /*
   * Shuts down the web server and disables WiFi
   * Call this when the file manager is no longer needed.
   */
  inline void stop() {
    server().stop();
    WiFi.mode(WIFI_OFF);
  }

} // namespace WebFileManager
