#include "net.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include <atomic>
#include <string>
#include <vector>

#include "config.h"
#include "upload_text.h"
#include "wifi_text.h"

namespace net {

static const char* kNamespace = "wifi";
static const uint32_t kJoinMs = 25000;        // time allowed for one connection attempt
static const uint32_t kRetryMs = 30000;       // pause between attempts when offline
static const uint32_t kPortalMs = 600000;     // the hotspot closes itself after 10 minutes
static const uint32_t kReplyMs = 1500;        // lets the "saved" page reach the phone
static const int kMaxNetworks = 12;
static const IPAddress kPortalIp(192, 168, 4, 1);

enum class Request : uint8_t { None, Start, Stop };
static std::atomic<Request> s_request{Request::None};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Status s_status;
static Server s_serverConfig;

// Everything below is used only by the network task.
static std::string s_ssid, s_password;        // saved network
static std::string s_newSsid, s_newPassword;  // entered on the setup page; empty SSID = keep the saved network
static std::string s_name;                    // saved thermostat name
static bool s_submitted = false;
static uint32_t s_submittedMs = 0;
static uint32_t s_sinceMs = 0;                // start of the current attempt, pause or hotspot
static bool s_timeStarted = false;
static bool s_routesAdded = false;
static std::vector<std::string> s_networks;
static WebServer s_server(80);
static DNSServer s_dns;

Status status() {
  portENTER_CRITICAL(&s_mux);
  const Status copy = s_status;
  portEXIT_CRITICAL(&s_mux);
  return copy;
}

static State state() { return status().state; }

static void setState(State st, const std::string& ssid) {
  portENTER_CRITICAL(&s_mux);
  s_status.state = st;
  strlcpy(s_status.ssid, ssid.c_str(), sizeof(s_status.ssid));
  s_status.ip[0] = '\0';
  portEXIT_CRITICAL(&s_mux);
  s_sinceMs = millis();
}

static void setFailed(bool failed) {
  portENTER_CRITICAL(&s_mux);
  s_status.setupFailed = failed;
  portEXIT_CRITICAL(&s_mux);
}

static void setHotspot(const char* name, const char* password) {
  portENTER_CRITICAL(&s_mux);
  strlcpy(s_status.apName, name, sizeof(s_status.apName));
  strlcpy(s_status.apPassword, password, sizeof(s_status.apPassword));
  portEXIT_CRITICAL(&s_mux);
}

void startSetup() { s_request = Request::Start; }
void stopSetup() { s_request = Request::Stop; }

// ---- Saved network ----

// Name and password are one record, so a power cut can never save only one of them.
struct Saved {
  char ssid[33];
  char password[64];
};

static void loadNetwork() {
  Preferences p;
  if (!p.begin(kNamespace, true)) return;  // nothing saved yet
  Saved rec = {};
  const bool ok = p.getBytes("network", &rec, sizeof(rec)) == sizeof(rec);
  p.end();
  if (!ok) return;
  rec.ssid[sizeof(rec.ssid) - 1] = '\0';
  rec.password[sizeof(rec.password) - 1] = '\0';
  s_ssid = rec.ssid;
  s_password = rec.password;
  if (!thermo::validWifi(s_ssid, s_password)) {
    s_ssid.clear();
    s_password.clear();
  }
}

static bool saveNetwork() {
  Preferences p;
  if (!p.begin(kNamespace, false)) return false;
  Saved rec = {};
  strlcpy(rec.ssid, s_newSsid.c_str(), sizeof(rec.ssid));
  strlcpy(rec.password, s_newPassword.c_str(), sizeof(rec.password));
  const bool ok = p.putBytes("network", &rec, sizeof(rec)) == sizeof(rec);
  p.end();
  return ok;
}

// ---- Thermostat name ----

static void setName(const std::string& name) {
  s_name = name;
  portENTER_CRITICAL(&s_mux);
  strlcpy(s_status.name, name.c_str(), sizeof(s_status.name));
  portEXIT_CRITICAL(&s_mux);
}

static void loadName() {
  Preferences p;
  if (!p.begin(kNamespace, true)) return;  // nothing saved yet
  std::string name;
  if (p.isKey("name")) name = p.getString("name").c_str();
  p.end();
  if (thermo::cleanName(name)) setName(name);
}

static bool saveName(const std::string& name) {
  if (name == s_name) return true;
  Preferences p;
  if (!p.begin(kNamespace, false)) return false;
  const bool ok = name.empty() ? p.remove("name") : p.putString("name", name.c_str()) == name.size();
  p.end();
  if (ok) setName(name);
  return ok;
}

static char s_wantedName[sizeof(Status::name)] = "";  // guarded by s_mux
static bool s_nameWanted = false;                     // guarded by s_mux

void requestName(const char* name) {
  portENTER_CRITICAL(&s_mux);
  strlcpy(s_wantedName, name, sizeof(s_wantedName));
  s_nameWanted = true;
  portEXIT_CRITICAL(&s_mux);
}

static void takeName() {
  char name[sizeof(s_wantedName)];
  portENTER_CRITICAL(&s_mux);
  const bool wanted = s_nameWanted;
  s_nameWanted = false;
  memcpy(name, s_wantedName, sizeof(name));
  portEXIT_CRITICAL(&s_mux);
  if (wanted && !saveName(name)) Serial.println("Name save FAILED");
}

// ---- Server ----

static_assert(thermo::kMaxServer < sizeof(Server::address) && thermo::kMaxToken < sizeof(Server::token), "");

Server server() {
  portENTER_CRITICAL(&s_mux);
  const Server copy = s_serverConfig;
  portEXIT_CRITICAL(&s_mux);
  return copy;
}

static void setServer(const Server& s) {
  portENTER_CRITICAL(&s_mux);
  s_serverConfig = s;
  portEXIT_CRITICAL(&s_mux);
}

static void loadServer() {
  Preferences p;
  if (!p.begin(kNamespace, true)) return;  // nothing saved yet
  Server s;
  const bool ok = p.isKey("server") && p.getBytes("server", &s, sizeof(s)) == sizeof(s);
  p.end();
  if (!ok) return;
  s.address[sizeof(s.address) - 1] = '\0';
  s.token[sizeof(s.token) - 1] = '\0';
  std::string address = s.address;
  if (thermo::cleanServer(address) && thermo::validToken(s.token)) setServer(s);
}

static bool saveServer(const std::string& address, const std::string& token) {
  Server s;
  if (!address.empty()) {  // no server: the token goes too
    strlcpy(s.address, address.c_str(), sizeof(s.address));
    strlcpy(s.token, token.c_str(), sizeof(s.token));
  }
  const Server old = server();
  if (strcmp(s.address, old.address) == 0 && strcmp(s.token, old.token) == 0) return true;
  Preferences p;
  if (!p.begin(kNamespace, false)) return false;
  const bool ok = p.putBytes("server", &s, sizeof(s)) == sizeof(s);
  p.end();
  if (ok) setServer(s);
  return ok;
}

// ---- Connecting ----

static void join(State st, const std::string& ssid, const std::string& password) {
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.empty() ? nullptr : password.c_str());
  setState(st, ssid);
}

static void joinSaved() {
  if (s_ssid.empty()) {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    setState(State::NotSetUp, "");
  } else {
    join(State::Connecting, s_ssid, s_password);
  }
}

static void connected(const std::string& ssid) {
  const String ip = WiFi.localIP().toString();
  setState(State::Connected, ssid);
  portENTER_CRITICAL(&s_mux);
  strlcpy(s_status.ip, ip.c_str(), sizeof(s_status.ip));
  portEXIT_CRITICAL(&s_mux);
  Serial.printf("Wi-Fi connected, address %s\n", ip.c_str());
  if (!s_timeStarted) {
    s_timeStarted = true;  // keeps itself in step from here on
    configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  }
}

// ---- Setup page ----

static void sendPage(int code, const std::string& body) {
  std::string page =
      "<!doctype html><html lang=en><meta charset=utf-8>"
      "<meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>Thermostat Wi-Fi</title><style>"
      "body{font:17px sans-serif;max-width:26em;margin:0 auto;padding:16px}"
      "label{display:block;margin:16px 0 4px}"
      "input,select,button{font:inherit;width:100%;padding:10px;box-sizing:border-box}"
      "button{margin-top:24px}</style><h1>Thermostat Wi-Fi</h1>";
  page += body;
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(code, "text/html; charset=utf-8", page.c_str());
}

static void sendForm(int code, const char* error, const std::string& name, const std::string& address) {
  std::string b;
  if (error[0] != '\0') b += std::string("<p><b>") + error + "</b></p>";
  b += "<form method=post action=/save><label for=t>Thermostat name (optional)</label>"
       "<input id=t name=name maxlength=" + std::to_string(thermo::kMaxName) +
       " autocomplete=off value=\"" + thermo::htmlEscape(name) + "\">"
       "<label for=n>Network</label><select id=n name=net>";
  if (!s_ssid.empty()) b += "<option value=keep>Keep " + thermo::htmlEscape(s_ssid) + "</option>";
  // Listed networks are sent by number, so no network name can be mistaken for "keep" or "other".
  for (size_t i = 0; i < s_networks.size(); ++i) {
    b += "<option value=" + std::to_string(i) + ">" + thermo::htmlEscape(s_networks[i]) + "</option>";
  }
  b += "<option value=other>Other (type the name below)</option></select>"
       "<label for=o>Other network name</label>"
       "<input id=o name=other maxlength=32 autocapitalize=none autocomplete=off>"
       "<label for=p>Password</label>"
       "<input id=p name=pass type=password maxlength=63 autocomplete=off>"
       "<label for=s>Server address (optional)</label>"
       "<input id=s name=server maxlength=80 autocapitalize=none autocomplete=off "
       "placeholder='192.168.1.10:5080' value=\"" + thermo::htmlEscape(address) + "\">"
       "<label for=k>Server token" +
       (server().token[0] != '\0' ? " (empty keeps the saved one)" : "") + "</label>"
       "<input id=k name=token type=password maxlength=64 autocomplete=off>"
       "<button>Save and connect</button></form>"
       "<p>The thermostat uses 2.4 GHz networks only.</p>";
  sendPage(code, b);
}

static void handleSave() {
  std::string name = s_server.arg("name").c_str();
  std::string address = s_server.arg("server").c_str();
  std::string token = s_server.arg("token").c_str();
  if (!thermo::cleanName(name)) {
    sendForm(400, "The thermostat name can have up to 16 plain letters, numbers and symbols.", name, address);
    return;
  }
  if (!thermo::cleanServer(address)) {
    sendForm(400, "The server address should look like 192.168.1.10:5080.", name, address);
    return;
  }
  if (token.empty()) token = server().token;
  if (!address.empty() && !thermo::validToken(token)) {
    sendForm(400, "Enter the server token: 16 to 64 letters and digits.", name, address);
    return;
  }

  const String choice = s_server.arg("net");
  std::string ssid = s_server.arg("other").c_str();
  const std::string password = s_server.arg("pass").c_str();
  const bool keep = ssid.empty() && choice == "keep" && !s_ssid.empty();
  if (keep && !password.empty()) {
    sendForm(400, "To enter a new password, choose the network from the list.", name, address);
    return;
  }
  if (ssid.empty() && choice.length() > 0 && isDigit(choice[0])) {
    const size_t i = static_cast<size_t>(choice.toInt());
    if (i < s_networks.size()) ssid = s_networks[i];
  }
  if (!keep && !thermo::validWifi(ssid, password)) {
    sendForm(400, "Choose a network. The password must be 8 to 63 characters, or empty for an open network.",
             name, address);
    return;
  }
  if (!saveName(name) || !saveServer(address, token)) {
    Serial.println("Name or server save FAILED");
    sendForm(500, "The name or server could not be saved. Please try again.", name, address);
    return;
  }
  if (keep) ssid.clear();
  s_newSsid = ssid;
  s_newPassword = password;
  s_submitted = true;
  s_submittedMs = millis();
  sendPage(200, "<p>Connecting to <b>" + thermo::htmlEscape(keep ? s_ssid : ssid) +
                    "</b>.</p><p>This hotspot now closes. Check the thermostat screen for the result.</p>");
}

static void addRoutes() {
  if (s_routesAdded) return;
  s_routesAdded = true;
  s_server.on("/", HTTP_GET, [] { sendForm(200, "", s_name, server().address); });
  s_server.on("/save", HTTP_POST, handleSave);
  s_server.onNotFound([] {  // sends phones that are checking for internet to the page
    s_server.sendHeader("Location", "http://192.168.4.1/");
    s_server.send(302, "text/plain", "");
  });
}

// ---- Hotspot ----

static void scan() {
  s_networks.clear();
  const int n = WiFi.scanNetworks();
  for (int i = 0; i < n && static_cast<int>(s_networks.size()) < kMaxNetworks; ++i) {
    const std::string name = WiFi.SSID(i).c_str();
    bool seen = name.empty();
    for (const std::string& k : s_networks) seen = seen || k == name;
    if (!seen) s_networks.push_back(name);
  }
  WiFi.scanDelete();
}

static void closePortal() {
  s_dns.stop();
  s_server.stop();
  WiFi.softAPdisconnect(true);
  setHotspot("", "");
  s_submitted = false;
}

static void openPortal() {
  setFailed(false);
  setState(State::Starting, s_ssid);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  scan();

  // A new password every time, shown only on the thermostat's own screen.
  static const char kChars[] = "abcdefghjkmnpqrstuvwxyz23456789";  // no look-alikes
  char password[11];
  for (int i = 0; i < 10; ++i) password[i] = kChars[esp_random() % (sizeof(kChars) - 1)];
  password[10] = '\0';
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[24];
  snprintf(name, sizeof(name), "Thermostat-%02X%02X", mac[4], mac[5]);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kPortalIp, kPortalIp, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(name, password, 1, 0, 2)) {
    Serial.println("Hotspot FAILED to start");
    closePortal();
    setFailed(true);
    joinSaved();
    return;
  }
  addRoutes();
  s_dns.start(53, "*", kPortalIp);
  s_server.begin();
  s_submitted = false;
  setHotspot(name, password);
  setState(State::Hotspot, s_ssid);
  Serial.println("Setup hotspot open");
}

// ---- Task ----

static void step() {
  takeName();
  const Request req = s_request.exchange(Request::None);
  const State st = state();

  if (req == Request::Start && st != State::Hotspot) {
    openPortal();
    return;
  }
  if (req == Request::Stop && (st == State::Hotspot || st == State::Trying)) {
    closePortal();
    joinSaved();
    return;
  }

  const uint32_t elapsed = millis() - s_sinceMs;
  const bool up = WiFi.status() == WL_CONNECTED;

  switch (st) {
    case State::Hotspot:
      s_dns.processNextRequest();
      s_server.handleClient();
      if (s_submitted && millis() - s_submittedMs >= kReplyMs) {
        closePortal();
        if (s_newSsid.empty()) joinSaved();
        else join(State::Trying, s_newSsid, s_newPassword);
      } else if (!s_submitted && millis() - s_sinceMs >= kPortalMs) {
        Serial.println("Setup hotspot timed out");
        closePortal();
        joinSaved();
      }
      break;

    case State::Trying:  // the saved network is replaced only once the new one works
      if (up) {
        if (!saveNetwork()) Serial.println("Wi-Fi save FAILED");
        s_ssid = s_newSsid;
        s_password = s_newPassword;
        connected(s_ssid);
      } else if (elapsed >= kJoinMs) {
        Serial.println("Wi-Fi setup: could not join the network");
        setFailed(true);
        joinSaved();
      }
      break;

    case State::Connecting:
      if (up) {
        connected(s_ssid);
      } else if (elapsed >= kJoinMs) {
        setState(State::Offline, s_ssid);
      }
      break;

    case State::Connected:
      if (!up) {
        Serial.println("Wi-Fi lost");
        join(State::Connecting, s_ssid, s_password);
      }
      break;

    case State::Offline:
      if (up) connected(s_ssid);
      else if (elapsed >= kRetryMs) join(State::Connecting, s_ssid, s_password);
      break;

    case State::NotSetUp:
    case State::Starting:
      break;
  }
}

static void task(void*) {
  WiFi.persistent(false);  // the network is saved here, not by the Wi-Fi library
  WiFi.setHostname("thermostat");
  loadNetwork();
  joinSaved();
  for (;;) {
    step();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void begin() {
  loadServer();
  loadName();  // before the task starts, so Home shows the name from its first refresh
  // Core 0; loop(), with heating control, runs on core 1.
  if (xTaskCreatePinnedToCore(task, "net", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("Network task FAILED to start");
  }
}

}  // namespace net
