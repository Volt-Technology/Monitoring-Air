#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// Pin
constexpr uint8_t TRIG_PIN   = 25;
constexpr uint8_t ECHO_PIN   = 34;
constexpr uint8_t BUZZER_PIN = 18;

// Buzzer
constexpr int BUZZER_ON  = 100;
constexpr int BUZZER_OFF = 98;

// OLED
Adafruit_SH1106G display(128, 64, &Wire, -1);

// Air
constexpr float JARAK_PENUH_CM  = 5.0f;
constexpr float JARAK_KOSONG_CM = 20.0f;
constexpr float JARAK_MIN_CM    = 2.0f;
constexpr int BATAS_RENDAH      = 20;
constexpr int BATAS_PENUH       = 90;

// Sensor
constexpr float CM_PER_US         = (331.3f + 0.606f * 28.0f) / 10000.0f;
constexpr float FAKTOR_KALIBRASI  = 0.5333f;
constexpr float ALPHA = 0.4f;

constexpr uint8_t JUMLAH_SAMPEL   = 5;
constexpr uint8_t MAKS_GAGAL      = 3;
constexpr uint32_t INTERVAL_MS    = 300;
constexpr uint32_t TIMEOUT_US     = (uint32_t)(
  ((JARAK_KOSONG_CM + 30.0f) * 2.0f) / CM_PER_US);

float jarakHalus      = 0;
uint8_t sudahInit     = false;
uint8_t buzzerNyala   = false;
uint8_t gagalBeruntun = 0;
uint32_t tTerakhir    = 0;

void setBuzzer(bool nyala) {
  buzzerNyala = nyala;
  digitalWrite(BUZZER_PIN, nyala);
}

void updateBuzzer(int persen) {
  if (!buzzerNyala && persen >= BUZZER_ON) setBuzzer(true);
  else if (buzzerNyala && persen < BUZZER_OFF) setBuzzer(false);
}

float bacaJarakSekali() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long durasi = pulseIn(ECHO_PIN, HIGH, TIMEOUT_US);
  if (durasi == 0) return -1.0f;

  float jarak = durasi * CM_PER_US * 0.5f * FAKTOR_KALIBRASI;
  return (jarak < JARAK_MIN_CM) ? -1.0f : jarak;
}

float bacaJarakMedian() {
  float sampel[JUMLAH_SAMPEL];
  uint8_t n = 0;

  for (uint8_t i = 0; i < JUMLAH_SAMPEL; i++) {
    float d = bacaJarakSekali();
    if (d > 0) sampel[n++] = d;
    delay(30);
  }
  if (n < JUMLAH_SAMPEL / 2 + 1) return NAN;

  for (uint8_t i = 1; i < n; i++) {
    float key = sampel[i];
    int8_t j = i - 1;
    while (j >= 0 && sampel[j] > key) {
      sampel[j + 1] = sampel[j];
      j--;
    }
    sampel[j + 1] = key;
  }
  return sampel[n / 2];
}

int jarakKePersen(float jarak) {
  float persen = (JARAK_KOSONG_CM - jarak) / 
  (JARAK_KOSONG_CM - JARAK_PENUH_CM) * 100.0f;
  return constrain((int)roundf(persen), 0, 100);
}

void teksTengah(const char* teks, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(1);
  display.getTextBounds(teks, 0, y, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, y);
  display.print(teks);
}

void tampilError() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setCursor(35, 30);
  display.print("CEK SENSOR");
  display.display();
}

void tampilUtama(float jarakCm, int persen) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  teksTengah("MONITORING AIR", 0);
  display.drawFastHLine(0, 10, 128, SH110X_WHITE);

  display.setTextSize(3);
  display.setCursor(0, 16);
  display.print(persen);
  display.setTextSize(2);
  display.print("%");

  display.setTextSize(1);
  display.setCursor(0, 44);
  display.printf("Jarak: %.1f cm", jarakCm);
  display.setCursor(0, 56);
  display.print(persen <= BATAS_RENDAH ? "RENDAH" : persen >= 
               BATAS_PENUH ? "PENUH" : "NORMAL");

  // bar level
  constexpr int BX = 90, 
                BY = 14, 
                BW = 32, 
                BH = 48;
                
  constexpr int isiMaks = BH - 4;
  int isi = (persen * isiMaks + 50) / 100;
  display.drawRect(BX, BY, BW, BH, SH110X_WHITE);
  display.fillRect(BX + 2, BY + 2 + (isiMaks - isi), 
                    BW - 4, isi, SH110X_WHITE);

  display.display();
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  setBuzzer(false);

  Wire.begin();
  if (!display.begin(0x3C, true)) {
    Serial.println("Gagal inisialisasi OLED!");
    while (true) delay(1000);
  }
}

void loop() {
  uint32_t sekarang = millis();
  if (sekarang - tTerakhir < INTERVAL_MS) return;
  tTerakhir = sekarang;

  float jarak = bacaJarakMedian();

  if (isnan(jarak)) {
    if (++gagalBeruntun >= MAKS_GAGAL) {
      setBuzzer(false);
      tampilError();
    }
    return;
  }
  gagalBeruntun = 0;

  jarakHalus = sudahInit ? ALPHA * jarak + 
              (1.0f - ALPHA) * jarakHalus : jarak;
  sudahInit = true;

  int persen = jarakKePersen(jarakHalus);
  updateBuzzer(persen);
  tampilUtama(jarakHalus, persen);

  Serial.printf("Jarak: %.1f cm -> %d%% | Status: %s\n",
                jarakHalus, persen, buzzerNyala ? "ON" : "OFF");
}
