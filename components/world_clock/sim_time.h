#pragma once

#ifdef USE_HOST
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/time/real_time_clock.h"
#include <ctime>

namespace esphome::world_clock_time {

// Deterministic controllable RealTimeClock for headless host emulation & screenshot capture.
class SimTime final : public time::RealTimeClock {
 public:
  void update() override {}

  void set_epoch(time_t epoch, double scale = 1.0) {
    this->base_epoch_ = epoch;
    this->base_ms_ = millis();
    this->scale_ = scale;
    this->has_sim_epoch_ = true;
  }

  void clear_sim_epoch() { this->has_sim_epoch_ = false; }

  time_t timestamp_now() override {
    if (!this->has_sim_epoch_) {
      return ::time(nullptr);
    }
    uint32_t elapsed_ms = static_cast<uint32_t>(millis() - this->base_ms_);
    return this->base_epoch_ + static_cast<time_t>(static_cast<double>(elapsed_ms) * this->scale_ / 1000.0);
  }

 private:
  bool has_sim_epoch_{false};
  time_t base_epoch_{1767225600};  // 2026-01-01T00:00:00Z
  uint32_t base_ms_{0};
  double scale_{1.0};
};

}  // namespace esphome::world_clock_time
#endif
