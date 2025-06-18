#include <WiFi.h>
#include <HTTPClient.h>
#include <FirebaseESP32.h>
#include <WiFiManager.h>
#include <time.h>

#define API_KEY        "AIzaSyDCtvZMg-Ma0BCjG3eEpEEgs19EJVBTx90"
#define DATABASE_URL   "https://floodwatch-cabc5-default-rtdb.asia-southeast1.firebasedatabase.app/"

String namaSungai = "sungai_bedadung";

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

const char* backendHistoriURL = "https://services-flood.vercel.app/api/histori";
const char* backendNotifikasiURL = "https://services-flood.vercel.app/api/notifikasi";

#define LORA_TX 17
#define LORA_RX 16
#define M0      25
#define M1      26

#define LED_PIN    15
#define BUTTON_PIN 4

unsigned long lastDataTime = 0;
unsigned long lastBackendTime = 0;
unsigned long lastThresholdCheck = 0;
const unsigned long dataInterval = 5000;
const unsigned long backendInterval = 600000;
const unsigned long thresholdInterval = 10000;

float lastKetinggian = 0;
float nilaiAmbangBatas = 150.0;
float tinggiSensorDariDasar = 200.0;

void setup() {
  Serial.begin(115200);

  pinMode(M0, OUTPUT);
  pinMode(M1, OUTPUT);
  digitalWrite(M0, LOW);
  digitalWrite(M1, LOW);
  Serial2.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  if (digitalRead(BUTTON_PIN) == LOW) {
    WiFiManager wm;
    wm.resetSettings();
    delay(1000);
    ESP.restart();
  }

  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setTimeout(180);
  if (!wm.autoConnect("FloodWatch-Receiver")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }

  Serial.println("Connected to WiFi: " + WiFi.SSID());
  digitalWrite(LED_PIN, HIGH);

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = "sungaibedadung@gmail.com";
  auth.user.password = "123456";
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("📡 Receiver LoRa Flood Detection Ready");
}

void loop() {
  unsigned long currentMillis = millis();

  if (Serial2.available()) {
    String incoming = Serial2.readStringUntil('\n');
    incoming.trim();

    Serial.print("📩 Pesan masuk: ");
    Serial.println(incoming);

    if (incoming.startsWith("DATA:")) {
      processSensorData(incoming);
      lastDataTime = currentMillis;
    } else if (incoming.startsWith("ACK:")) {
      Serial.println("✅ Sender menerima update kalibrasi");
    }
  }

  if (currentMillis - lastThresholdCheck >= thresholdInterval) {
    lastThresholdCheck = currentMillis;
    checkFirebaseThreshold();
  }

  if (currentMillis - lastBackendTime >= backendInterval) {
    lastBackendTime = currentMillis;
    sendToBackend(lastKetinggian);
  }

  updateLEDStatus();
  checkResetButton();
}

void processSensorData(String data) {
  int kStart = data.indexOf("KETINGGIAN=");
  int hStart = data.indexOf("HUJAN=");
  int endData = data.indexOf(';', hStart);

  if (kStart != -1 && hStart != -1 && endData != -1) {
    float ketinggian = data.substring(kStart + 11, hStart - 1).toFloat();
    int hujan = data.substring(hStart + 6, endData).toInt();

    lastKetinggian = ketinggian;

    Serial.println("📊 Data Sensor:");
    Serial.println("   Ketinggian: " + String(ketinggian) + " cm");
    Serial.println("   Hujan: " + String(hujan) + "%");

    updateFirebaseData(ketinggian, hujan);
  }
}

void checkFirebaseThreshold() {
  String pathTinggi = "/" + namaSungai + "/kalibrasi/tinggiSensor";
  String pathAmbang = "/" + namaSungai + "/threshold/nilai";
  bool needUpdate = false;

  if (Firebase.getFloat(fbdo, pathTinggi) && fbdo.httpCode() == 200) {
    float newTinggi = fbdo.floatData();
    if (newTinggi != tinggiSensorDariDasar) {
      tinggiSensorDariDasar = newTinggi;
      Serial.println("🔄 Tinggi sensor diperbarui: " + String(newTinggi));
      needUpdate = true;
    }
  }

  if (Firebase.getFloat(fbdo, pathAmbang) && fbdo.httpCode() == 200) {
    float newAmbang = fbdo.floatData();
    if (newAmbang != nilaiAmbangBatas) {
      nilaiAmbangBatas = newAmbang;
      Serial.println("🔄 Ambang batas diperbarui: " + String(newAmbang));
      needUpdate = true;
    }
  }

  if (needUpdate) {
    String cmd = "SETTING:TINGGI=" + String(tinggiSensorDariDasar, 1) + 
                 ",BATAS=" + String(nilaiAmbangBatas, 1) + ";";
    Serial2.println(cmd);
    Serial.println("📤 Mengirim kalibrasi: " + cmd);
  }
}

void updateFirebaseData(float ketinggian, int hujan) {
  String path = "/" + namaSungai;

  Firebase.setFloat(fbdo, path + "/ketinggian", ketinggian);
  Firebase.setInt(fbdo, path + "/hujan", hujan);
}

void sendToBackend(float ketinggian) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ WiFi tidak terhubung");
    return;
  }

  // Hitung level berdasarkan ambang batas
  String level = "AMAN";
  if (ketinggian >= nilaiAmbangBatas) {
    level = "BAHAYA";
  } else if (ketinggian >= nilaiAmbangBatas * 0.7) {
    level = "WASPADA";
  }

  // Ambil timestamp saat ini
  time_t now;
  time(&now);

  // --------- Kirim Data Histori ---------
  HTTPClient http;
  http.begin(backendHistoriURL);
  http.addHeader("Content-Type", "application/json");

  String jsonHistori = "{\"sungai\":\"" + namaSungai +
                       "\",\"ketinggian\":" + String(ketinggian, 1) +
                       ",\"timestamp\":" + String(now) +
                       ",\"level\":\"" + level + "\"}";

  int httpCode = http.POST(jsonHistori);
  Serial.printf(httpCode > 0 ? "✅ Histori terkirim (HTTP %d)\n" : "❌ Gagal kirim histori (HTTP %d)\n", httpCode);
  http.end();

  // --------- Kirim Notifikasi Jika Perlu ---------
  if (level == "WASPADA" || level == "BAHAYA") {
    http.begin(backendNotifikasiURL);
    http.addHeader("Content-Type", "application/json");

    String jsonNotif = "{\"sungai\":\"" + namaSungai +
                       "\",\"ketinggian\":" + String(ketinggian, 1) +
                       ",\"timestamp\":" + String(now) +
                       ",\"level\":\"" + level + "\"}";

    int notifCode = http.POST(jsonNotif);
    Serial.printf(notifCode > 0 ? "📣 Notifikasi terkirim (HTTP %d)\n" : "⚠ Gagal kirim notifikasi (HTTP %d)\n", notifCode);
    http.end();
  }
}

void updateLEDStatus() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    unsigned long now = millis();
    if (now - lastBlink >= 500) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }
  }
}

void checkResetButton() {
  static unsigned long lastPress = 0;

  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastPress > 200) {
    lastPress = millis();
    Serial.println("🔄 Tombol reset WiFi ditekan");
    digitalWrite(LED_PIN, LOW);

    WiFiManager wm;
    wm.resetSettings();
    delay(1000);
    ESP.restart();
  }
}