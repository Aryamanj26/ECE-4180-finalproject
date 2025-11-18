#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>

const char* ssid     = "ESP32-Music";
const char* password = "12345678";

#define SD_CS 9  // your working CS pin

WebServer server(80);
File uploadFile;

String humanSize(uint64_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  double kb = bytes / 1024.0;
  if (kb < 1024) return String(kb, 1) + " KB";
  double mb = kb / 1024.0;
  if (mb < 1024) return String(mb, 1) + " MB";
  double gb = mb / 1024.0;
  return String(gb, 1) + " GB";
}

String makeFileTable() {
  String html;
  File root = SD.open("/");
  if (!root) {
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

    // strip leading '/' for display / form value
    String displayName = name;
    if (displayName.startsWith("/")) displayName.remove(0, 1);

    html += "<tr>";
    html += "<td>" + displayName + "</td>";
    html += "<td>" + humanSize(size) + "</td>";
    html += "<td>";

    // Download button
    html += "<form style='display:inline' method='GET' action='/download'>";
    html += "<input type='hidden' name='name' value='" + displayName + "'>";
    html += "<input type='submit' value='Download'>";
    html += "</form>";

    html += "&nbsp;";

    // Delete button
    html += "<form style='display:inline' method='POST' action='/delete' onsubmit='return confirm(\"Delete "
            + displayName + " ?\");'>";
    html += "<input type='hidden' name='name' value='" + displayName + "'>";
    html += "<input type='submit' value='Delete'>";
    html += "</form>";

    html += "</td>";
    html += "</tr>";
  }

  html += "</table>";
  return html;
}

void handleRoot() {
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

  page += "<br><hr><small>Connect to WiFi \"" + String(ssid) +
          "\" and open http://192.168.4.1/</small>";
  page += "</body></html>";

  server.send(200, "text/html", page);
}

// upload handler (same endpoint used for data chunks)
void handleUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    Serial.print("Upload start: ");
    Serial.println(filename);

    if (SD.exists(filename)) SD.remove(filename);
    uploadFile = SD.open(filename, FILE_WRITE);
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
      Serial.println("Upload failed: file not open");
    }

    // After upload, show simple page with link back
    String page;
    page += "<html><body>";
    page += "<p>Upload finished: " + String(upload.filename) + "</p>";
    page += "<a href='/'>Back to file manager</a>";
    page += "</body></html>";

    server.send(200, "text/html", page);
  }
}

// delete handler
void handleDelete() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing 'name' parameter");
    return;
  }

  String shortName = server.arg("name");
  String fullPath = "/" + shortName;

  Serial.print("Delete request: ");
  Serial.println(fullPath);

  if (SD.exists(fullPath)) {
    SD.remove(fullPath);
    Serial.println("File deleted.");
  } else {
    Serial.println("File not found.");
  }

  // redirect back to root
  server.sendHeader("Location", "/", true);
  server.send(303); // 303 See Other
}

// download handler
void handleDownload() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing 'name' parameter");
    return;
  }

  String shortName = server.arg("name");
  String fullPath = "/" + shortName;

  Serial.print("Download request: ");
  Serial.println(fullPath);

  if (!SD.exists(fullPath)) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  File f = SD.open(fullPath, FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Failed to open file");
    return;
  }

  // you can set audio/wav if mostly WAVs; octet-stream is generic
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting SD File Manager...");

  // use the known-good SPI config
  SPI.begin(18, 19, 23, SD_CS);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD init failed");
    while (true) delay(1000);
  }
  Serial.println("SD init OK");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  Serial.println("SoftAP started");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  Serial.println("Open http://192.168.4.1/ in your browser.");

  // routes
  server.on("/", HTTP_GET, handleRoot);

  // upload: need handler for the POST itself and for the data
  server.on(
    "/upload",
    HTTP_POST,
    []() { /* nothing here, upload handled in handleUpload */ },
    handleUpload
  );

  // delete via POST
  server.on("/delete", HTTP_POST, handleDelete);

  // download via GET
  server.on("/download", HTTP_GET, handleDownload);

  server.begin();
}

void loop() {
  server.handleClient();
}
