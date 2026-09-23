#pragma once

#include <cstdint>

namespace rmh {

// Optional future hook. The build and dashboard work with no MAX17048 fitted.
class FuelGauge {
public:
    bool begin() { return false; }
    [[nodiscard]] bool available() const { return false; }
    [[nodiscard]] std::uint8_t percent() const { return 0xFF; }
};

}  // namespace rmh
