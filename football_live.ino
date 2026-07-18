/*
  LIVE FOOTBALL SCOREBOARD - ESP32 Dev Module + 0.96" SSD1306 OLED (128x64, I2C)
  ------------------------------------------------------------------
  What it does:
    - Connects to WiFi
    - Polls football-data.org for any currently LIVE match
    - Shows "TeamA vs TeamB", the score, and a real-time HH:MM:SS clock
      that keeps ticking smoothly between API polls (synced to the
      match minute every poll, not just when a new response arrives)
    - When a goal is detected, flashes "GOAL!" then shows who scored
      and in which minute, then returns to the normal live view
      while the clock keeps counting

  SETUP STEPS:
    1) Get a FREE API key: https://www.football-data.org/client/register
    2) Fill in WIFI_SSID, WIFI_PASSWORD, API_TOKEN below.
    3) In ArduinoDroid Library Manager, install:
         - ArduinoJson (by Benoit Blanchon, v6.x or v7.x)
         - Adafruit SSD1306
         - Adafruit GFX Library
    4) Wire the OLED: VCC->3.3V, GND->GND, SDA->GPIO21, SCL->GPIO22
       (these are the standard default I2C pins on a regular ESP32
       Dev Module - change OLED_SDA/OLED_SCL below if wired differently)
    5) Board setting in ArduinoDroid: "ESP32 Dev Module"

  NOTE ON FREE TIER: football-data.org free plan only covers 12 top
  competitions, and only shows matches that are actually live right
  now. If nothing's playing, the screen will say "No live match".
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- USER CONFIG ----------------
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* API_TOKEN     = "YOUR_FOOTBALL_DATA_ORG_TOKEN";

#define OLED_SDA 21
#define OLED_SCL 22
// ----------------------------------------------

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Poll every 20s -> 3 requests/min, well under the 10/min free-tier limit
const unsigned long POLL_INTERVAL_MS = 20000;
const unsigned long GOAL_DISPLAY_MS  = 6000;

// ---------------- Match state ----------------
long   currentMatchId   = -1;
String homeName         = "";
String awayName         = "";
int    homeScore        = 0;
int    awayScore        = 0;
String matchStatus      = "";     // LIVE, PAUSED, FINISHED...
int    apiMinute        = 0;      // last minute reported by API

// Local smooth clock, synced to apiMinute on every successful poll
unsigned long clockBaseSeconds = 0;
unsigned long clockSyncMillis  = 0;

// Goal flash state
bool   showingGoal      = false;
unsigned long goalUntil = 0;
String goalScorer       = "";
String goalTeam         = "";
int    goalMinute       = 0;

unsigned long lastPollMillis = 0;
bool haveMatch = false;

// ---------------- WiFi / HTTP helpers ----------------
bool httpGetJson(const String& url, JsonDocument& filter, JsonDocument& out) {
  WiFiClientSecure client;
client.setInsecure();

HTTPClient http;

http.setReuse(false);
http.begin(url);
  http.addHeader("X-Auth-Token", API_TOKEN);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    DeserializationError err = deserializeJson(out, http.getStream(), DeserializationOption::Filter(filter));
    ok = !err;
  }
  http.end();
  return ok;
}

// ---------------- Data fetch ----------------
bool fetchLiveMatch() {
  DynamicJsonDocument filter(512);
  filter["matches"][0]["id"] = true;
  filter["matches"][0]["minute"] = true;
  filter["matches"][0]["status"] = true;
  filter["matches"][0]["homeTeam"]["shortName"] = true;
  filter["matches"][0]["homeTeam"]["tla"] = true;
  filter["matches"][0]["awayTeam"]["shortName"] = true;
  filter["matches"][0]["awayTeam"]["tla"] = true;
  filter["matches"][0]["score"]["fullTime"]["home"] = true;
  filter["matches"][0]["score"]["fullTime"]["away"] = true;

  DynamicJsonDocument doc(8192);
  if (!httpGetJson("https://api.football-data.org/v4/matches?status=LIVE", filter, doc)) return false;

  JsonArray matches = doc["matches"].as<JsonArray>();
  if (matches.isNull() || matches.size() == 0) return false;

  JsonObject m = matches[0];
  long id = m["id"] | -1;
  int newHome = m["score"]["fullTime"]["home"] | 0;
  int newAway = m["score"]["fullTime"]["away"] | 0;
  int minute  = m["minute"] | apiMinute;
  const char* status = m["status"] | "LIVE";
  const char* hName = m["homeTeam"]["tla"] | (const char*)m["homeTeam"]["shortName"];
  const char* aName = m["awayTeam"]["tla"] | (const char*)m["awayTeam"]["shortName"];

  bool isNewMatch = (id != currentMatchId);

  // Detect goal only if it's the same match we were already tracking
  if (!isNewMatch && haveMatch && (newHome + newAway) > (homeScore + awayScore)) {
    checkForGoalScorer(id, newHome > homeScore ? hName : aName);
  }

  currentMatchId = id;
  homeName = hName ? hName : "HOME";
  awayName = aName ? aName : "AWAY";
  homeScore = newHome;
  awayScore = newAway;
  matchStatus = status;
  apiMinute = minute;
  haveMatch = true;

  // Re-sync smooth clock to the authoritative API minute
  clockBaseSeconds = (unsigned long)minute * 60UL;
  clockSyncMillis = millis();

  return true;
}

// Fetch match detail to find who scored the newest goal
void checkForGoalScorer(long matchId, const char* scoringTeamGuess) {
  DynamicJsonDocument filter(512);
  filter["goals"][0]["minute"] = true;
  filter["goals"][0]["team"]["name"] = true;
  filter["goals"][0]["scorer"]["name"] = true;

  DynamicJsonDocument doc(4096);
  String url = "https://api.football-data.org/v4/matches/" + String(matchId);
  if (!httpGetJson(url, filter, doc)) {
    goalScorer = "Goal!";
    goalTeam = scoringTeamGuess ? scoringTeamGuess : "";
    goalMinute = apiMinute;
  } else {
    JsonArray goals = doc["goals"].as<JsonArray>();
    if (goals.size() > 0) {
      JsonObject last = goals[goals.size() - 1];
      const char* scorer = last["scorer"]["name"] | "Unknown";
      const char* team = last["team"]["name"] | "";
      goalScorer = scorer;
      goalTeam = team;
      goalMinute = last["minute"] | apiMinute;
    }
  }
  showingGoal = true;
  goalUntil = millis() + GOAL_DISPLAY_MS;
}

// ---------------- Display ----------------
void formatClock(unsigned long totalSeconds, char* buf) {
  unsigned long h = totalSeconds / 3600;
  unsigned long m = (totalSeconds % 3600) / 60;
  unsigned long s = totalSeconds % 60;
  sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
}

void drawNoMatch() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.print("Waiting for a");
  display.setCursor(10, 32);
  display.print("live match...");
  display.display();
}

void drawGoalScreen() {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  // Flash effect: only draw GOAL text every other 400ms
  if ((millis() / 400) % 2 == 0) {
    display.setCursor(8, 4);
    display.print("GOAL!");
  }
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.print(goalTeam);
  display.setCursor(0, 50);
  display.print(goalScorer);
  display.print("  ");
  display.print(goalMinute);
  display.print("'");
  display.display();
}

void drawLiveScreen() {
  unsigned long liveSeconds = clockBaseSeconds + (millis() - clockSyncMillis) / 1000;
  char clockBuf[10];
  formatClock(liveSeconds, clockBuf);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Teams
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(homeName);
  display.print(" vs ");
  display.print(awayName);

  // Score, big
  display.setTextSize(3);
  char scoreBuf[8];
  sprintf(scoreBuf, "%d - %d", homeScore, awayScore);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(scoreBuf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 18);
  display.print(scoreBuf);

  // Clock
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print(clockBuf);

  // Status
  display.setCursor(80, 48);
  display.print(matchStatus);

  display.display();
}

// ---------------- Setup / Loop ----------------
void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
  }

  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("WiFi connected!");
  display.display();
  delay(800);

  fetchLiveMatch();
  lastPollMillis = millis();
}

void loop() {
  // Poll periodically
  if (millis() - lastPollMillis >= POLL_INTERVAL_MS) {
    lastPollMillis = millis();
    if (WiFi.status() == WL_CONNECTED) {
      fetchLiveMatch();
    }
  }

  // Render
  if (showingGoal) {
    if (millis() > goalUntil) {
      showingGoal = false;
    } else {
      drawGoalScreen();
      delay(80);
      return;
    }
  }

  if (haveMatch) {
    drawLiveScreen();
  } else {
    drawNoMatch();
  }

  delay(200); // smooth clock updates without hammering the CPU
}