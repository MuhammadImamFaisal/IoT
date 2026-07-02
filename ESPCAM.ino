#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "esp_camera.h"

// =======================
// WIFI CONFIG
// =======================
const char* ssid = "Xiaomi 14T";
const char* password = "bejirkumam";




// =======================
// TELEGRAM CONFIG
// =======================
#define BOT_TOKEN "8338325449:AAHAemkkgDj5acbj8ycv7YUQrLaay5DB24A"
#define CHAT_ID   "1385623855"

// =======================
// ESP32-CAM AI THINKER PIN
// =======================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_LED_PIN      4

// =======================
// OBJECT
// =======================
WebServer server(80);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// =======================
// TELEGRAM PHOTO BUFFER
// =======================
camera_fb_t *telegram_fb = nullptr;
size_t telegram_index = 0;

// =======================
// SETUP
// =======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  Serial.println();
  Serial.println("=====================================");
  Serial.println("ESP32-CAM TELEGRAM PHOTO SYSTEM");
  Serial.println("Mode: QVGA Stabil");
  Serial.println("=====================================");

  setupCamera();
  connectWiFi();

  secured_client.setInsecure();

  setupRoutes();
  server.begin();

  Serial.println();
  Serial.println("ESP32-CAM Web Server Ready.");
  Serial.print("ESP32-CAM IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/capture");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/send-photo?mode=SIANG");
}

// =======================
// LOOP
// =======================
void loop() {
  server.handleClient();
}

// =======================
// WIFI CONNECT
// =======================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 60) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected.");
    Serial.print("ESP32-CAM IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi gagal. Cek SSID/password/hotspot 2.4GHz.");
  }
}

// =======================
// CAMERA INIT
// =======================
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 15;
  config.fb_count = 1;

  if (psramFound()) {
    Serial.println("PSRAM ditemukan.");
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("PSRAM tidak ditemukan.");
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed. Error: 0x%x\n", err);
    return;
  }

  Serial.println("Camera init success.");

  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_framesize(s, FRAMESIZE_QVGA);
    s->set_quality(s, 15);
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
  }

  for (int i = 0; i < 3; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
      Serial.println("Frame awal dibuang.");
    } else {
      Serial.println("Frame awal gagal.");
    }
    delay(300);
  }
}

// =======================
// ROUTES
// =======================
void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/capture", HTTP_GET, handleCapture);

  server.on("/send-photo", HTTP_GET, []() {
    String mode = "UNKNOWN";
    if (server.hasArg("mode")) {
      mode = server.arg("mode");
      mode.toUpperCase();
    }
    bool result = sendPhotoToTelegram(mode);
    if (result) {
      server.send(200, "text/plain", "Foto berhasil dikirim ke Telegram");
    } else {
      server.send(500, "text/plain", "Gagal mengirim foto ke Telegram");
    }
  });

  server.on("/flash/on", HTTP_GET, []() {
    digitalWrite(FLASH_LED_PIN, HIGH);
    server.send(200, "text/plain", "Flash ON");
  });

  server.on("/flash/off", HTTP_GET, []() {
    digitalWrite(FLASH_LED_PIN, LOW);
    server.send(200, "text/plain", "Flash OFF");
  });
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP32-CAM</title></head><body>";
  html += "<h2>ESP32-CAM System</h2>";
  html += "<ul>";
  html += "<li><a href='/capture'>/capture</a> - lihat foto lokal</li>";
  html += "<li><a href='/send-photo?mode=SIANG'>/send-photo?mode=SIANG</a></li>";
  html += "<li><a href='/send-photo?mode=MALAM'>/send-photo?mode=MALAM</a></li>";
  html += "<li><a href='/flash/on'>/flash/on</a></li>";
  html += "<li><a href='/flash/off'>/flash/off</a></li>";
  html += "</ul></body></html>";
  server.send(200, "text/html", html);
}

void handleCapture() {
  Serial.println("Request /capture");
  camera_fb_t *fb = capturePhotoWithRetry();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  WiFiClient client = server.client();
  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.sendHeader("Connection", "close");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  Serial.println("Capture berhasil.");
}

camera_fb_t* capturePhotoWithRetry() {
  camera_fb_t *fb = nullptr;
  for (int i = 1; i <= 5; i++) {
    fb = esp_camera_fb_get();
    if (fb) {
      Serial.printf("Capture berhasil. Percobaan ke-%d | Size: %d\n", i, fb->len);
      return fb;
    }
    Serial.printf("Capture gagal. Percobaan ke-%d\n", i);
    delay(500);
  }
  return nullptr;
}

bool isMoreDataAvailable() {
  if (telegram_fb == nullptr) return false;
  return telegram_index < telegram_fb->len;
}

uint8_t getNextByte() {
  if (telegram_fb == nullptr) return 0;
  return telegram_fb->buf[telegram_index++];
}

bool sendPhotoToTelegram(String mode) {
  Serial.println("Mengambil foto untuk Telegram...");

  if (mode == "MALAM") {
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(800);
  }

  telegram_fb = capturePhotoWithRetry();
  telegram_index = 0;

  if (mode == "MALAM") {
    digitalWrite(FLASH_LED_PIN, LOW);
  }

  if (!telegram_fb) {
    Serial.println("Camera capture failed.");
    return false;
  }

  String caption = "Pengusir Hama\nGerakan terdeteksi!\nMode: " + mode + "\n";
  if (mode == "SIANG") caption += "Aksi: Servo + Buzzer aktif.";
  else if (mode == "MALAM") caption += "Aksi: Servo aktif, buzzer mati.";
  else caption += "Aksi: Servo aktif.";

  Serial.println("Mengirim caption ke Telegram...");
  bot.sendMessage(CHAT_ID, caption, "");

  Serial.println("Mengirim foto ke Telegram...");
  String response = bot.sendPhotoByBinary(
    CHAT_ID,
    "image/jpeg",
    (int)telegram_fb->len,
    isMoreDataAvailable,
    getNextByte,
    nullptr,
    nullptr
  );

  esp_camera_fb_return(telegram_fb);
  telegram_fb = nullptr;
  telegram_index = 0;

  Serial.print("Telegram response: ");
  Serial.println(response);

  if (response.length() > 0) {
    Serial.println("Foto berhasil dikirim.");
    return true;
  } else {
    Serial.println("Gagal mengirim foto.");
    return false;
  }
}