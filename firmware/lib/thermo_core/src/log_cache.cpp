#include "log_cache.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

namespace thermo {

// Stored as is (little-endian). Do not change the layout without changing kMagic.
struct Stored {
  uint32_t time;
  int16_t tempCentiF;  // kNoTemp if none
  int16_t setpointCentiF;
  uint16_t pressureDeciHpa;
  uint8_t humidity;    // kNoHumidity if none
  uint8_t heatPct;
  uint8_t flags;
  uint8_t unsent;      // 0xFF until sent, then 0; not covered by the CRC
  uint16_t crc;
};
static_assert(sizeof(Stored) == LogCache::kRecord, "log record layout");

struct Header {
  uint32_t magic;
  uint32_t seq;
  uint8_t reserved[6];
  uint16_t crc;
};
static_assert(sizeof(Header) == LogCache::kRecord, "log header layout");

static const uint32_t kMagic = 0x31474F4C;  // "LOG1"
static const int16_t kNoTemp = -32768;
static const uint8_t kNoHumidity = 255;
static const uint8_t kFault = 1, kModeOff = 2, kPartial = 4;
static const uint32_t kUnsentOffset = 13;
static_assert(offsetof(Stored, unsent) == kUnsentOffset, "log record layout");

// CRC-16/CCITT of the bytes before `unsent`.
static uint16_t crcOf(const void* rec) {
  const uint8_t* p = static_cast<const uint8_t*>(rec);
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < kUnsentOffset; ++i) {
    crc ^= static_cast<uint16_t>(p[i] << 8);
    for (int b = 0; b < 8; ++b) {
      crc = static_cast<uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
  }
  return crc;
}

static bool isBlank(const Stored& r) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&r);
  for (size_t i = 0; i < sizeof(r); ++i) {
    if (p[i] != 0xFF) return false;
  }
  return true;
}

static bool isUnsent(const Stored& r) { return !isBlank(r) && r.unsent == 0xFF && r.crc == crcOf(&r); }

static Stored pack(const LogRecord& r) {
  Stored s;
  s.time = r.time;
  s.tempCentiF = r.hasTemp ? static_cast<int16_t>(lroundf(r.tempF * 100.0f)) : kNoTemp;
  s.setpointCentiF = static_cast<int16_t>(lroundf(r.setpointF * 100.0f));
  s.pressureDeciHpa = r.hasTemp ? static_cast<uint16_t>(lroundf(r.pressureHpa * 10.0f)) : 0;
  s.humidity = r.hasHumidity ? static_cast<uint8_t>(lroundf(r.humidity)) : kNoHumidity;
  s.heatPct = r.heatPct;
  s.flags = static_cast<uint8_t>((r.sensorFault ? kFault : 0) | (r.modeOff ? kModeOff : 0) |
                                 (r.partial ? kPartial : 0));
  s.unsent = 0xFF;
  s.crc = crcOf(&s);
  return s;
}

static LogRecord unpack(const Stored& s) {
  LogRecord r;
  r.time = s.time;
  r.hasTemp = s.tempCentiF != kNoTemp;
  r.tempF = r.hasTemp ? s.tempCentiF / 100.0f : 0;
  r.setpointF = s.setpointCentiF / 100.0f;
  r.pressureHpa = s.pressureDeciHpa / 10.0f;
  r.hasHumidity = s.humidity != kNoHumidity;
  r.humidity = r.hasHumidity ? s.humidity : 0;
  r.heatPct = s.heatPct;
  r.sensorFault = s.flags & kFault;
  r.modeOff = s.flags & kModeOff;
  r.partial = s.flags & kPartial;
  return r;
}

bool LogCache::readHeader(uint32_t sector, uint32_t& seq) {
  Header h;
  if (!flash_->read(sector * kSector, &h, sizeof(h))) return false;
  if (h.magic != kMagic || h.seq == 0 || h.crc != crcOf(&h)) return false;
  seq = h.seq;
  return true;
}

bool LogCache::startSector(uint32_t sector, uint32_t seq) {
  if (!flash_->erase(sector * kSector)) return false;
  Header h;
  memset(&h, 0, sizeof(h));
  h.magic = kMagic;
  h.seq = seq;
  h.crc = crcOf(&h);
  if (!flash_->write(sector * kSector, &h, sizeof(h))) return false;
  head_ = sector;
  seq_ = seq;
  slot_ = 1;
  return true;
}

uint32_t LogCache::countUnsent(uint32_t seq) {
  const uint32_t sector = sectorOf(seq);
  uint32_t found, n = 0;
  if (!readHeader(sector, found) || found != seq) return 0;
  const uint32_t end = seq == seq_ ? slot_ : kPerSector + 1;
  for (uint32_t i = 1; i < end; ++i) {
    Stored r;
    if (flash_->read(sector * kSector + i * kRecord, &r, sizeof(r)) && isUnsent(r)) ++n;
  }
  return n;
}

bool LogCache::begin(LogFlash& flash, uint32_t sectors) {
  flash_ = nullptr;
  if (sectors < 2) return false;
  flash_ = &flash;
  sectors_ = sectors;
  head_ = seq_ = slot_ = pending_ = 0;
  tail_ = {1, 1};
  peeked_.clear();

  for (uint32_t i = 0; i < sectors_; ++i) {
    uint32_t seq;
    if (readHeader(i, seq) && seq > seq_) {
      seq_ = seq;
      head_ = i;
    }
  }
  if (seq_ == 0) return true;  // empty: the first append starts sector 0

  // Free slots come after the last one used, so a damaged one is never written over.
  slot_ = kPerSector + 1;
  while (slot_ > 1) {
    Stored r;
    if (!flash_->read(head_ * kSector + (slot_ - 1) * kRecord, &r, sizeof(r))) {
      flash_ = nullptr;
      return false;
    }
    if (!isBlank(r)) break;
    --slot_;
  }

  for (uint32_t seq = oldestSeq(); seq <= seq_; ++seq) pending_ += countUnsent(seq);
  return true;
}

bool LogCache::append(const LogRecord& rec) {
  if (!flash_) return false;
  if (seq_ == 0) {
    if (!startSector(0, 1)) return false;
  } else if (slot_ > kPerSector) {
    const uint32_t lost = seq_ + 1 > sectors_ ? countUnsent(oldestSeq()) : 0;
    if (!startSector((head_ + 1) % sectors_, seq_ + 1)) return false;
    pending_ -= lost < pending_ ? lost : pending_;
  }

  const Stored s = pack(rec);
  const uint32_t addr = head_ * kSector + slot_ * kRecord;
  ++slot_;  // even if the write fails: the slot may be partly written
  if (!flash_->write(addr, &s, sizeof(s))) return false;
  ++pending_;
  return true;
}

std::vector<LogRecord> LogCache::peek(size_t max) {
  std::vector<LogRecord> out;
  peeked_.clear();
  if (!flash_ || pending_ == 0 || max == 0) return out;

  Pos p = tail_;
  if (p.seq < oldestSeq()) p = {oldestSeq(), 1};
  bool first = true;
  for (; p.seq <= seq_; p = {p.seq + 1, 1}) {
    uint32_t found;
    const uint32_t sector = sectorOf(p.seq);
    if (!readHeader(sector, found) || found != p.seq) continue;
    const uint32_t end = p.seq == seq_ ? slot_ : kPerSector + 1;
    for (; p.slot < end; ++p.slot) {
      Stored r;
      if (!flash_->read(sector * kSector + p.slot * kRecord, &r, sizeof(r)) || !isUnsent(r)) continue;
      if (first) tail_ = p;
      first = false;
      out.push_back(unpack(r));
      peeked_.push_back(p);
      if (out.size() >= max) return out;
    }
  }
  if (out.empty()) pending_ = 0;  // the count was wrong: nothing is waiting
  return out;
}

bool LogCache::markSent() {
  bool ok = true;
  const uint8_t sent = 0;
  for (const Pos& p : peeked_) {
    ok &= flash_->write(sectorOf(p.seq) * kSector + p.slot * kRecord + kUnsentOffset, &sent, 1);
    if (pending_ > 0) --pending_;
    tail_ = {p.seq, p.slot + 1};  // moves on even if the write failed: not sent again until a restart
  }
  peeked_.clear();
  return ok;
}

}  // namespace thermo
