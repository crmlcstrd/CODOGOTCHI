/*
 * CODOGOTCHI - ESP32 firmware skeleton (untested on hardware: expect to tune touch thresholds)
 *
 * Hardware : ESP32 + 1.3" SH1106 128x64 I2C OLED + passive buzzer + TTP223 touch module
 *   Wiring : OLED  VCC->3V3, GND->GND, SDA->GPIO21, SCL->GPIO22
 *            Buzzer (passive) + -> GPIO25, - -> GND
 *            TTP223 VCC->3V3, GND->GND, OUT->GPIO4 (active-HIGH output).
 *            Nothing else should drive GPIO4.
 * Libraries: Adafruit SH110X (for the SH1106 OLED), Adafruit GFX, ArduinoJson 7.x
 *            (WiFi, HTTPClient, WebServer, Preferences ship with the ESP32 Arduino core; 2.x and 3.x both work)
 *            Install ArduinoJson 7.x specifically - version 6 will not compile this sketch.
 *
 * Design: the device is a "dumb" client. A backend (your website's server) talks to Codeforces + GitHub,
 * computes mood/health/level, and the ESP32 just fetches ~200 bytes of JSON and acts it out.
 *
 * Expected response of GET API_URL  (Authorization: Bearer <device token>):
 *   {"v":1,"mood":2,"health":73,"level":4,"eyes":1,"mouth":2,"celebrate_seq":142,"sleep":false}
 *     mood 0 sad | 1 neutral | 2 happy | 3 ecstatic
 *     eyes / mouth = cosmeticId of the equipped accessory in that slot (0 = nothing equipped)
 *     celebrate_seq increments every time the user earns something new (solve / push)
 *     sleep = true during the user's quiet hours
 *
 * Serial debug commands (115200): m0-m3 mood, h0-h100 health, l<n> level-up, c celebrate, z toggle sleep, t print touch,
 *   1-36 equip that accessory in its slot, 0 clear both slots
 */

#include "accessories.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "types.h"

// ============================ CONFIG ============================
#define OLED_W      128
#define OLED_H      64
#define OLED_ADDR   0x3C
#define PIN_SDA     21
#define PIN_SCL     22
#define PIN_BUZZER  25          // PASSIVE buzzer (an active one can only beep one tone)

// Touch pins, ordered LEFT -> RIGHT. One pin = single copper strip.
static const uint8_t TOUCH_PINS[] = {4};
constexpr uint8_t NSEG = sizeof(TOUCH_PINS) / sizeof(TOUCH_PINS[0]);

static const uint32_t TAP_MAX_MS   = 280;
static const uint32_t MULTI_GAP_MS = 380;
static const uint32_t HOLD_MS      = 650;

static const char* API_URL = "https://codogotchi-backend.codobackend.workers.dev/api/device/state";
static const uint32_t POLL_MS = 30000;   // equip/buy on the website shows up within ~30 s (double-tap + hold = sync now)

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

// ============================ GLOBALS ============================
Adafruit_SH1106G display = Adafruit_SH1106G(OLED_W, OLED_H, &Wire, -1);
Preferences prefs;
PetState pet;

bool muted = false;
bool petting = false;
bool sleeping = false;
bool dbgTouch = false;

// ============================ SOUND ============================
static const Note P_IDLE[]      = {{1800,2200,45},{0,0,80},{2200,1900,35}};
static const Note P_HAPPY[]     = {{1200,1800,70},{0,0,25},{1500,2400,90}};
static const Note P_SAD[]       = {{1400,900,160},{0,0,40},{1000,600,260}};
static const Note P_CELEBRATE[] = {{1047,1047,80},{1319,1319,80},{1568,1568,80},{2093,2093,160},{0,0,40},
                                   {1568,2093,60},{0,0,30},{1568,2093,60},{2093,2637,220}};
static const Note P_POKE[]      = {{900,1500,50}};
static const Note P_PURR[]      = {{520,580,45},{560,500,45},{520,580,45},{560,500,45}};
static const Note P_ANNOYED[]   = {{700,500,90},{0,0,40},{700,480,110}};
static const Note P_SYNC[]      = {{1000,1400,40},{1400,1000,40},{1000,1400,40}};
static const Note P_WAKE[]      = {{800,1600,120}};
static const Note P_MUTE_ON[]   = {{1500,1000,90}};
static const Note P_MUTE_OFF[]  = {{1000,1500,90}};

static const Pattern PATTERNS[SND_COUNT] = {
  {P_IDLE, LEN(P_IDLE)}, {P_HAPPY, LEN(P_HAPPY)}, {P_SAD, LEN(P_SAD)}, {P_CELEBRATE, LEN(P_CELEBRATE)},
  {P_POKE, LEN(P_POKE)}, {P_PURR, LEN(P_PURR)}, {P_ANNOYED, LEN(P_ANNOYED)}, {P_SYNC, LEN(P_SYNC)},
  {P_WAKE, LEN(P_WAKE)}, {P_MUTE_ON, LEN(P_MUTE_ON)}, {P_MUTE_OFF, LEN(P_MUTE_OFF)}
};

QueueHandle_t sndQ;

static void buzz(uint16_t f) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(PIN_BUZZER, f);
#else
  ledcWriteTone(0, f);
#endif
}

static void soundTask(void*) {
  uint8_t id;
  for (;;) {
    if (xQueueReceive(sndQ, &id, portMAX_DELAY) != pdTRUE) continue;
    const Pattern& p = PATTERNS[id];
    for (uint8_t i = 0; i < p.len; i++) {
      const Note& n = p.n[i];
      if (n.f0 == 0) { buzz(0); vTaskDelay(pdMS_TO_TICKS(n.ms)); continue; }
      uint16_t steps = n.ms / 8;
      if (steps < 1) steps = 1;
      for (uint16_t s = 0; s < steps; s++) {
        buzz((uint16_t)(n.f0 + ((int32_t)n.f1 - (int32_t)n.f0) * s / steps));
        vTaskDelay(pdMS_TO_TICKS(8));
      }
    }
    buzz(0);
  }
}

static void playSound(Snd s, bool force = false) {
  if (muted && !force) return;
  uint8_t id = s;
  xQueueSend(sndQ, &id, 0);
}

// ============================ TOUCH ============================
Seg segs[NSEG];

static void touchCalibrate() {
  for (uint8_t i = 0; i < NSEG; i++) {
    segs[i].pin = TOUCH_PINS[i];

    pinMode(segs[i].pin, INPUT);

    segs[i].base = 0;
    segs[i].last = 0;
    segs[i].on = false;
  }
}

static bool segTouched(Seg& s) {
  bool touched = digitalRead(s.pin) == HIGH;

  s.last = touched ? 1 : 0;
  s.on = touched;

  return touched;
}


static Gesture gestureUpdate(uint32_t now) {
  static bool wasTouched = false, holdFired = false, swiped = false, petHold = false;
  static uint32_t downAt = 0, lastUp = 0, lastSegAt = 0, lastDbg = 0;
  static uint8_t taps = 0;
  static int8_t lastSeg = -1;
  Gesture g = G_NONE;

  bool any = false;
  int8_t segNow = -1;
  for (uint8_t i = 0; i < NSEG; i++) if (segTouched(segs[i])) { any = true; if (segNow < 0) segNow = i; }

  if (dbgTouch && now - lastDbg > 150) {
    lastDbg = now;
    for (uint8_t i = 0; i < NSEG; i++) Serial.printf("seg%u raw=%lu on=%d  ", i, (unsigned long)segs[i].last, segs[i].on);
    Serial.println();
  }

  if (any && !wasTouched) { downAt = now; holdFired = false; swiped = false; }

  if (NSEG > 1 && segNow >= 0 && segNow != lastSeg) {
    if (lastSeg >= 0 && now - lastSegAt < 300) { g = (segNow > lastSeg) ? G_SWIPE_R : G_SWIPE_L; swiped = true; taps = 0; }
    lastSeg = segNow;
  }
  if (segNow >= 0) lastSegAt = now;
  if (!any && now - lastSegAt > 600) lastSeg = -1;

  if (!any && wasTouched) {
    if (petHold) { g = G_PET_END; petHold = false; }
    else if (!holdFired && !swiped && now - downAt < TAP_MAX_MS) { taps++; lastUp = now; }
  }

  if (!any && taps && now - lastUp > MULTI_GAP_MS) {
    g = (taps == 1) ? G_TAP1 : (taps == 2) ? G_TAP2 : (taps < 5) ? G_TAP3 : G_SPAM;
    taps = 0;
  }

  if (any && !holdFired && !swiped && now - downAt > HOLD_MS) {
    holdFired = true;
    if (taps == 2) g = G_SYNC_HOLD;
    else { g = G_PET_START; petHold = true; }
    taps = 0;
  }

  wasTouched = any;
  return g;
}

// ============================ FACE MODEL ============================
Face cur = {0.8f, 0.4f, 0, 0, 0, 0, 0, 0}, tgt = cur;
float extraBounce = 0;

static const float MOOD_SMILE[4]  = {-0.7f, 0.0f, 0.6f, 1.0f};
static const float MOOD_MOUTH[4]  = { 0.0f, 0.0f, 0.0f, 0.8f};

Rx rx = RX_NONE;
uint32_t rxStart = 0, rxDur = 0;
static void startRx(Rx r, uint32_t dur) { rx = r; rxStart = millis(); rxDur = dur; }

static float gazeX = 0, gazeY = 0, gazeForceX = 0;
static uint32_t nextGaze = 0, gazeForceUntil = 0, levelShowUntil = 0; // Renamed from ageShowUntil

#define LERP(f) c.f += (t.f - c.f) * k
static void smoothFace(Face& c, const Face& t, float k) {
  LERP(eyeOpen); LERP(droop); LERP(smile); LERP(mouth); LERP(lookX); LERP(lookY); LERP(blush); LERP(happyEyes);
}

static void applyReaction(uint32_t now) {
  if (rx == RX_NONE) return;
  uint32_t t = now - rxStart;
  if (t > rxDur) { rx = RX_NONE; return; }
  float s = t / 1000.f;
  switch (rx) {
    case RX_CELEBRATE:
      tgt.happyEyes = 1; tgt.smile = 1; tgt.mouth = 1; tgt.blush = 1; tgt.droop = 0;
      extraBounce = -fabsf(sinf(s * PI * 3.f)) * 7.f;
      break;
    case RX_POKE:
      tgt.eyeOpen = 1.25f; tgt.droop = 0; tgt.smile = 0; tgt.mouth = 0.6f; tgt.lookX = 0; tgt.lookY = 0;
      extraBounce = 1.5f;
      break;
    case RX_PET:
      tgt.happyEyes = 1; tgt.blush = 1; tgt.smile = max(tgt.smile, 0.7f); tgt.lookX = 0; tgt.lookY = 0;
      extraBounce += sinf(s * 9.f) * 0.8f;
      break;
    case RX_ANNOYED:
      tgt.droop = 0.9f; tgt.eyeOpen = 0.6f; tgt.smile = -0.15f; tgt.mouth = 0; tgt.lookX = 0.9f; tgt.lookY = 0;
      break;
    case RX_STATUS: {
      float h = pet.health / 100.f;
      if (s < 1.6f) { tgt.smile = 0; tgt.mouth = 0; tgt.eyeOpen = 0.35f + 0.9f * h; tgt.droop = constrain((1.f - h) * 1.2f, 0.f, 1.f); }
      else { tgt.eyeOpen = 0.9f; tgt.droop = 0; tgt.smile = constrain(MOOD_SMILE[pet.mood] * 1.3f, -1.f, 1.f); tgt.mouth = MOOD_MOUTH[pet.mood]; }
      tgt.lookX = 0; tgt.lookY = 0;
      break; }
    case RX_SYNC:
      tgt.lookX = cosf(s * 6.f) * 0.9f; tgt.lookY = sinf(s * 6.f) * 0.7f;
      break;
    default: break;
  }
}

static void computeTargets(uint32_t now) {
  float h = pet.health / 100.f;
  uint8_t m = pet.mood > 3 ? 3 : pet.mood;

  tgt.eyeOpen = 0.55f + 0.55f * h;
  tgt.droop = 1.f - h;
  tgt.smile = MOOD_SMILE[m];
  tgt.mouth = MOOD_MOUTH[m];
  tgt.blush = 0; tgt.happyEyes = 0;

  if (now > nextGaze) {
    if (random(3) == 0) { gazeX = 0; gazeY = 0; }
    else { gazeX = random(-100, 101) / 100.f; gazeY = random(-50, 51) / 100.f; }
    nextGaze = now + random(1800, 6000);
  }
  tgt.lookX = (now < gazeForceUntil) ? gazeForceX : gazeX;
  tgt.lookY = gazeY;
  extraBounce = sinf(now / 1100.f) * 0.8f;

  if (sleeping) {
    tgt.eyeOpen = 0.08f; tgt.droop = 0; tgt.smile = 0.2f; tgt.mouth = 0; tgt.lookX = 0; tgt.lookY = 0;
    extraBounce = sinf(now / 1800.f) * 1.2f;
  } else {
    applyReaction(now);
  }
}

// ============================ RENDERING ============================
static float blinkAmount(uint32_t now) {
  static uint32_t next = 2500, start = 0;
  static bool blinking = false;
  if (sleeping) return 0;
  if (!blinking && now >= next) { blinking = true; start = now; }
  if (!blinking) return 0;
  float p = (now - start) / 160.f;
  if (p >= 1.f) { blinking = false; next = now + random(2200, 6500); return 0; }
  return p < 0.5f ? p * 2.f : (1.f - p) * 2.f;
}

static void drawEye(int cx, bool leftEye, const Face& f, float blink) {
  const int ew = 26, baseH = 28, cy = 26;
  float oh = baseH * f.eyeOpen * (1.f - blink);
  if (oh < 2) oh = 2;
  int lx = (int)(f.lookX * 4.f);
  int yc = cy + (int)(f.lookY * 3.f + extraBounce);

  if (f.happyEyes > 0.5f) {
    for (int i = -ew / 2; i <= ew / 2; i++) {
      float k = (float)i / (ew / 2);
      int yy = yc + 5 - (int)(9 * (1.f - k * k));
      for (int t = 0; t < 3; t++) display.drawPixel(cx + lx + i, yy + t, SH110X_WHITE);
    }
    return;
  }
  int x = cx - ew / 2 + lx;
  int y = yc - (int)(oh / 2);
  display.fillRoundRect(x, y, ew, (int)oh, 8, SH110X_WHITE);
  if (f.droop > 0.05f) {
    int d   = (int)(f.droop * oh * 0.6f);
    int lid = (int)(f.droop * oh * 0.2f);
    if (lid > 0) display.fillRect(x - 1, y - 1, ew + 2, lid + 1, SH110X_BLACK);
    if (leftEye) display.fillTriangle(x - 1, y - 1, x + ew + 1, y - 1, x - 1, y + d, SH110X_BLACK);
    else         display.fillTriangle(x + ew + 1, y - 1, x - 1, y - 1, x + ew + 1, y + d, SH110X_BLACK);
  }
}

static void drawMouth(const Face& f) {
  const int cx = 64, cy = 50;
  int W = 10 + (int)(6 * fabsf(f.smile));
  if (f.mouth > 0.3f && fabsf(f.smile) < 0.25f) {
    display.fillCircle(cx, cy + 2, 2 + (int)(f.mouth * 3), SH110X_WHITE);
    return;
  }
  int px = 0, py = 0;
  for (int i = -W; i <= W; i += 2) {
    float k = (float)i / W;
    int y = cy + (int)(f.smile * 8.f * (1.f - k * k));
    if (f.smile > 0.2f && f.mouth > 0.2f) {
      int top = cy - 1;
      int h = (int)((y - top) * f.mouth);
      if (h > 0) { display.drawFastVLine(cx + i, top, h + 1, SH110X_WHITE); display.drawFastVLine(cx + i + 1, top, h + 1, SH110X_WHITE); }
    }
    if (i > -W) { display.drawLine(px, py, cx + i, y, SH110X_WHITE); display.drawLine(px, py + 1, cx + i, y + 1, SH110X_WHITE); }
    px = cx + i; py = y;
  }
}

static void drawBlush(float a) {
  if (a < 0.3f) return;
  for (int dx = 0; dx < 8; dx++) for (int dy = 0; dy < 4; dy++)
    if (((dx + dy) & 1) == 0) { display.drawPixel(10 + dx, 42 + dy, SH110X_WHITE); display.drawPixel(110 + dx, 42 + dy, SH110X_WHITE); }
}

static void drawHeart(int x, int y) {
  display.fillCircle(x - 2, y, 2, SH110X_WHITE); display.fillCircle(x + 2, y, 2, SH110X_WHITE);
  display.fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 5, SH110X_WHITE);
}

static void drawSparkles(uint32_t now) {
  static const int8_t PX[6] = {8, 118, 22, 104, 60, 70}, PY[6] = {10, 12, 56, 54, 4, 60};
  float s = (now - rxStart) / 1000.f;
  for (int k = 0; k < 6; k++) {
    int r = (int)(fabsf(sinf(s * 8.f + k * 1.3f)) * 3.f) + 1;
    display.drawFastHLine(PX[k] - r, PY[k], 2 * r + 1, SH110X_WHITE);
    display.drawFastVLine(PX[k], PY[k] - r, 2 * r + 1, SH110X_WHITE);
  }
}

static void drawCosmetics() {
  if (!pet.eyes && !pet.mouth) return;
  FaceFrame f;
  buildFaceFrame(f, cur.eyeOpen);
  if (pet.eyes)  drawAccessory(pet.eyes, f);
  if (pet.mouth) drawAccessory(pet.mouth, f);
}

static void drawFrame(uint32_t now) {
  display.clearDisplay();
  float blink = blinkAmount(now);
  drawEye(38, true,  cur, blink);
  drawEye(90, false, cur, blink);
  drawMouth(cur);
  drawBlush(cur.blush);
  drawCosmetics();

  if (!sleeping && rx == RX_CELEBRATE) drawSparkles(now);
  if (!sleeping && rx == RX_PET) {
    for (int k = 0; k < 3; k++) {
      float ph = fmodf(now / 1000.f * 0.8f + k * 0.33f, 1.f);
      drawHeart(104 + k * 8, 40 - (int)(ph * 34));
    }
  }
  if (sleeping) {
    display.setTextSize(1);
    for (int k = 0; k < 3; k++) {
      float ph = fmodf(now / 1500.f + k * 0.33f, 1.f);
      display.setCursor(96 + k * 8, 30 - (int)(ph * 24));
      display.print('z');
    }
  }
  if (now < levelShowUntil) { display.setTextSize(1); display.setCursor(0, 0); display.print("LVL "); display.print(pet.level); }
  if (now > 20000 && (pet.lastSyncOkMs == 0 || now - pet.lastSyncOkMs > 30UL * 60000UL)) {
    display.setTextSize(1); display.setCursor(122, 0); display.print('?');
  }
  display.display();
}

// ============================ NETWORK ============================
static bool wifiUp() {
  if (WiFi.status() == WL_CONNECTED) return true;
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
  if (!ssid.length()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) vTaskDelay(pdMS_TO_TICKS(250));
  return WiFi.status() == WL_CONNECTED;
}

static bool fetchState() {
  if (!wifiUp()) { Serial.println("fetch: WiFi not connected"); return false; }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, API_URL)) return false;
  http.setTimeout(15000);
  http.addHeader("Authorization", "Bearer " + prefs.getString("token", ""));
  int code = http.GET();
  if (code != 200) { Serial.printf("fetch: HTTP %d\n", code); http.end(); return false; }
  String body = http.getString();   // getString() copes with chunked responses (getStream() does not)
  http.end();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { Serial.printf("fetch: bad JSON (%s): %s\n", err.c_str(), body.c_str()); return false; }

  pet.lastSyncOkMs = millis();
  if (millis() < pet.demoUntil) return true;

  int mood = doc["mood"] | (int)pet.mood;
  int health = doc["health"] | (int)pet.health;
  int level = doc["level"] | (int)pet.level; // Renamed from age
  uint32_t seq = doc["celebrate_seq"] | 0u;

  if (level > pet.level) pet.levelUpPending = true; // Renamed from ageUpPending
  if (seq > pet.celebrateSeq) pet.celebratePending = true;
  pet.celebrateSeq = seq;
  pet.mood = constrain(mood, 0, 3);
  pet.health = constrain(health, 0, 100);
  pet.level = constrain(level, 0, 50); // Capped at 50
  int e = doc["eyes"]  | (int)pet.eyes;
  int m = doc["mouth"] | (int)pet.mouth;
  if (e < 0 || e > 255 || accessorySlot((uint8_t)e) != 0) e = 0;   // wrong slot or junk -> nothing
  if (m < 0 || m > 255 || accessorySlot((uint8_t)m) != 1) m = 0;
  if (e != pet.eyes || m != pet.mouth) pet.cosmeticChanged = true;
  pet.eyes = e; pet.mouth = m;
  pet.sleepNow = doc["sleep"] | false;

  prefs.putUChar("mood", pet.mood); prefs.putUChar("health", pet.health);
  prefs.putUChar("level", pet.level);
  prefs.putUChar("eyes", pet.eyes); prefs.putUChar("mouth", pet.mouth);
  prefs.putUInt("seq", pet.celebrateSeq);
  Serial.printf("fetch ok: mood=%u health=%u level=%u eyes=%u mouth=%u seq=%lu\n", (unsigned)pet.mood,
                (unsigned)pet.health, (unsigned)pet.level, (unsigned)pet.eyes, (unsigned)pet.mouth,
                (unsigned long)pet.celebrateSeq);
  return true;
}

static void netTask(void*) {
  uint32_t last = 0;
  bool first = true;
  for (;;) {
    uint32_t now = millis();
    if (first || pet.syncRequested || now - last > POLL_MS) {
      first = false;
      pet.syncRequested = false;
      last = now;
      if (!fetchState()) last = now + 15000 - POLL_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ============================ SETUP PORTAL ============================
static const char FORM[] PROGMEM =
  "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
  "<body style='font-family:sans-serif;max-width:360px;margin:24px auto'><h2>Codogotchi setup</h2>"
  "<form method=POST action=/save>WiFi name<br><input name=ssid style='width:100%'><br><br>"
  "Password<br><input name=pass type=password style='width:100%'><br><br>"
  "Device token (from the website)<br><input name=token style='width:100%'><br><br><button>Save</button></form>";

static void portalScreen(const String& ap) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);  display.print("SETUP MODE");
  display.setCursor(0, 16); display.print("join WiFi:");
  display.setCursor(0, 26); display.print(ap);
  display.setCursor(0, 42); display.print("then open");
  display.setCursor(0, 52); display.print("192.168.4.1");
  display.display();
}

static void runSetupPortal() {
  char name[24];
  snprintf(name, sizeof(name), "Codogotchi-%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(name);
  WebServer srv(80);
  srv.on("/", [&]() { srv.send_P(200, "text/html", FORM); });
  srv.on("/save", HTTP_POST, [&]() {
    prefs.putString("ssid", srv.arg("ssid"));
    prefs.putString("pass", srv.arg("pass"));
    prefs.putString("token", srv.arg("token"));
    srv.send(200, "text/html", "<body style='font-family:sans-serif'>Saved! Restarting...");
    delay(800);
    ESP.restart();
  });
  srv.begin();
  portalScreen(String(name));
  for (;;) { srv.handleClient(); delay(5); }
}

static bool setupWindowHeld() {
  uint32_t t0 = millis(), heldSince = 0;
  while (millis() - t0 < 5000) {   // 5 s window: start touching once the text appears, hold ~1.5 s
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(14, 28);
    display.print("hold strip = setup");
    display.display();
    bool any = false;
    for (uint8_t i = 0; i < NSEG; i++) if (segTouched(segs[i])) any = true;
    if (any) { if (!heldSince) heldSince = millis(); if (millis() - heldSince > 1200) return true; }
    else heldSince = 0;
    delay(20);
  }
  return false;
}

// ============================ BEHAVIOUR ============================
static uint32_t wakeUntil = 0, nextIdleChirp = 0, lastPurr = 0;

static void onGesture(Gesture g, uint32_t now) {
  if (g == G_NONE) return;
  if (sleeping) {
    wakeUntil = now + 60000;
    playSound(SND_WAKE);
    startRx(RX_POKE, 500);
    return;
  }
  switch (g) {
    case G_TAP1:  startRx(RX_POKE, 450); playSound(SND_POKE); break;
    case G_TAP2:  startRx(RX_STATUS, 3400); playSound(SND_SYNC); break;
    case G_TAP3:  muted = !muted; prefs.putBool("mute", muted);
                  playSound(muted ? SND_MUTE_ON : SND_MUTE_OFF, true); startRx(RX_POKE, 400); break;
    case G_SPAM:  startRx(RX_ANNOYED, 2200); playSound(SND_ANNOYED); break;
    case G_PET_START: petting = true; startRx(RX_PET, 0x7FFFFFFF); break;
    case G_PET_END:   petting = false;
                      if (rx == RX_PET) rxDur = (now - rxStart) + 500; break;
    case G_SWIPE_L: case G_SWIPE_R:
      gazeForceX = (g == G_SWIPE_R) ? 1.f : -1.f; gazeForceUntil = now + 900;
      startRx(RX_PET, 1600); playSound(SND_HAPPY); break;
    case G_SYNC_HOLD: pet.syncRequested = true; startRx(RX_SYNC, 2500); playSound(SND_SYNC, true); break;
    default: break;
  }
}

static void housekeeping(uint32_t now) {
  static uint8_t prevMood = 255;
  static bool prevSleeping = false;

  sleeping = pet.sleepNow && now > wakeUntil;
  if (sleeping != prevSleeping) { display.setContrast(sleeping ? 0 : 0x80); prevSleeping = sleeping; }

  if (pet.cosmeticChanged) {                          // accessory equipped/unequipped on the website
    pet.cosmeticChanged = false;
    if (!sleeping) { startRx(RX_POKE, 450); playSound(SND_HAPPY); }
  }

  if (!sleeping && pet.levelUpPending) {              // level-up: longer celebration + show the number
    pet.levelUpPending = false; pet.celebratePending = false;
    levelShowUntil = now + 4000;
    startRx(RX_CELEBRATE, 4000); playSound(SND_CELEBRATE);
  } else if (!sleeping && pet.celebratePending) {
    pet.celebratePending = false;
    startRx(RX_CELEBRATE, 2800); playSound(SND_CELEBRATE);
  }

  if (prevMood != 255 && pet.mood != prevMood && !sleeping) playSound(pet.mood > prevMood ? SND_HAPPY : SND_SAD);
  prevMood = pet.mood;

  if (!sleeping && !petting && now > nextIdleChirp) {
    if (nextIdleChirp) playSound(pet.mood == 0 ? SND_SAD : SND_IDLE);
    nextIdleChirp = now + random(20000, 50000);
  }
  if (petting && now - lastPurr > 650) { playSound(SND_PURR); lastPurr = now; }
}

static void handleSerial() {
  static String buf;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') { buf += ch; continue; }
    buf.trim();
    if (!buf.length()) continue;

    // Numeric input -> equip that accessory in its slot (1..36), 0 = clear both slots
    if (isdigit(buf[0])) {
      int v = buf.toInt();
      uint8_t oldEyes = pet.eyes, oldMouth = pet.mouth;
      if (v == 0) { pet.eyes = 0; pet.mouth = 0; }
      else if (v <= 36 && accessorySlot((uint8_t)v) == 0) pet.eyes = v;
      else if (v <= 36 && accessorySlot((uint8_t)v) == 1) pet.mouth = v;
      if (pet.eyes != oldEyes || pet.mouth != oldMouth) pet.cosmeticChanged = true;   // play the equip reaction
      prefs.putUChar("eyes", pet.eyes); prefs.putUChar("mouth", pet.mouth);
      pet.demoUntil = millis() + 10UL * 60000UL;
      Serial.printf("eyes = %u, mouth = %u\n", (unsigned)pet.eyes, (unsigned)pet.mouth);
      buf = ""; continue;
    }

    char c = buf[0];
    int v = buf.substring(1).toInt();
    pet.demoUntil = millis() + 10UL * 60000UL;
    switch (c) {
      case 'm': pet.mood = constrain(v, 0, 3); break;
      case 'h': pet.health = constrain(v, 0, 100); break;
      case 'l': pet.level = constrain(v, 0, 50); pet.levelUpPending = true; break;
      case 'c': pet.celebratePending = true; break;
      case 'z': pet.sleepNow = !pet.sleepNow; break;
      case 't': dbgTouch = !dbgTouch; break;
    }
    buf = "";
  }
}

// ============================ ARDUINO ============================
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  prefs.begin("codo", false);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  if (!display.begin(OLED_ADDR, true)) { Serial.println("OLED init failed - check wiring (SDA=21, SCL=22) and the I2C address (0x3C or 0x3D)"); for (;;) delay(1000); }
  display.setTextColor(SH110X_WHITE);
  display.clearDisplay(); display.display();

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BUZZER, 2000, 8);
#else
  ledcSetup(0, 2000, 8); ledcAttachPin(PIN_BUZZER, 0);
#endif
  sndQ = xQueueCreate(6, sizeof(uint8_t));
  xTaskCreatePinnedToCore(soundTask, "snd", 2048, NULL, 1, NULL, 0);

  pet.mood = prefs.getUChar("mood", 1);
  pet.health = prefs.getUChar("health", 60);
  pet.level = prefs.getUChar("level", 0);
  pet.eyes  = prefs.getUChar("eyes", 0);
  pet.mouth = prefs.getUChar("mouth", 0);
  pet.celebrateSeq = prefs.getUInt("seq", 0);
  muted = prefs.getBool("mute", false);

  touchCalibrate();
  if (!prefs.getString("ssid", "").length() || setupWindowHeld()) runSetupPortal();

  xTaskCreatePinnedToCore(netTask, "net", 16384, NULL, 1, NULL, 0);   // TLS needs a big stack
  playSound(SND_WAKE);
}

void loop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 33) { delay(1); return; }
  float dt = (now - last) / 1000.f;
  if (dt > 0.1f) dt = 0.1f;
  last = now;

  handleSerial();
  onGesture(gestureUpdate(now), now);
  housekeeping(now);
  computeTargets(now);
  smoothFace(cur, tgt, 1.f - expf(-dt / 0.10f));
  drawFrame(now);
}
