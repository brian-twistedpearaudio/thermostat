// Records waiting to be sent to the server, kept in a ring of flash sectors.
// No hardware calls: testable on the PC.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "log_interval.h"

namespace thermo {

class LogFlash {
 public:
  virtual ~LogFlash() = default;
  virtual bool read(uint32_t addr, void* buf, size_t n) = 0;
  virtual bool write(uint32_t addr, const void* buf, size_t n) = 0;  // can only clear bits
  virtual bool erase(uint32_t addr) = 0;                             // one sector, to 0xFF
};

// Each sector starts with a numbered header; the highest number is being written. A sent record
// is marked in place. A power cut loses at most the record being written: bad checksums are
// skipped, and a sector without a valid header is ignored. When full, the oldest are dropped.
class LogCache {
 public:
  static constexpr uint32_t kSector = 4096;
  static constexpr uint32_t kRecord = 16;
  static constexpr uint32_t kPerSector = kSector / kRecord - 1;  // slot 0 is the header

  bool begin(LogFlash& flash, uint32_t sectors);  // at least 2 sectors; keeps what is stored
  bool append(const LogRecord& rec);

  uint32_t pending() const { return pending_; }
  std::vector<LogRecord> peek(size_t max);  // the oldest unsent records
  bool markSent();                          // the records from the last peek

 private:
  struct Pos {
    uint32_t seq;
    uint32_t slot;
  };

  uint32_t sectorOf(uint32_t seq) const { return (head_ + sectors_ - (seq_ - seq)) % sectors_; }
  bool readHeader(uint32_t sector, uint32_t& seq);
  bool startSector(uint32_t sector, uint32_t seq);
  uint32_t oldestSeq() const { return seq_ >= sectors_ ? seq_ - sectors_ + 1 : 1; }
  uint32_t countUnsent(uint32_t seq);

  LogFlash* flash_ = nullptr;
  uint32_t sectors_ = 0;
  uint32_t head_ = 0;  // sector being written
  uint32_t seq_ = 0;   // its number; 0 = nothing stored yet
  uint32_t slot_ = 0;  // next free slot, 1..kPerSector; kPerSector + 1 = full
  uint32_t pending_ = 0;
  Pos tail_ = {1, 1};  // no unsent records before this
  std::vector<Pos> peeked_;
};

}  // namespace thermo
