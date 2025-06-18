#include <HardwareSerial.h>

// -------------------- Pin Konfigurasi --------------------
#define LORA_TX         17
#define LORA_RX         16
#define M0              25
#define M1              26
#define TRIG_PIN        5
#define ECHO_PIN        18
#define RAIN_SENSOR_PIN 34
#define BUZZER_PIN      27

// -------------------- Variabel Sensor --------------------
float ketinggianAir;
int rainPercent;

// -------------------- Kalibrasi --------------------
float tinggiSensorDariDasar = 200.0;
float batasAmanKetinggian = 150.0;

// -------------------- Timing & Filter --------------------
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 2000;

#define FILTER_SAMPLES 5
float distanceSamples[FILTER_SAMPLES];
int currentSample = 0;

// -------------------- Buzzer --------------------
unsigned long lastBuzzerTime = 0;
const unsigned long buzzerInterval = 200;
bool buzzerState = false;

void setup() {
  Serial.begin(115200);

  pinMode(M0, OUTPUT);
  pinMode(M1, OUTPUT);
  digitalWrite(M0, LOW);
  digitalWrite(M1, LOW);
  Serial2.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
  delay(500);

  Serial.println("🚀 Sender LoRa Flood Detection Ready");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RAIN_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  for (int i = 0; i < FILTER_SAMPLES; i++) {
    distanceSamples[i] = tinggiSensorDariDasar;
  }
}

void loop() {
  unsigned long currentMillis = millis();

  processIncomingMessages();

  float distance = readUltrasonic();
  ketinggianAir = tinggiSensorDariDasar - distance;
  ketinggianAir = constrain(ketinggianAir, 0, tinggiSensorDariDasar);

  rainPercent = readRainSensor();,

  controlBuzzer(currentMillis);

  if (currentMillis - lastSendTime >= sendInterval) {
    lastSendTime = currentMillis;
    sendSensorData();
  }

  delay(50);
}

void processIncomingMessages() {
  if (Serial2.available()) {
    String incoming = Serial2.readStringUntil('\n');
    incoming.trim();

    Serial.print("📩 Pesan masuk: ");
    Serial.println(incoming);

    if (incoming.startsWith("SETTING:")) {
      int tStart = incoming.indexOf("TINGGI=");
      int bStart = incoming.indexOf("BATAS=");
      int endCmd = incoming.indexOf(';');

      if (tStart != -1 && bStart != -1 && endCmd != -1) {
        float newTinggi = incoming.substring(tStart + 7, bStart - 1).toFloat();
        float newBatas = incoming.substring(bStart + 6, endCmd).toFloat();

        if (newTinggi > 0 && newBatas > 0) {
          tinggiSensorDariDasar = newTinggi;
          batasAmanKetinggian = newBatas;

          Serial.println("🔧 Kalibrasi diperbarui:");
          Serial.println("   Tinggi Sensor: " + String(tinggiSensorDariDasar) + " cm");
          Serial.println("   Batas Aman: " + String(batasAmanKetinggian) + " cm");

          Serial2.println("ACK:SETTING_OK;");
        }
      }
    }
  }
}

float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    Serial.println("⚠ Error: Sensor ultrasonik tidak merespon");
    return tinggiSensorDariDasar;
  }

  float distance = duration * 0.034 / 2;
  distanceSamples[currentSample] = distance;
  currentSample = (currentSample + 1) % FILTER_SAMPLES;

  float avgDistance = 0;
  for (int i = 0; i < FILTER_SAMPLES; i++) {
    avgDistance += distanceSamples[i];
  }
  return avgDistance / FILTER_SAMPLES;
}

int readRainSensor() {
  int rawValue = analogRead(RAIN_SENSOR_PIN);
  int percent = map(rawValue, 4095, 0, 0, 100);
  return constrain(percent, 0, 100);
}

void controlBuzzer(unsigned long currentTime) {
  if (ketinggianAir >= batasAmanKetinggian) {
    if (currentTime - lastBuzzerTime >= buzzerInterval) {
      lastBuzzerTime = currentTime;
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = false;
  }
}

void sendSensorData() {
  String dataStr = "DATA:KETINGGIAN=" + String(ketinggianAir, 1) + 
                   ",HUJAN=" + String(rainPercent) + ";";

  Serial2.println(dataStr);
  Serial.println("📤 Mengirim data: " + dataStr);
}