#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Preferences.h>

const char* WIFI_SSID     = "Xiaomi 14T";
const char* WIFI_PASSWORD = "bejirkumam";

#define PIN_PIR     27
#define PIN_SERVO   13
#define PIN_BUZZER  26
#define PIN_LDR     34

const int BUZZER_FREQ_HZ  = 2000;
int LDR_THRESHOLD         = 1000;
const int LDR_HYSTERESIS  = 150;

String CAM_IP = "10.163.0.149";

WebServer server(80);
Servo myServo;
Preferences prefs;

bool modeSiang = true;
bool autoMode  = true;
int  ldrValue  = 0;

unsigned long lastTrigger = 0;
const unsigned long COOLDOWN_MS = 8000;
unsigned long lastLdrCheck = 0;
const unsigned long LDR_CHECK_INTERVAL_MS = 2000;

#define MAX_RIWAYAT 10
String riwayatLog[MAX_RIWAYAT];
int riwayatCount = 0;
unsigned long totalDeteksi = 0;

const int SERVO_CENTER = 90;
const int SERVO_LEFT   = 30;
const int SERVO_RIGHT  = 150;

void bacaLDRdanUpdateMode() {
  ldrValue = analogRead(PIN_LDR);
  if (!autoMode) return;
  if (ldrValue > (LDR_THRESHOLD + LDR_HYSTERESIS) && !modeSiang) {
    modeSiang = true;
    Serial.printf("[LDR] Terang -> mode SIANG (nilai=%d)\n", ldrValue);
  } else if (ldrValue < (LDR_THRESHOLD - LDR_HYSTERESIS) && modeSiang) {
    modeSiang = false;
    Serial.printf("[LDR] Gelap -> mode MALAM (nilai=%d)\n", ldrValue);
  }
}

void gerakanPengusir() {
  for (int i = 0; i < 3; i++) {
    myServo.write(SERVO_LEFT);
    delay(400);
    myServo.write(SERVO_RIGHT);
    delay(400);
  }
  myServo.write(SERVO_CENTER);
}

void catatRiwayat(String pesan) {
  for (int i = MAX_RIWAYAT - 1; i > 0; i--) {
    riwayatLog[i] = riwayatLog[i - 1];
  }
  riwayatLog[0] = pesan;
  if (riwayatCount < MAX_RIWAYAT) riwayatCount++;
  totalDeteksi++;
}

void triggerCamCapture() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClient client;
  if (client.connect(CAM_IP.c_str(), 80)) {
    String mode = modeSiang ? "SIANG" : "MALAM";
    client.println("GET /send-photo?mode=" + mode + " HTTP/1.1");
    client.println("Host: " + CAM_IP);
    client.println("Connection: close");
    client.println();
    Serial.println("[CAM] Request foto Telegram dikirim ke ESP32-CAM");
    client.stop();
  } else {
    Serial.println("[CAM] Gagal konek ke ESP32-CAM");
  }
}

void handleMotionDetected() {
  Serial.println("[PIR] Gerakan terdeteksi!");
  String aksi;
  if (modeSiang) {
    for (int i = 0; i < 2; i++) {
      tone(PIN_BUZZER, BUZZER_FREQ_HZ);
      delay(300);
      noTone(PIN_BUZZER);
      delay(200);
    }
    gerakanPengusir();
    aksi = "Buzzer 2x + Servo aktif (SIANG)";
  } else {
    gerakanPengusir();
    aksi = "Servo aktif (MALAM)";
  }
  catatRiwayat(aksi + " - t+" + String(millis() / 1000) + "s");
  Serial.println("[LOG] " + aksi);
  triggerCamCapture();
}

String htmlPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Pengusir Hama</title>";
  html += "<style>body{font-family:Arial;background:#f2f2f2;margin:0;padding:20px;}";
  html += ".card{background:#fff;border-radius:10px;padding:20px;max-width:420px;margin:0 auto 16px;box-shadow:0 2px 6px rgba(0,0,0,0.1);}";
  html += "h2{margin-top:0;color:#2c3e50;}";
  html += "button{padding:12px 18px;margin:6px;border:none;border-radius:8px;font-size:15px;cursor:pointer;}";
  html += ".btn-day{background:#f39c12;color:#fff;}.btn-night{background:#2c3e50;color:#fff;}";
  html += ".btn-manual{background:#3498db;color:#fff;}.btn-auto{background:#27ae60;color:#fff;}";
  html += ".active{outline:3px solid #27ae60;}p{color:#555;}li{color:#555;font-size:14px;}</style></head><body>";

  html += "<div class='card'><h2>Sistem Pengusir Hama</h2>";
  html += "<p>Mode aktif: <b>" + String(modeSiang ? "SIANG (Servo + Buzzer)" : "MALAM (Servo saja)") + "</b></p>";
  html += "<p>Sumber mode: <b>" + String(autoMode ? "OTOMATIS (LDR)" : "MANUAL") + "</b></p>";
  html += "<p>Nilai LDR: " + String(ldrValue) + " / 4095</p>";
  html += "<form action='/setauto' method='GET' style='display:inline'>";
  html += "<button class='btn-auto" + String(autoMode ? " active" : "") + "' name='auto' value='1'>Otomatis (LDR)</button></form>";
  html += "<form action='/setauto' method='GET' style='display:inline'>";
  html += "<button class='btn-manual" + String(!autoMode ? " active" : "") + "' name='auto' value='0'>Manual</button></form>";
  html += "<p style='margin-top:12px'>Paksa mode:</p><form action='/setmode' method='GET'>";
  html += "<button class='btn-day" + String(modeSiang && !autoMode ? " active" : "") + "' name='mode' value='siang'>Siang</button>";
  html += "<button class='btn-night" + String(!modeSiang && !autoMode ? " active" : "") + "' name='mode' value='malam'>Malam</button>";
  html += "</form></div>";

  html += "<div class='card'><h2>Kontrol Servo Manual</h2><form action='/manual' method='GET'>";
  html += "<button class='btn-manual' name='dir' value='left'>&#8592; Kiri</button>";
  html += "<button class='btn-manual' name='dir' value='center'>Tengah</button>";
  html += "<button class='btn-manual' name='dir' value='right'>Kanan &#8594;</button></form></div>";

  html += "<div class='card'><h2>Riwayat Deteksi</h2><ul>";
  if (riwayatCount == 0) html += "<li>Belum ada gerakan terdeteksi.</li>";
  else for (int i = 0; i < riwayatCount; i++) html += "<li>" + riwayatLog[i] + "</li>";
  html += "</ul></div>";

  html += "<div class='card'><h2>Status</h2>";
  html += "<p>IP ESP32Dev: " + WiFi.localIP().toString() + "</p>";
  html += "<p>IP ESP32-CAM: " + CAM_IP + "</p>";
  html += "<p>Total deteksi: " + String(totalDeteksi) + "</p></div>";
  html += "</body></html>";
  return html;
}

void handleRoot()    { server.send(200, "text/html", htmlPage()); }

void handleSetMode() {
  if (server.hasArg("mode")) {
    modeSiang = (server.arg("mode") == "siang");
    autoMode  = false;
    prefs.putBool("modeSiang", modeSiang);
    prefs.putBool("autoMode", autoMode);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetAuto() {
  if (server.hasArg("auto")) {
    autoMode = (server.arg("auto") == "1");
    prefs.putBool("autoMode", autoMode);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleManual() {
  if (server.hasArg("dir")) {
    String d = server.arg("dir");
    if (d == "left")       myServo.write(SERVO_LEFT);
    else if (d == "right") myServo.write(SERVO_RIGHT);
    else                   myServo.write(SERVO_CENTER);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStatusJson() {
  String json = "{";
  json += "\"mode\":\"" + String(modeSiang ? "siang" : "malam") + "\",";
  json += "\"autoMode\":" + String(autoMode ? "true" : "false") + ",";
  json += "\"ldrValue\":" + String(ldrValue) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"totalDeteksi\":" + String(totalDeteksi) + ",";
  json += "\"riwayat\":[";
  for (int i = 0; i < riwayatCount; i++) {
    json += "\"" + riwayatLog[i] + "\"";
    if (i < riwayatCount - 1) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  noTone(PIN_BUZZER);
  pinMode(PIN_LDR, INPUT);

  ESP32PWM::allocateTimer(0);
  myServo.setPeriodHertz(50);
  myServo.attach(PIN_SERVO, 500, 2400);
  myServo.write(SERVO_CENTER);

  prefs.begin("hama", false);
  modeSiang = prefs.getBool("modeSiang", true);
  autoMode  = prefs.getBool("autoMode", true);

  bacaLDRdanUpdateMode();
  Serial.printf("[LDR] Nilai awal: %d (threshold=%d)\n", ldrValue, LDR_THRESHOLD);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menghubungkan WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Terhubung! IP ESP32Dev: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/setmode", handleSetMode);
  server.on("/setauto", handleSetAuto);
  server.on("/manual", handleManual);
  server.on("/status", handleStatusJson);
  server.begin();
  Serial.println("Web server & REST API aktif.");
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastLdrCheck > LDR_CHECK_INTERVAL_MS) {
    lastLdrCheck = now;
    bacaLDRdanUpdateMode();
  }

  int pirState = digitalRead(PIN_PIR);
  if (pirState == HIGH && (now - lastTrigger > COOLDOWN_MS)) {
    lastTrigger = now;
    handleMotionDetected();
  }
}