

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <LoRa.h>
#include <PZEM004Tv30.h>
#include <Preferences.h>

// ─────────────────────────────────────────────
//  WiFi / NTP CREDENTIALS  — edit before flash
// ─────────────────────────────────────────────
#define WIFI_SSID   "ESP"
#define WIFI_PASS   "abcd1234"

#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.google.com"
#define NTP_TZ      "UTC+5:30"   // POSIX tz string — adjust for your region
                               // e.g. "WIB-7" for Jakarta, "EST5EDT,M3.2.0,M11.1.0" for US East

// ─────────────────────────────────────────────
//  PIN DEFINITIONS  (UNCHANGED)
// ─────────────────────────────────────────────
#define TFT_CS    5
#define TFT_RST  22
#define TFT_DC   21

#define LORA_SCK   18
#define LORA_MOSI  23
#define LORA_MISO  19
#define LORA_NSS   15
#define LORA_RST   14
#define LORA_DIO0  26

#define PZEM_RX  16
#define PZEM_TX  17

#define BUTTON_PIN  4
#define RELAY_PIN   2

// ─────────────────────────────────────────────
//  SYSTEM CONSTANTS  (UNCHANGED except DISPLAY_MS)
// ─────────────────────────────────────────────
#define DEVICE_ID              "1A"

#define LORA_FREQUENCY         433E6
#define LORA_BANDWIDTH         125E3
#define LORA_SPREADING_FACTOR  7
#define LORA_CODING_RATE       5
#define LORA_TX_POWER          17

#define PZEM_WARMUP_MS         3000
#define PZEM_V_MAX             280.0f
#define PZEM_V_MIN              50.0f
#define PZEM_A_MAX             100.0f
#define PZEM_W_MAX           25000.0f
#define PZEM_HZ_MIN            40.0f
#define PZEM_HZ_MAX            70.0f

#define BTN_DEBOUNCE_MS        30
#define BTN_SHORT_MAX_MS     1000
#define BTN_LONG_MIN_MS      3000

#define PZEM_READ_MS          2000
#define LORA_TX_MS            5000
#define DISPLAY_MS            1000    // ← raised from 500 ms to 1 s (fix #2)
#define NVS_SAVE_MS          60000UL

#define NVS_NS          "energy"
#define NVS_TOTAL       "total"
#define NVS_MONTHLY     "monthly"
#define NVS_PREVDAY     "prevday"
#define NVS_MONTH       "month"
#define NVS_DAY         "day"

// WiFi connect timeout at boot
#define WIFI_TIMEOUT_MS       10000UL

// ─────────────────────────────────────────────
//  COLOUR PALETTE (RGB565)  (UNCHANGED)
// ─────────────────────────────────────────────
#define CLR_BG      0x0000
#define CLR_HDR0    0x2945
#define CLR_HDR1    0x4228
#define CLR_LABEL   0x8C51
#define CLR_VALUE   0xFFFF
#define CLR_GOOD    0x07E0
#define CLR_WARN    0xFFE0
#define CLR_ALERT   0xF800
#define CLR_CYAN    0x07FF
#define CLR_ORANGE  0xFD20
#define CLR_DIM     0x4208

// ─────────────────────────────────────────────
//  GLOBAL OBJECTS  (UNCHANGED)
// ─────────────────────────────────────────────
Adafruit_ST7735  tft  = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
PZEM004Tv30      pzem(Serial2, PZEM_RX, PZEM_TX);
Preferences      prefs;

// ─────────────────────────────────────────────
//  LIVE MEASUREMENTS  (UNCHANGED)
// ─────────────────────────────────────────────
float voltage   = 0.0f;
float current   = 0.0f;
float power     = 0.0f;
float frequency = 0.0f;
float pzemKwh   = 0.0f;
bool  pzemOk    = false;

// ─────────────────────────────────────────────
//  BILLING COUNTERS  (UNCHANGED)
// ─────────────────────────────────────────────
float totalKwh   = 0.0f;
float monthlyKwh = 0.0f;
float prevDayKwh = 0.0f;
float dailyKwh   = 0.0f;

float prevPzemKwh  = 0.0f;
bool  countersReady = false;

uint32_t dayStartMs   = 0;
uint32_t monthStartMs = 0;

// ─────────────────────────────────────────────
//  RELAY STATE  (UNCHANGED)
// ─────────────────────────────────────────────
bool relayOn = true;

// ─────────────────────────────────────────────
//  DISPLAY STATE
// ─────────────────────────────────────────────
uint8_t currentPage = 0;
uint8_t lastPage    = 255;

// ── Flash state (fix #3) ─────────────────────────────────────────────
// triggerFlash() only sets pending flag; SPI fill happens in handleFlash().
bool     flashPending = false;  // NEW: deferred fill not yet sent to TFT
bool     flashActive  = false;
uint32_t flashStart   = 0;
uint16_t flashColor   = CLR_GOOD;
#define  FLASH_MS     150

// ── Anti-flicker: shadow copies of last drawn values (fix #2) ────────
// drawPage0 compares against these before issuing any SPI writes.
struct P0Shadow {
  float    voltage   = -1.0f;
  float    current   = -1.0f;
  float    power     = -1.0f;
  float    frequency = -1.0f;
  bool     relayOn   = false;
  bool     pzemOk    = false;
  bool     warmup    = false;
} p0;

struct P1Shadow {
  float    totalKwh  = -1.0f;
  char     timeBuf[20] = {0};
} p1;

// ─────────────────────────────────────────────
//  NTP / RTC STATE  (NEW — replaces simulated time)
// ─────────────────────────────────────────────
bool ntpSynced = false;   // true once first successful NTP sync

// ─────────────────────────────────────────────
//  BUTTON STATE MACHINE  (UNCHANGED)
// ─────────────────────────────────────────────
enum BtnState { BTN_IDLE, BTN_DEBOUNCE, BTN_HELD, BTN_LONG_DONE, BTN_RELEASE };
BtnState btnState     = BTN_IDLE;
uint32_t btnTimer     = 0;
uint32_t btnPressTime = 0;
bool     btnLongDone  = false;

// ─────────────────────────────────────────────
//  TIMING  (UNCHANGED)
// ─────────────────────────────────────────────
uint32_t lastPzemRead   = 0;
uint32_t lastLoraTx     = 0;
uint32_t lastDisplayUpd = 0;
uint32_t lastNvsSave    = 0;

// ─────────────────────────────────────────────
//  PROTOTYPES
// ─────────────────────────────────────────────
void initDisplay();
void initLoRa();
void initWiFiNTP();
void loadPreferences();
void savePreferences();
void resetTotalConsumption();
void readPZEM();
void updateCounters();
void setRelay(bool on);
void transmitLoRa();
void receiveLoRa();
void handleButton();
void handleFlash();
void triggerFlash(uint16_t color);
void invalidateShadows();
void drawCurrentPage();
void drawPage0();
void drawPage1();
void drawHdr(const char* title, uint16_t color, const char* pg);
void drawRow(uint8_t y, uint8_t h, const char* label, const char* value,
             uint16_t lblColor, uint16_t valColor);
void getRealTime(char* buf, size_t len);   // replaces getSimTime()

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println(F("[TX] Postpaid v2.2 booting..."));

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN,  OUTPUT);
  setRelay(true);

  loadPreferences();
  initDisplay();

  // NTP init before LoRa so SPI bus is free during WiFi association
  initWiFiNTP();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  initLoRa();

  Serial2.begin(9600, SERIAL_8N1, PZEM_RX, PZEM_TX);

  dayStartMs   = millis();
  monthStartMs = millis();

  Serial.println(F("[TX] Ready."));
}

// ─────────────────────────────────────────────
//  LOOP  (UNCHANGED structure)
// ─────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  handleButton();
  handleFlash();   // must run every loop tick for smooth flash timing

  if (now - lastPzemRead >= PZEM_READ_MS) {
    lastPzemRead = now;
    readPZEM();
    if (pzemOk) updateCounters();
  }

  receiveLoRa();

  if (now - lastLoraTx >= LORA_TX_MS) {
    lastLoraTx = now;
    transmitLoRa();
  }

  // Suppress TFT update while flash is in progress (fix #3)
  if (!flashActive && !flashPending && (now - lastDisplayUpd >= DISPLAY_MS)) {
    lastDisplayUpd = now;
    drawCurrentPage();
  }

  if (now - lastNvsSave >= NVS_SAVE_MS) {
    lastNvsSave = now;
    savePreferences();
  }
}

// ─────────────────────────────────────────────
//  INIT: DISPLAY  (UNCHANGED)
// ─────────────────────────────────────────────
void initDisplay() {
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(CLR_BG);
  tft.setTextWrap(false);

  tft.setTextColor(CLR_CYAN);
  tft.setTextSize(2);
  tft.setCursor(6, 20);  tft.print(F("POSTPAID"));
  tft.setCursor(6, 44);  tft.print(F("METER"));
  tft.setTextSize(1);
  tft.setTextColor(CLR_LABEL);
  tft.setCursor(6, 74);  tft.print(F("Device: " DEVICE_ID));
  tft.setCursor(6, 90);  tft.print(F("v2.2 — Init..."));
  delay(1500);
  tft.fillScreen(CLR_BG);
}

// ─────────────────────────────────────────────
//  INIT: WiFi + NTP  (NEW)
//
//  Strategy:
//  - Connect to WiFi with a 10 s timeout.
//  - If connected, call configTime() and wait up to 5 s for a valid epoch.
//  - If WiFi or NTP fails we continue without real time (display shows
//    "No NTP" until sync succeeds — the loop does NOT retry; the RTC
//    will have a valid time once it syncs even if WiFi later drops).
//  - WiFi is NOT disconnected after sync so the internal RTC stays
//    driven by the hardware timer (no dependency on WiFi staying up).
// ─────────────────────────────────────────────
void initWiFiNTP() {
  tft.fillScreen(CLR_BG);
  tft.setTextSize(1);
  tft.setTextColor(CLR_LABEL);
  tft.setCursor(4, 20); tft.print(F("Connecting WiFi..."));
  tft.setCursor(4, 32); tft.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WiFi] Not connected — continuing without NTP."));
    tft.setTextColor(CLR_WARN);
    tft.setCursor(4, 48); tft.print(F("WiFi failed — no NTP"));
    delay(1500);
    tft.fillScreen(CLR_BG);
    return;
  }

  Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
  tft.setTextColor(CLR_GOOD);
  tft.setCursor(4, 48); tft.print(F("WiFi OK — syncing NTP..."));

  // Configure SNTP — uses ESP32 Arduino core's built-in SNTP client
  configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
  setenv("TZ", NTP_TZ, 1);
  tzset();

  // Wait up to 5 s for a valid time
  struct tm ti;
  uint32_t t1 = millis();
  bool got = false;
  while (millis() - t1 < 5000) {
    if (getLocalTime(&ti) && ti.tm_year > 100) { got = true; break; }
    delay(200);
  }

  if (got) {
    ntpSynced = true;
    Serial.printf("[NTP] Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                  ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                  ti.tm_hour, ti.tm_min, ti.tm_sec);
    tft.setTextColor(CLR_GOOD);
    tft.setCursor(4, 60); tft.print(F("NTP OK"));
  } else {
    Serial.println(F("[NTP] Sync timeout."));
    tft.setTextColor(CLR_WARN);
    tft.setCursor(4, 60); tft.print(F("NTP timeout"));
  }

  delay(1000);
  tft.fillScreen(CLR_BG);
}

// ─────────────────────────────────────────────
//  INIT: LoRa  (UNCHANGED)
// ─────────────────────────────────────────────
void initLoRa() {
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  uint8_t retries = 5;
  while (!LoRa.begin(LORA_FREQUENCY) && retries--) {
    Serial.println(F("[LoRa] Retry..."));
    delay(500);
  }
  if (retries == 0) {
    Serial.println(F("[LoRa] FATAL"));
    tft.fillScreen(CLR_ALERT);
    tft.setCursor(4, 56); tft.setTextColor(CLR_VALUE);
    tft.setTextSize(1);   tft.print(F("LoRa INIT FAILED"));
    while (true) delay(1000);
  }
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setTxPower(LORA_TX_POWER);
  LoRa.enableCrc();
  Serial.println(F("[LoRa] OK."));
}

// ─────────────────────────────────────────────
//  NVS  (UNCHANGED)
// ─────────────────────────────────────────────
void loadPreferences() {
  prefs.begin(NVS_NS, true);
  totalKwh   = prefs.getFloat(NVS_TOTAL,   0.0f);
  monthlyKwh = prefs.getFloat(NVS_MONTHLY, 0.0f);
  prevDayKwh = prefs.getFloat(NVS_PREVDAY, 0.0f);
  prefs.end();
  dailyKwh = 0.0f;
  Serial.printf("[NVS] total=%.3f monthly=%.3f prevday=%.3f\n",
                totalKwh, monthlyKwh, prevDayKwh);
}

void savePreferences() {
  if (totalKwh == 0.0f && monthlyKwh == 0.0f) return;
  prefs.begin(NVS_NS, false);
  prefs.putFloat(NVS_TOTAL,   totalKwh);
  prefs.putFloat(NVS_MONTHLY, monthlyKwh);
  prefs.putFloat(NVS_PREVDAY, prevDayKwh);
  prefs.end();
  Serial.println(F("[NVS] Saved."));
}

// ─────────────────────────────────────────────
//  RESET TOTAL CONSUMPTION  (UNCHANGED)
// ─────────────────────────────────────────────
void resetTotalConsumption() {
  totalKwh   = 0.0f;
  monthlyKwh = 0.0f;
  prevDayKwh = 0.0f;
  dailyKwh   = 0.0f;
  prevPzemKwh = pzemKwh;
  prefs.begin(NVS_NS, false);
  prefs.putFloat(NVS_TOTAL,   0.0f);
  prefs.putFloat(NVS_MONTHLY, 0.0f);
  prefs.putFloat(NVS_PREVDAY, 0.0f);
  prefs.end();
  Serial.println(F("[RESET] Total consumption cleared."));
}

// ─────────────────────────────────────────────
//  PZEM READ + SPIKE FILTER  (UNCHANGED)
// ─────────────────────────────────────────────
void readPZEM() {
  float v = pzem.voltage();
  float i = pzem.current();
  float p = pzem.power();
  float f = pzem.frequency();
  float e = pzem.energy();

  if (millis() < PZEM_WARMUP_MS) {
    pzemOk = false;
    Serial.println(F("[PZEM] Warmup — skipping."));
    return;
  }

  if (isnan(v) || isnan(i) || isnan(p) || isnan(f) || isnan(e)) {
    pzemOk = false;
    Serial.println(F("[PZEM] Read failed — NaN."));
    return;
  }

  bool sane = (v >= PZEM_V_MIN   && v <= PZEM_V_MAX)  &&
              (i >= 0.0f          && i <= PZEM_A_MAX)  &&
              (p >= 0.0f          && p <= PZEM_W_MAX)  &&
              (f >= PZEM_HZ_MIN   && f <= PZEM_HZ_MAX);

  if (!sane) {
    pzemOk = false;
    Serial.printf("[PZEM] Spike rejected: V=%.1f A=%.1f W=%.1f Hz=%.1f\n",
                  v, i, p, f);
    return;
  }

  pzemOk    = true;
  voltage   = v;
  current   = i;
  power     = p;
  frequency = f;
  pzemKwh   = e;

  if (!countersReady) {
    prevPzemKwh  = pzemKwh;
    countersReady = true;
    dayStartMs   = millis();
    monthStartMs = millis();
    Serial.printf("[PZEM] First valid read — baseline anchored at %.4f kWh\n",
                  prevPzemKwh);
  }

  Serial.printf("[PZEM] V=%.1f A=%.3f W=%.1f Hz=%.1f E=%.4f\n",
                voltage, current, power, frequency, pzemKwh);
}

// ─────────────────────────────────────────────
//  ENERGY COUNTER UPDATE  (UNCHANGED)
// ─────────────────────────────────────────────
void updateCounters() {
  if (!countersReady) return;

  uint32_t now = millis();

  float delta = pzemKwh - prevPzemKwh;
  if (delta < 0.0f) delta = 0.0f;
  prevPzemKwh = pzemKwh;

  totalKwh   += delta;
  monthlyKwh += delta;
  dailyKwh   += delta;

  if ((now - dayStartMs) >= 86400000UL) {
    dayStartMs = now;
    prevDayKwh = dailyKwh;
    dailyKwh   = 0.0f;
    Serial.printf("[COUNTER] Day rollover — yesterday=%.4f kWh\n", prevDayKwh);
    savePreferences();
  }

  if ((now - monthStartMs) >= (30UL * 86400000UL)) {
    monthStartMs = now;
    monthlyKwh   = 0.0f;
    Serial.println(F("[COUNTER] Month rollover."));
    savePreferences();
  }
}

// ─────────────────────────────────────────────
//  RELAY CONTROL  (UNCHANGED)
// ─────────────────────────────────────────────
void setRelay(bool on) {
  relayOn = on;
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  Serial.printf("[RELAY] %s\n", on ? "ON" : "OFF");
}

// ─────────────────────────────────────────────
//  LoRa TRANSMIT  (UNCHANGED)
// ─────────────────────────────────────────────
void transmitLoRa() {
  char pkt[128];
  snprintf(pkt, sizeof(pkt),
           "TX,%s,%.1f,%.2f,%.1f,%.1f,%.3f,%.2f,%.2f,%d",
           DEVICE_ID,
           voltage, current, power, frequency,
           totalKwh, monthlyKwh, prevDayKwh,
           relayOn ? 1 : 0);
  LoRa.beginPacket();
  LoRa.print(pkt);
  LoRa.endPacket();
  Serial.print(F("[LoRa] TX: "));
  Serial.println(pkt);
}

// ─────────────────────────────────────────────
//  LoRa RECEIVE  (UNCHANGED logic; flash now deferred — fix #3)
// ─────────────────────────────────────────────
void receiveLoRa() {
  int sz = LoRa.parsePacket();
  if (sz == 0 || sz > 64) return;

  char buf[65] = {0};
  uint8_t idx = 0;
  while (LoRa.available() && idx < 64) buf[idx++] = (char)LoRa.read();
  buf[idx] = '\0';

  Serial.print(F("[LoRa] RX: ")); Serial.println(buf);

  if (strncmp(buf, "RELAY,", 6) == 0) {
    int s = atoi(&buf[6]);
    if (s == 0 || s == 1) {
      setRelay(s == 1);
      triggerFlash(relayOn ? CLR_GOOD : CLR_ALERT);  // deferred — safe
    }
  }
}

// ─────────────────────────────────────────────
//  BUTTON HANDLER  (UNCHANGED)
// ─────────────────────────────────────────────
void handleButton() {
  bool raw = (digitalRead(BUTTON_PIN) == LOW);
  uint32_t now = millis();

  switch (btnState) {
    case BTN_IDLE:
      if (raw) { btnState = BTN_DEBOUNCE; btnTimer = now; }
      break;

    case BTN_DEBOUNCE:
      if (!raw) {
        btnState = BTN_IDLE;
      } else if (now - btnTimer >= BTN_DEBOUNCE_MS) {
        btnState     = BTN_HELD;
        btnPressTime = now;
        btnLongDone  = false;
      }
      break;

    case BTN_HELD:
      if (!raw) {
        btnState = BTN_RELEASE; btnTimer = now;
      } else if (!btnLongDone && (now - btnPressTime >= BTN_LONG_MIN_MS)) {
        btnLongDone = true;
        btnState    = BTN_LONG_DONE;
        resetTotalConsumption();
        triggerFlash(CLR_WARN);
        Serial.println(F("[BTN] Long press — total reset."));
      }
      break;

    case BTN_LONG_DONE:
      if (!raw) { btnState = BTN_RELEASE; btnTimer = now; }
      break;

    case BTN_RELEASE:
      if (raw) {
        btnState = BTN_HELD;
      } else if (now - btnTimer >= BTN_DEBOUNCE_MS) {
        uint32_t held = now - btnPressTime;
        if (!btnLongDone && held < BTN_SHORT_MAX_MS) {
          currentPage = (currentPage == 0) ? 1 : 0;
          lastPage    = 255;
          triggerFlash(CLR_CYAN);
          Serial.printf("[BTN] Page -> %d\n", currentPage);
        }
        btnLongDone = false;
        btnState    = BTN_IDLE;
      }
      break;
  }
}

// ─────────────────────────────────────────────
//  NON-BLOCKING FLASH  (FIX #3)
//
//  triggerFlash() sets flashPending; it does NOT touch the TFT here.
//  handleFlash() is the only place that issues SPI calls for the flash,
//  and it runs from the main loop where the SPI bus is not in use by
//  LoRa.  This prevents LoRa-ISR / TFT-SPI contention.
// ─────────────────────────────────────────────
void triggerFlash(uint16_t color) {
  flashColor   = color;
  flashPending = true;   // actual fillScreen deferred to handleFlash()
  flashActive  = false;  // reset so handleFlash() knows to start fresh
}

void handleFlash() {
  // Phase 1: pending → start the flash (do the fillScreen here, safely)
  if (flashPending) {
    flashPending = false;
    flashActive  = true;
    flashStart   = millis();
    tft.fillScreen(flashColor);   // one safe SPI call from main loop
    return;
  }

  // Phase 2: active → wait FLASH_MS then restore
  if (!flashActive) return;
  if (millis() - flashStart >= FLASH_MS) {
    flashActive = false;
    tft.fillScreen(CLR_BG);
    invalidateShadows();  // force full page redraw after flash
    lastPage = 255;
  }
}

// ─────────────────────────────────────────────
//  SHADOW INVALIDATION  (FIX #2 helper)
//  Called after flash or page change to force
//  a complete repaint on the next draw cycle.
// ─────────────────────────────────────────────
void invalidateShadows() {
  p0.voltage   = -1.0f;
  p0.current   = -1.0f;
  p0.power     = -1.0f;
  p0.frequency = -1.0f;
  p0.relayOn   = !relayOn;   // guaranteed mismatch
  p0.pzemOk    = !pzemOk;
  p0.warmup    = !(millis() < PZEM_WARMUP_MS);
  p1.totalKwh  = -1.0f;
  p1.timeBuf[0] = '\0';
}

// ─────────────────────────────────────────────
//  REAL-TIME CLOCK  (NEW — replaces getSimTime)
//
//  Uses ESP32 SNTP via getLocalTime().  The internal RTC continues
//  counting after WiFi disconnects, so no fallback branch is needed
//  once ntpSynced is true.
//  Format: DD/MM/YYYY HH:MM:SS
//  If NTP has never synced, shows "Syncing NTP..."
// ─────────────────────────────────────────────
void getRealTime(char* buf, size_t len) {
  if (!ntpSynced) {
    strncpy(buf, "Syncing NTP...", len);
    buf[len - 1] = '\0';
    return;
  }
  struct tm ti;
  if (!getLocalTime(&ti)) {
    strncpy(buf, "Time unavail.", len);
    buf[len - 1] = '\0';
    return;
  }
  snprintf(buf, len, "%02d/%02d/%04d %02d:%02d:%02d",
           ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
           ti.tm_hour, ti.tm_min, ti.tm_sec);
}

// ─────────────────────────────────────────────
//  DISPLAY ROUTER  (UNCHANGED logic, shadow check added)
// ─────────────────────────────────────────────
void drawCurrentPage() {
  if (flashActive || flashPending) return;
  if (currentPage != lastPage) {
    tft.fillScreen(CLR_BG);
    invalidateShadows();
    lastPage = currentPage;
  }
  if (currentPage == 0) drawPage0();
  else                  drawPage1();
}

// ─────────────────────────────────────────────
//  DISPLAY HELPERS  (UNCHANGED)
// ─────────────────────────────────────────────
void drawHdr(const char* title, uint16_t color, const char* pg) {
  tft.fillRect(0, 0, 160, 15, color);
  tft.setTextSize(1);
  tft.setTextColor(CLR_BG);
  tft.setCursor(4, 4);
  tft.print(title);
  tft.setCursor(148, 4);
  tft.print(pg);
}

void drawRow(uint8_t y, uint8_t h,
             const char* label, const char* value,
             uint16_t lblColor, uint16_t valColor) {
  tft.fillRect(0, y, 160, h, CLR_BG);
  tft.setTextSize(1);
  tft.setTextColor(lblColor);
  tft.setCursor(4, y + 4);
  tft.print(label);
  tft.setTextColor(valColor);
  tft.setCursor(90, y + 4);
  tft.print(value);
}

// ─────────────────────────────────────────────
//  PAGE 0: LIVE READINGS  (FIX #2 — selective redraw)
//
//  Each row is only repainted when its value has changed.
//  Header is static once drawn (repainted only after page switch/flash).
// ─────────────────────────────────────────────
void drawPage0() {
  // Header — only on first draw of this page
  if (lastPage == 255 || p0.voltage < 0.0f) {
    drawHdr("LIVE READINGS", CLR_HDR0, "1");
  }

  char buf[20];
  const uint8_t ROW_H = 18;
  uint8_t y = 17;

  // ── Voltage ──────────────────────────────────────────────────────
  if (voltage != p0.voltage) {
    snprintf(buf, sizeof(buf), "%.1f V", voltage);
    drawRow(y, ROW_H, "Voltage", buf, CLR_LABEL,
            (voltage >= PZEM_V_MIN && voltage <= PZEM_V_MAX) ? CLR_VALUE : CLR_WARN);
    p0.voltage = voltage;
  }
  y += ROW_H;

  // ── Current ──────────────────────────────────────────────────────
  if (current != p0.current) {
    snprintf(buf, sizeof(buf), "%.2f A", current);
    drawRow(y, ROW_H, "Current", buf, CLR_LABEL, CLR_VALUE);
    p0.current = current;
  }
  y += ROW_H;

  // ── Power ────────────────────────────────────────────────────────
  if (power != p0.power) {
    snprintf(buf, sizeof(buf), "%.1f W", power);
    drawRow(y, ROW_H, "Power", buf, CLR_LABEL, CLR_VALUE);
    p0.power = power;
  }
  y += ROW_H;

  // ── Frequency ────────────────────────────────────────────────────
  if (frequency != p0.frequency) {
    snprintf(buf, sizeof(buf), "%.1f Hz", frequency);
    drawRow(y, ROW_H, "Frequency", buf, CLR_LABEL,
            (frequency >= PZEM_HZ_MIN && frequency <= PZEM_HZ_MAX)
              ? CLR_VALUE : CLR_WARN);
    p0.frequency = frequency;
  }
  y += ROW_H;

  // ── Relay ────────────────────────────────────────────────────────
  if (relayOn != p0.relayOn) {
    drawRow(y, ROW_H, "Relay",
            relayOn ? "ON" : "OFF",
            CLR_LABEL,
            relayOn ? CLR_GOOD : CLR_ALERT);
    p0.relayOn = relayOn;
  }
  y += ROW_H;

  // ── Status bar ───────────────────────────────────────────────────
  bool nowWarmup = (millis() < PZEM_WARMUP_MS);
  if (pzemOk != p0.pzemOk || nowWarmup != p0.warmup) {
    tft.fillRect(0, y, 160, 128 - y, CLR_DIM);
    tft.setTextSize(1);
    tft.setTextColor(pzemOk ? CLR_GOOD : CLR_ALERT);
    tft.setCursor(4, y + 4);
    tft.print(pzemOk ? "PZEM: OK" : "PZEM: FAIL");
    if (nowWarmup) {
      tft.setTextColor(CLR_WARN);
      tft.setCursor(80, y + 4);
      tft.print(F("WARMUP"));
    }
    p0.pzemOk  = pzemOk;
    p0.warmup  = nowWarmup;
  }
}

// ─────────────────────────────────────────────
//  PAGE 1: CONSUMPTION INFO  (FIX #2 — selective redraw)
//
//  Layout UNCHANGED from v2.1.
//  Only totalKwh number and time string are updated when they change.
//  Static chrome (labels, dividers, device ID, hint) drawn once.
// ─────────────────────────────────────────────
void drawPage1() {
  bool firstDraw = (p1.totalKwh < 0.0f);

  // ── Static chrome (drawn only on first paint of page) ────────────
  if (firstDraw) {
    drawHdr("CONSUMPTION", CLR_HDR1, "2");

    tft.setTextSize(1);
    tft.setTextColor(CLR_LABEL);
    tft.setCursor(4, 22);
    tft.print(F("TOTAL CONSUMPTION"));

    tft.drawFastHLine(0, 32, 160, CLR_DIM);

    tft.setTextSize(1);
    tft.setTextColor(CLR_LABEL);
    tft.setCursor(64, 64);
    tft.print(F("kWh"));

    tft.drawFastHLine(0, 74, 160, CLR_DIM);

    tft.setTextSize(1);
    tft.setTextColor(CLR_LABEL);
    tft.setCursor(4, 78);
    tft.print(F("Time"));

    tft.drawFastHLine(0, 102, 160, CLR_DIM);

    tft.setTextSize(1);
    tft.setTextColor(CLR_LABEL);
    tft.setCursor(4, 106);
    tft.print(F("Device: "));
    tft.setTextColor(CLR_CYAN);
    tft.print(F(DEVICE_ID));

    tft.setTextColor(CLR_DIM);
    tft.setCursor(4, 118);
    tft.print(F("[hold 3s] = reset total"));
  }

  // ── Total kWh — repaint only when value changes ──────────────────
  if (totalKwh != p1.totalKwh) {
    // Erase old large number area
    tft.fillRect(0, 33, 160, 30, CLR_BG);

    char kwh[20];
    snprintf(kwh, sizeof(kwh), "%.3f", totalKwh);

    tft.setTextSize(3);
    tft.setTextColor(CLR_ORANGE);
    uint8_t charW  = 18;
    uint8_t numLen = strlen(kwh);
    uint8_t xStart = (160 - numLen * charW) / 2;
    if (xStart > 160) xStart = 4;
    tft.setCursor(xStart, 38);
    tft.print(kwh);

    // Restore "kWh" label (was cleared)
    tft.setTextSize(1);
    tft.setTextColor(CLR_LABEL);
    tft.setCursor(64, 64);
    tft.print(F("kWh"));

    p1.totalKwh = totalKwh;
  }

  // ── Time — repaint every second (string changes each second) ─────
  char timeBuf[20];
  getRealTime(timeBuf, sizeof(timeBuf));

  if (strncmp(timeBuf, p1.timeBuf, sizeof(timeBuf)) != 0) {
    // Erase old time line
    tft.fillRect(0, 83, 160, 18, CLR_BG);

    tft.setTextSize(1);
    tft.setTextColor(CLR_VALUE);
    tft.setCursor(4, 90);
    tft.print(timeBuf);

    strncpy(p1.timeBuf, timeBuf, sizeof(p1.timeBuf));
    p1.timeBuf[sizeof(p1.timeBuf) - 1] = '\0';
  }
}
