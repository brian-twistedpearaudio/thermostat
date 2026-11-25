#include "uplink.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <esp_mac.h>
#include <esp_partition.h>
#include <time.h>

#include <atomic>
#include <string>
#include <vector>

#include "log_cache.h"
#include "net.h"
#include "upload_text.h"

namespace uplink {

static const uint32_t kSectors = 256;     // 1 MB: 65,280 records, about 226 days
static const uint32_t kRetryMs = 15000;   // pause after a failed attempt
static const uint32_t kSyncMs = 10000;
static const uint32_t kWifiNoticeMs = 40000;
static const size_t kBatch = 24;
static const int kQueueLength = 8;

enum class Link : uint8_t { Off, Waiting, Ok, NoAnswer, BadToken, OldServer };  // set by the sync
static std::atomic<Link> s_link{Link::Off};
static std::atomic<uint32_t> s_pending{0};
static QueueHandle_t s_queue = nullptr;

class PartitionFlash : public thermo::LogFlash {
 public:
  const esp_partition_t* part = nullptr;
  bool read(uint32_t addr, void* buf, size_t n) override {
    return esp_partition_read(part, addr, buf, n) == ESP_OK;
  }
  bool write(uint32_t addr, const void* buf, size_t n) override {
    return esp_partition_write(part, addr, buf, n) == ESP_OK;
  }
  bool erase(uint32_t addr) override {
    return esp_partition_erase_range(part, addr, thermo::LogCache::kSector) == ESP_OK;
  }
};

// Everything below is used only by the uplink task.
static PartitionFlash s_flash;
static thermo::LogCache s_cache;
static std::string s_device;

static void setLink(Link link) {
  if (s_link.exchange(link) == link) return;
  if (link == Link::Ok) Serial.println("Server connected");
  if (link == Link::NoAnswer) Serial.println("Server not answering");
  if (link == Link::BadToken) Serial.println("Server rejected the token");
  if (link == Link::OldServer) Serial.println("Server needs updating");
}

// True once the server has the records.
static bool send(const net::Server& server, const std::vector<thermo::LogRecord>& records) {
  const net::Status w = net::status();
  if (w.state != net::State::Connected) return false;

  const std::string address = server.address;
  const size_t colon = address.find(':');
  if (colon == std::string::npos) return false;
  const std::string body = thermo::uploadJson(s_device, w.name, records);

  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  int code = -1;
  bool understood = false;
  if (http.begin(client, address.substr(0, colon).c_str(),
                 static_cast<uint16_t>(atoi(address.c_str() + colon + 1)), "/api/v1/readings")) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Api-Token", server.token);
    code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
    // Only an answer from the thermostat server counts: another program could also answer 200.
    if (code == 200 && http.getSize() <= 512) understood = http.getString().indexOf("\"added\":") >= 0;
    http.end();
  }

  if (code > 0 && code != 401 && !understood) Serial.printf("Server answered %d\n", code);
  return understood;
}

// ---- Configuration sync ----

static SemaphoreHandle_t s_syncLock = nullptr;
static SyncRequest s_offer;    // guarded by s_syncLock
static bool s_offered = false;  // guarded by s_syncLock
static SyncReply s_reply;      // guarded by s_syncLock
static std::atomic<bool> s_replyReady{false};
static std::atomic<bool> s_syncSoon{false};

void offerSync(const SyncRequest& r, bool soon) {
  if (!s_syncLock) return;
  xSemaphoreTake(s_syncLock, portMAX_DELAY);  // only ever held for a short copy
  s_offer = r;
  s_offered = true;
  xSemaphoreGive(s_syncLock);
  if (soon) s_syncSoon = true;
}

bool takeSync(SyncReply& out) {
  if (!s_replyReady) return false;
  xSemaphoreTake(s_syncLock, portMAX_DELAY);
  s_replyReady = false;
  out = s_reply;
  xSemaphoreGive(s_syncLock);
  return true;
}

// Also the check that the server is there. False if there was nothing to send yet.
static bool sync(const net::Server& server) {
  SyncRequest r;
  xSemaphoreTake(s_syncLock, portMAX_DELAY);
  const bool offered = s_offered;
  if (offered) r = s_offer;
  xSemaphoreGive(s_syncLock);
  const std::string address = server.address;
  const size_t colon = address.find(':');
  if (!offered || colon == std::string::npos) return false;

  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  Link link = Link::NoAnswer;
  int code = -1;
  const std::string path = "/api/v1/devices/" + s_device + "/sync";
  if (http.begin(client, address.substr(0, colon).c_str(),
                 static_cast<uint16_t>(atoi(address.c_str() + colon + 1)), path.c_str())) {
    http.addHeader("Content-Type", "text/plain");
    http.addHeader("X-Api-Token", server.token);
    code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(r.text.data())), r.text.size());
    if (code == 401) link = Link::BadToken;
    if (code == 404 || code == 405) link = Link::OldServer;
    if (code == 200 && http.getSize() > 0 && http.getSize() <= 1024) {
      SyncReply reply;
      if (thermo::parseSyncReply(http.getString().c_str(), reply.fields)) {
        link = Link::Ok;
        memcpy(reply.changes, r.changes, sizeof(reply.changes));
        xSemaphoreTake(s_syncLock, portMAX_DELAY);
        s_reply = reply;
        s_replyReady = true;
        xSemaphoreGive(s_syncLock);
      }
    }
    http.end();
  }
  if (link == Link::NoAnswer && code > 0) Serial.printf("Server answered %d to the sync\n", code);
  setLink(link);
  return true;
}

static void keep(const thermo::LogRecord& rec) {
  if (!s_cache.append(rec)) Serial.println("Record cache write FAILED");
}

// ---- History ----

// A request is a bucket length and a number that grows, so an answer can be matched to it.
static portMUX_TYPE s_wantMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_wantBucket = 0;
static uint32_t s_wantNumber = 0;
static SemaphoreHandle_t s_answerLock = nullptr;
static History s_answer;             // guarded by s_answerLock
static uint32_t s_answerNumber = 0;  // guarded by s_answerLock
static std::atomic<bool> s_answerReady{false};

static uint32_t wanted(uint32_t& bucket) {
  portENTER_CRITICAL(&s_wantMux);
  bucket = s_wantBucket;
  const uint32_t number = s_wantNumber;
  portEXIT_CRITICAL(&s_wantMux);
  return number;
}

static HistoryState fetch(const net::Server& server, uint32_t bucket, History& out) {
  const std::string address = server.address;
  const size_t colon = address.find(':');
  if (colon == std::string::npos) return HistoryState::NoServer;
  if (net::status().state != net::State::Connected) return HistoryState::NoWifi;
  const time_t now = time(nullptr);
  if (now < 1700000000) return HistoryState::NoClock;
  out.span = thermo::graphSpan(static_cast<uint32_t>(now), bucket);

  char path[112];
  snprintf(path, sizeof(path), "/api/v1/devices/%s/graph?from=%lu&to=%lu&bucket=%lu", s_device.c_str(),
           static_cast<unsigned long>(out.span.start), static_cast<unsigned long>(out.span.end()),
           static_cast<unsigned long>(bucket));
  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  if (!http.begin(client, address.substr(0, colon).c_str(),
                  static_cast<uint16_t>(atoi(address.c_str() + colon + 1)), path)) {
    return HistoryState::NoAnswer;
  }
  http.addHeader("X-Api-Token", server.token);
  const int code = http.GET();
  HistoryState state = HistoryState::NoAnswer;
  if (code == 401) state = HistoryState::BadToken;
  if (code == 404) state = HistoryState::NoRecords;
  if (code == 200) {
    const int size = http.getSize();  // -1 if not given; a full reply is about 9 KB
    if (size > 0 && size <= 20000) {
      const String text = http.getString();
      if (static_cast<int>(text.length()) == size) {
        state = thermo::parseGraph(text.c_str(), out.span, out.points) ? HistoryState::Ok : HistoryState::OldServer;
      }
    } else {
      state = HistoryState::OldServer;
    }
  }
  http.end();
  return state;
}

static void answerHistory(const net::Server& server) {
  static uint32_t done = 0;
  static History work;  // too large for the stack
  uint32_t bucket;
  const uint32_t number = wanted(bucket);
  if (number == done) return;
  done = number;
  work.state = fetch(server, bucket, work);
  xSemaphoreTake(s_answerLock, portMAX_DELAY);
  s_answer = work;
  s_answerNumber = number;
  s_answerReady = true;
  xSemaphoreGive(s_answerLock);
}

void requestHistory(uint32_t bucket) {
  portENTER_CRITICAL(&s_wantMux);
  s_wantBucket = bucket;
  ++s_wantNumber;
  portEXIT_CRITICAL(&s_wantMux);
}

bool takeHistory(History& out) {
  if (!s_answerReady || !s_answerLock) return false;
  uint32_t bucket;
  const uint32_t number = wanted(bucket);
  xSemaphoreTake(s_answerLock, portMAX_DELAY);  // only ever held for a short copy
  s_answerReady = false;
  const bool mine = s_answerNumber == number;
  if (mine) out = s_answer;
  xSemaphoreGive(s_answerLock);
  return mine;
}

static void task(void*) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char id[13];
  snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  s_device = id;

  s_flash.part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!s_flash.part || s_flash.part->size < kSectors * thermo::LogCache::kSector ||
      !s_cache.begin(s_flash, kSectors)) {
    Serial.println("Record cache FAILED to open");
  }
  s_pending = s_cache.pending();
  if (s_pending > 0) Serial.printf("%lu records waiting for the server\n", static_cast<unsigned long>(s_pending));

  net::Server last = {};
  uint32_t triedMs = 0, syncMs = 0;
  bool failed = false, synced = false;

  for (;;) {
    thermo::LogRecord rec;
    const bool got = xQueueReceive(s_queue, &rec, pdMS_TO_TICKS(200)) == pdTRUE;

    const net::Server server = net::server();
    answerHistory(server);
    if (server.address[0] == '\0') {
      s_link = Link::Off;
      last = server;
      continue;
    }
    if (strcmp(server.address, last.address) != 0 || strcmp(server.token, last.token) != 0) {
      last = server;  // just set, or changed: try at once
      failed = synced = false;
      setLink(Link::Waiting);
    }

    const bool up = net::status().state == net::State::Connected;
    if (!up) {
      setLink(Link::Waiting);
      synced = false;  // at once when Wi-Fi is back
    } else if (s_syncSoon.exchange(false) || !synced || millis() - syncMs >= kSyncMs) {
      if (sync(server)) {
        synced = true;
        syncMs = millis();
      }
    }
    const bool reachable = up && (s_link == Link::Ok || s_link == Link::OldServer);

    if (got) {
      // Straight to the server if it is there and nothing older is waiting; otherwise behind it.
      if (!reachable || s_cache.pending() > 0) {
        keep(rec);
      } else {
        failed = !send(server, {rec});
        triedMs = millis();
        if (failed) keep(rec);
      }
    }

    if (reachable && s_cache.pending() > 0 && (!failed || millis() - triedMs >= kRetryMs)) {
      const std::vector<thermo::LogRecord> batch = s_cache.peek(kBatch);
      failed = !send(server, batch);
      triedMs = millis();
      if (!failed) {
        if (!s_cache.markSent()) Serial.println("Record cache write FAILED");
        Serial.printf("Sent %u waiting records\n", static_cast<unsigned>(batch.size()));
      }
    }
    s_pending = s_cache.pending();
  }
}

void begin() {
  s_queue = xQueueCreate(kQueueLength, sizeof(thermo::LogRecord));
  s_answerLock = xSemaphoreCreateMutex();
  s_syncLock = xSemaphoreCreateMutex();
  if (!s_queue || !s_answerLock || !s_syncLock || xTaskCreatePinnedToCore(task, "uplink", 10240, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("Uplink task FAILED to start");
  }
}

void add(const thermo::LogRecord& rec) {
  if (s_queue && xQueueSend(s_queue, &rec, 0) != pdTRUE) Serial.println("Record dropped: queue full");
}

const char* notice() {
  // A short gap in Wi-Fi is not worth a message; a network being set up is shown on its own screen.
  static uint32_t lastUpMs = 0;
  const net::State st = net::status().state;
  if (st != net::State::Connecting && st != net::State::Offline) lastUpMs = millis();
  if (millis() - lastUpMs >= kWifiNoticeMs) return "No Wi-Fi";
  if (st != net::State::Connected) return "";
  switch (s_link.load()) {
    case Link::NoAnswer: return "Server not reachable";
    case Link::BadToken: return "Server rejected the token";
    case Link::OldServer: return "Server needs updating";
    default: return "";
  }
}

void summary(char* buf, size_t n) {
  const unsigned long waiting = s_pending;
  const char* text = "";
  switch (s_link.load()) {
    case Link::Off: break;
    case Link::Waiting: text = "Server: connecting"; break;
    case Link::Ok: text = "Server connected"; break;
    case Link::NoAnswer: text = "Server not answering"; break;
    case Link::BadToken: text = "Server rejected the token"; break;
    case Link::OldServer: text = "Server needs updating"; break;
  }
  if (text[0] != '\0' && waiting > 0) snprintf(buf, n, "%s, %lu waiting", text, waiting);
  else snprintf(buf, n, "%s", text);
}

}  // namespace uplink
