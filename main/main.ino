#include "HX711.h"
#include <TM1637Display.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ================= WIFI =================
const char* WIFI_SSID = "Hans M";
const char* WIFI_PASSWORD = "123456789";

// ================= API =================
const char* API_URL = "https://backend-bakepal-production.up.railway.app/containers/weight";
const char* THRESHOLD_API_URL_BASE = "https://backend-bakepal-production.up.railway.app/containers/threshold/";
const char* CONTAINER_ID = "1";
const float WEIGHT_CHANGE_THRESHOLD = 0.5;

// Ambang batas berat (diambil dari API saat startup). 0 = belum/tidak ada ambang batas.
float weightThreshold = 0.0;

// ================= HX711 =================
#define DT1_PIN 18
#define SCK1_PIN 5

#define DT2_PIN 19
#define SCK2_PIN 23

HX711 scale1;
HX711 scale2;

// Faktor kalibrasi
float calibration_factor1 = 222.0;
float calibration_factor2 = 222.0;

// ================= TM1637 =================
#define TM_CLK 25
#define TM_DIO 26

TM1637Display display(TM_CLK, TM_DIO);

// Update display setiap 2 detik
unsigned long lastDisplayUpdate = 0;

// Berat terakhir yang dikirim ke server
float lastSentWeight = 0;
bool hasSentWeight = false;

// ================= LED =================
#define LED1_PIN 4    // merah  - weight < 50g
#define LED2_PIN 13   // kuning - 50g <= weight < threshold
#define LED3_PIN 16   // hijau  - weight >= threshold

void connectWiFi() {

  WiFi.mode(WIFI_STA);

  Serial.print("Menghubungkan ke WiFi");

  bool ledState = false;

  while (WiFi.status() != WL_CONNECTED) {

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;

    while (WiFi.status() != WL_CONNECTED && attempts < 20) {

      ledState = !ledState;
      digitalWrite(LED1_PIN, ledState);
      digitalWrite(LED2_PIN, ledState);
      digitalWrite(LED3_PIN, ledState);

      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println();
      Serial.println("WiFi gagal terhubung. Mencoba lagi...");
    }
  }

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  Serial.println();
  Serial.print("WiFi terhubung. IP : ");
  Serial.println(WiFi.localIP());
}

void reconnectWiFi() {

  Serial.println("Koneksi WiFi terputus. Mencoba menghubungkan ulang...");

  WiFi.disconnect();
  WiFi.reconnect();

  int attempts = 0;
  bool ledState = false;

  while (WiFi.status() != WL_CONNECTED && attempts < 20) {

    ledState = !ledState;
    digitalWrite(LED1_PIN, ledState);
    digitalWrite(LED2_PIN, ledState);
    digitalWrite(LED3_PIN, ledState);

    delay(500);
    Serial.print(".");
    attempts++;
  }

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi terhubung kembali. IP : ");
    Serial.println(WiFi.localIP());
  }
  else {
    Serial.println();
    Serial.println("WiFi belum terhubung kembali, akan dicoba lagi.");
  }
}

void fetchThreshold() {

  if (WiFi.status() != WL_CONNECTED) {
    weightThreshold = 0.0;
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();   // skip cert validation

  HTTPClient http;
  String url = String(THRESHOLD_API_URL_BASE) + CONTAINER_ID;
  http.begin(client, url);

  int httpCode = http.GET();

  Serial.print("GET /containers/threshold/");
  Serial.print(CONTAINER_ID);
  Serial.print(" -> ");
  Serial.println(httpCode);

  weightThreshold = 0.0;

  if (httpCode == 200) {
    String body = http.getString();
    int idx = body.indexOf("threshold");

    if (idx != -1) {
      int colonIdx = body.indexOf(':', idx);

      if (colonIdx != -1) {
        weightThreshold = body.substring(colonIdx + 1).toFloat();

        if (weightThreshold < 0)
          weightThreshold = 0.0;
      }
    }
  }

  http.end();

  Serial.print("Ambang batas berat : ");
  Serial.println(weightThreshold);
}

void setup() {

  Serial.begin(115200);

  // ================= LED =================
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  // ================= WIFI =================
  connectWiFi();

  // ================= THRESHOLD =================
  fetchThreshold();

  // ================= TM1637 =================
  display.setBrightness(7);   // 0-7
  display.clear();
  display.showNumberDec(0, false);   // tanpa dot

  // ================= HX711 =================
  scale1.begin(DT1_PIN, SCK1_PIN);
  scale2.begin(DT2_PIN, SCK2_PIN);

  scale1.set_scale(calibration_factor1);
  scale2.set_scale(calibration_factor2);

  Serial.println("Melakukan tare...");

  scale1.tare();
  scale2.tare();

  Serial.println("Tare selesai.");

  delay(1000);
}

void sendWeightUpdate(int weight) {

  if (WiFi.status() != WL_CONNECTED)
    return;

  WiFiClientSecure client;
  client.setInsecure();   // skip cert validation

  HTTPClient http;
  http.begin(client, API_URL);
  http.addHeader("Content-Type", "application/json");

  char payload[96];
  snprintf(payload, sizeof(payload), "{\"container_id\":\"%s\",\"weight\":%d}", CONTAINER_ID, weight);

  int httpCode = http.POST(payload);

  Serial.print("POST /containers/weight -> ");
  Serial.println(httpCode);

  http.end();
}

void handleWeight() {

  const int SAMPLES = 20;

  float weight1 = 0;
  float weight2 = 0;

  long raw1 = 0;
  long raw2 = 0;

  // ================= BACA KEDUA LOAD CELL SECARA INTERLEAVED =================
  // Kedua chip HX711 melakukan konversi secara independen/paralel, jadi
  // membaca keduanya bergantian (bukan berurutan penuh) memangkas total
  // waktu tunggu blocking menjadi ~separuhnya, tanpa mengubah sampel atau
  // rumus rata-rata yang dipakai.
  bool ready1 = scale1.is_ready();
  bool ready2 = scale2.is_ready();

  double sum1 = 0;
  double sum2 = 0;

  for (int i = 0; i < SAMPLES; i++) {
    if (ready1)
      sum1 += scale1.read();

    if (ready2)
      sum2 += scale2.read();
  }

  // ================= LOAD CELL 1 =================
  if (ready1) {
    double avg1 = sum1 / SAMPLES;
    raw1 = (long)avg1;
    weight1 = (avg1 - scale1.get_offset()) / calibration_factor1;

    if (weight1 < 0)
      weight1 = 0;
  }

  // ================= LOAD CELL 2 =================
  if (ready2) {
    double avg2 = sum2 / SAMPLES;
    raw2 = (long)avg2;
    weight2 = (avg2 - scale2.get_offset()) / calibration_factor2;

    if (weight2 < 0)
      weight2 = 0;
  }

  // ================= TOTAL BERAT =================
  float totalWeight = weight1 + weight2;

  // ================= LOGIKA LED =================
  if (totalWeight < 50.0) {

    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, LOW);
    digitalWrite(LED3_PIN, LOW);

  }
  else if (totalWeight < weightThreshold) {

    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, HIGH);
    digitalWrite(LED3_PIN, LOW);

  }
  else {

    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);
    digitalWrite(LED3_PIN, HIGH);

  }

  // ================= SERIAL =================
  Serial.printf("RAW1 : %ld   Beban1 : %.2f g || RAW2 : %ld   Beban2 : %.2f g || Total : %.2f g\n",
                raw1, weight1, raw2, weight2, totalWeight);

  // ================= TM1637 =================
  int tampil = (int)floor(totalWeight);   // pembulatan

  if (tampil < 0)
    tampil = 0;

  if (tampil > 9999)
    tampil = 9999;

  // Display hanya berubah setiap 2 detik
  unsigned long now = millis();
  if (now - lastDisplayUpdate >= 2000) {
    lastDisplayUpdate = now;
    display.showNumberDec(tampil, false);   // false = dot mati
  }

  // ================= KIRIM KE SERVER =================
  if (!hasSentWeight || fabs(totalWeight - lastSentWeight) > WEIGHT_CHANGE_THRESHOLD) {
    sendWeightUpdate(tampil);
    lastSentWeight = totalWeight;
    hasSentWeight = true;
  }
}

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    reconnectWiFi();
    return;
  }

  handleWeight();

  delay(100);
}