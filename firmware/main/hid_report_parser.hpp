#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "gamepad_state.hpp"

namespace rmh {

// Small, allocation-free HID report-descriptor parser for standard gamepads.
// It intentionally understands only Input fields used by controllers.
class HidReportParser {
public:
    enum class Target : std::uint8_t {
        Button,
        LeftX,
        LeftY,
        RightX,
        RightY,
        LeftTrigger,
        RightTrigger,
        Hat,
        Ignored,
    };

    struct Field {
        std::uint8_t report_id{0};
        std::uint16_t bit_offset{0};
        std::uint8_t bit_size{0};
        std::int32_t logical_minimum{0};
        std::int32_t logical_maximum{0};
        std::uint16_t usage_page{0};
        std::uint16_t usage{0};
        Target target{Target::Ignored};
        std::uint8_t target_index{0};
    };

    bool parse_descriptor(const std::uint8_t* descriptor, std::size_t size);
    bool decode_input(const std::uint8_t* report, std::size_t size,
                      GamepadState& output) const;

    [[nodiscard]] std::size_t field_count() const { return field_count_; }
    [[nodiscard]] bool has_report_ids() const { return has_report_ids_; }
    [[nodiscard]] const char* error() const { return error_; }
    [[nodiscard]] const Field& field(std::size_t index) const {
        return fields_[index];
    }

private:
    static constexpr std::size_t kMaxFields = 96;
    std::array<Field, kMaxFields> fields_{};
    std::size_t field_count_{0};
    bool has_report_ids_{false};
    const char* error_{"not parsed"};
};

}  // namespace rmh
