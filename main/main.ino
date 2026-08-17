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
const char* CONTAINER_ID = "1";
const float WEIGHT_CHANGE_THRESHOLD = 0.5;

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
#define LED1_PIN 4    // 10g - 500g
#define LED2_PIN 13   // <10g
#define LED3_PIN 16   // >500g

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

  float weight1 = 0;
  float weight2 = 0;

  long raw1 = 0;
  long raw2 = 0;

  // ================= LOAD CELL 1 =================
  if (scale1.is_ready()) {
    raw1 = scale1.read();
    weight1 = scale1.get_units(20);

    if (weight1 < 0)
      weight1 = 0;
  }

  // ================= LOAD CELL 2 =================
  if (scale2.is_ready()) {
    raw2 = scale2.read();
    weight2 = scale2.get_units(20);

    if (weight2 < 0)
      weight2 = 0;
  }

  // ================= TOTAL BERAT =================
  float totalWeight = weight1 + weight2;

  // ================= LOGIKA LED =================
  if (totalWeight < 10.0) {

    digitalWrite(LED2_PIN, HIGH);
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED3_PIN, LOW);

  }
  else if (totalWeight > 500.0) {

    digitalWrite(LED2_PIN, LOW);
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED3_PIN, HIGH);

  }
  else {

    digitalWrite(LED2_PIN, LOW);
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED3_PIN, LOW);

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
  if (millis() - lastDisplayUpdate >= 2000) {
    lastDisplayUpdate = millis();
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