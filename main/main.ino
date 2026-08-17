#include "HX711.h"
#include <TM1637Display.h>
#include <WiFi.h>

// ================= WIFI =================
const char* WIFI_SSID = "Hans M";
const char* WIFI_PASSWORD = "123456789";

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

// ================= LED =================
#define LED1_PIN 4    // 10g - 500g
#define LED2_PIN 13   // <10g
#define LED3_PIN 16   // >500g

void connectWiFi() {

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Menghubungkan ke WiFi");

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 20) {

    digitalWrite(LED1_PIN, !digitalRead(LED1_PIN));
    digitalWrite(LED2_PIN, !digitalRead(LED2_PIN));
    digitalWrite(LED3_PIN, !digitalRead(LED3_PIN));

    delay(500);
    Serial.print(".");
    attempts++;
  }

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi terhubung. IP : ");
    Serial.println(WiFi.localIP());
  }
  else {
    Serial.println();
    Serial.println("WiFi gagal terhubung. LED akan berkedip terus.");

    while (true) {
      digitalWrite(LED1_PIN, !digitalRead(LED1_PIN));
      digitalWrite(LED2_PIN, !digitalRead(LED2_PIN));
      digitalWrite(LED3_PIN, !digitalRead(LED3_PIN));
      delay(300);
    }
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
  Serial.print("RAW1 : ");
  Serial.print(raw1);

  Serial.print("   Beban1 : ");
  Serial.print(weight1, 2);
  Serial.print(" g");

  Serial.print(" || RAW2 : ");
  Serial.print(raw2);

  Serial.print("   Beban2 : ");
  Serial.print(weight2, 2);
  Serial.print(" g");

  Serial.print(" || Total : ");
  Serial.print(totalWeight, 2);
  Serial.println(" g");

  // ================= TM1637 =================
  int tampil = (int)(totalWeight + 0.5);   // pembulatan

  if (tampil < 0)
    tampil = 0;

  if (tampil > 9999)
    tampil = 9999;

  // Display hanya berubah setiap 2 detik
  if (millis() - lastDisplayUpdate >= 2000) {
    lastDisplayUpdate = millis();
    display.showNumberDec(tampil, false);   // false = dot mati
  }
}

void loop() {
  handleWeight();

  delay(100);
}