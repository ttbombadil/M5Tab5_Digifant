#pragma once

#include "kwp_byte_engine.h"
#include "rx_ingress_ring.h"

namespace digifant::kwp {

class KwpReceiveService {
 public:
  void reset() noexcept { engine_.reset(); frameReady_ = false; }

  // The runner owns this call. USB callbacks never call the byte engine.
  void consume(const transport::RxIngressItem& item) noexcept {
    engine_.onRxByte(item.byte);
    if (engine_.frameComplete()) frameReady_ = true;
  }
  bool frameReady() const noexcept { return frameReady_; }
  const uint8_t* frameData() const noexcept { return engine_.frameData(); }
  uint8_t frameSize() const noexcept { return engine_.frameSize(); }
  ByteEngineFault fault() const noexcept { return engine_.fault(); }
  ByteEngineState state() const noexcept { return engine_.state(); }

 private:
  KwpByteEngine engine_{};
  bool frameReady_ = false;
};

}  // namespace digifant::kwp
