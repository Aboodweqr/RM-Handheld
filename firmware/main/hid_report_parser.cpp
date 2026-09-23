#include "hid_report_parser.hpp"

#include <algorithm>
#include <array>
#include <climits>

namespace rmh {
namespace {

std::uint32_t read_unsigned(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

std::int32_t sign_extend(std::uint32_t value, std::size_t byte_count) {
    if (byte_count == 0) return 0;
    const auto bits = static_cast<unsigned>(byte_count * 8U);
    if (bits >= 32U) return static_cast<std::int32_t>(value);
    const std::uint32_t sign = 1UL << (bits - 1U);
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

std::uint32_t extract_bits(const std::uint8_t* data, std::size_t size,
                           std::uint16_t bit_offset, std::uint8_t bit_size,
                           bool& ok) {
    if (bit_size == 0 || bit_size > 32 ||
        static_cast<std::size_t>(bit_offset) + bit_size > size * 8U) {
        ok = false;
        return 0;
    }
    std::uint32_t result = 0;
    for (std::uint8_t bit = 0; bit < bit_size; ++bit) {
        const auto source = static_cast<std::size_t>(bit_offset) + bit;
        if ((data[source / 8U] & (1U << (source % 8U))) != 0) {
            result |= 1UL << bit;
        }
    }
    ok = true;
    return result;
}

std::int16_t normalize_axis(std::int32_t value, std::int32_t minimum,
                            std::int32_t maximum) {
    if (maximum <= minimum) return 0;
    value = std::clamp(value, minimum, maximum);
    const auto span = static_cast<std::int64_t>(maximum) - minimum;
    const auto scaled =
        ((static_cast<std::int64_t>(value) - minimum) * 65534LL) / span - 32767LL;
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(scaled, -32767, 32767));
}

std::uint16_t normalize_trigger(std::int32_t value, std::int32_t minimum,
                                std::int32_t maximum) {
    if (maximum <= minimum) return 0;
    value = std::clamp(value, minimum, maximum);
    const auto span = static_cast<std::int64_t>(maximum) - minimum;
    const auto scaled = ((static_cast<std::int64_t>(value) - minimum) * 1023LL) / span;
    return static_cast<std::uint16_t>(std::clamp<std::int64_t>(scaled, 0, 1023));
}

struct Globals {
    std::uint16_t usage_page{0};
    std::int32_t logical_minimum{0};
    std::int32_t logical_maximum{0};
    std::uint16_t report_size{0};
    std::uint16_t report_count{0};
    std::uint8_t report_id{0};
};

struct Locals {
    std::array<std::uint32_t, 32> usages{};
    std::size_t usage_count{0};
    std::uint32_t usage_minimum{UINT32_MAX};
    std::uint32_t usage_maximum{UINT32_MAX};

    void clear() {
        usage_count = 0;
        usage_minimum = UINT32_MAX;
        usage_maximum = UINT32_MAX;
    }

    std::uint32_t usage_at(std::size_t index, std::uint16_t default_page) const {
        std::uint32_t value = default_page;
        value <<= 16U;
        if (index < usage_count) {
            value = usages[index];
        } else if (usage_minimum != UINT32_MAX && usage_maximum >= usage_minimum) {
            value = std::min<std::uint32_t>(usage_minimum + index, usage_maximum);
        }
        if ((value >> 16U) == 0) value |= static_cast<std::uint32_t>(default_page) << 16U;
        return value;
    }
};

HidReportParser::Target target_for(std::uint16_t page, std::uint16_t usage,
                                   std::uint8_t& index) {
    index = 0;
    if (page == 0x09 && usage >= 1 && usage <= 32) {
        index = static_cast<std::uint8_t>(usage - 1);
        return HidReportParser::Target::Button;
    }
    if (page != 0x01) return HidReportParser::Target::Ignored;
    switch (usage) {
        case 0x30: return HidReportParser::Target::LeftX;
        case 0x31: return HidReportParser::Target::LeftY;
        case 0x32: return HidReportParser::Target::LeftTrigger;
        case 0x33: return HidReportParser::Target::RightX;
        case 0x34: return HidReportParser::Target::RightY;
        case 0x35: return HidReportParser::Target::RightTrigger;
        case 0x39: return HidReportParser::Target::Hat;
        default: return HidReportParser::Target::Ignored;
    }
}

}  // namespace

bool HidReportParser::parse_descriptor(const std::uint8_t* descriptor,
                                       std::size_t size) {
    field_count_ = 0;
    has_report_ids_ = false;
    error_ = "malformed descriptor";
    if (descriptor == nullptr || size == 0) return false;

    Globals globals;
    std::array<Globals, 4> stack{};
    std::size_t stack_size = 0;
    Locals locals;
    std::array<std::uint16_t, 256> offsets{};

    std::size_t cursor = 0;
    while (cursor < size) {
        const auto prefix = descriptor[cursor++];
        if (prefix == 0xFE) {
            if (cursor + 2 > size) return false;
            const auto long_size = descriptor[cursor];
            cursor += 2;
            if (cursor + long_size > size) return false;
            cursor += long_size;
            continue;
        }

        const std::size_t byte_count_code = prefix & 0x03U;
        const std::size_t byte_count = byte_count_code == 3 ? 4 : byte_count_code;
        const std::uint8_t type = (prefix >> 2U) & 0x03U;
        const std::uint8_t tag = (prefix >> 4U) & 0x0FU;
        if (cursor + byte_count > size) return false;
        const auto raw = read_unsigned(descriptor + cursor, byte_count);
        const auto signed_raw = sign_extend(raw, byte_count);
        cursor += byte_count;

        if (type == 1) {  // Global item.
            switch (tag) {
                case 0: globals.usage_page = static_cast<std::uint16_t>(raw); break;
                case 1: globals.logical_minimum = signed_raw; break;
                case 2:
                    globals.logical_maximum = globals.logical_minimum < 0
                                                  ? signed_raw
                                                  : static_cast<std::int32_t>(raw);
                    break;
                case 7: globals.report_size = static_cast<std::uint16_t>(raw); break;
                case 8:
                    if (raw == 0 || raw > 255) return false;
                    globals.report_id = static_cast<std::uint8_t>(raw);
                    has_report_ids_ = true;
                    break;
                case 9: globals.report_count = static_cast<std::uint16_t>(raw); break;
                case 10:
                    if (stack_size >= stack.size()) return false;
                    stack[stack_size++] = globals;
                    break;
                case 11:
                    if (stack_size == 0) return false;
                    globals = stack[--stack_size];
                    break;
                default: break;
            }
        } else if (type == 2) {  // Local item.
            auto usage = raw;
            if (byte_count <= 2) {
                usage |= static_cast<std::uint32_t>(globals.usage_page) << 16U;
            }
            if (tag == 0 && locals.usage_count < locals.usages.size()) {
                locals.usages[locals.usage_count++] = usage;
            } else if (tag == 1) {
                locals.usage_minimum = usage;
            } else if (tag == 2) {
                locals.usage_maximum = usage;
            }
        } else if (type == 0) {  // Main item.
            if (tag == 8) {      // Input.
                if (globals.report_size == 0 || globals.report_count == 0 ||
                    globals.report_size > 32) {
                    return false;
                }
                const bool constant = (raw & 0x01U) != 0;
                const bool variable = (raw & 0x02U) != 0;
                auto& offset = offsets[globals.report_id];
                for (std::uint16_t item = 0; item < globals.report_count; ++item) {
                    const auto full_usage = locals.usage_at(item, globals.usage_page);
                    const auto page = static_cast<std::uint16_t>(full_usage >> 16U);
                    const auto usage = static_cast<std::uint16_t>(full_usage & 0xFFFFU);
                    std::uint8_t target_index = 0;
                    const auto target = constant || !variable
                                            ? Target::Ignored
                                            : target_for(page, usage, target_index);
                    if (target != Target::Ignored) {
                        if (field_count_ >= fields_.size()) {
                            error_ = "too many HID fields";
                            return false;
                        }
                        fields_[field_count_++] = Field{
                            globals.report_id,
                            offset,
                            static_cast<std::uint8_t>(globals.report_size),
                            globals.logical_minimum,
                            globals.logical_maximum,
                            page,
                            usage,
                            target,
                            target_index,
                        };
                    }
                    offset = static_cast<std::uint16_t>(offset + globals.report_size);
                }
            }
            locals.clear();
        }
    }

    if (field_count_ == 0) {
        error_ = "descriptor contains no supported gamepad fields";
        return false;
    }
    error_ = "ok";
    return true;
}

bool HidReportParser::decode_input(const std::uint8_t* report, std::size_t size,
                                   GamepadState& output) const {
    if (report == nullptr || size == 0 || field_count_ == 0) return false;
    std::uint8_t report_id = 0;
    if (has_report_ids_) {
        report_id = report[0];
        ++report;
        --size;
    }

    GamepadState decoded;
    bool found = false;
    for (std::size_t index = 0; index < field_count_; ++index) {
        const auto& field = fields_[index];
        if (field.report_id != report_id) continue;
        bool ok = false;
        const auto raw = extract_bits(report, size, field.bit_offset, field.bit_size, ok);
        if (!ok) return false;
        std::int32_t value = static_cast<std::int32_t>(raw);
        if (field.logical_minimum < 0 && field.bit_size < 32) {
            const auto sign = 1UL << (field.bit_size - 1U);
            value = static_cast<std::int32_t>((raw ^ sign) - sign);
        }
        found = true;
        switch (field.target) {
            case Target::Button:
                if (value != 0 && field.target_index < 32) {
                    decoded.buttons |= 1UL << field.target_index;
                }
                break;
            case Target::LeftX:
                decoded.left_x = normalize_axis(value, field.logical_minimum,
                                                field.logical_maximum);
                break;
            case Target::LeftY:
                decoded.left_y = normalize_axis(value, field.logical_minimum,
                                                field.logical_maximum);
                break;
            case Target::RightX:
                decoded.right_x = normalize_axis(value, field.logical_minimum,
                                                 field.logical_maximum);
                break;
            case Target::RightY:
                decoded.right_y = normalize_axis(value, field.logical_minimum,
                                                 field.logical_maximum);
                break;
            case Target::LeftTrigger:
                decoded.left_trigger = normalize_trigger(value, field.logical_minimum,
                                                         field.logical_maximum);
                break;
            case Target::RightTrigger:
                decoded.right_trigger = normalize_trigger(value, field.logical_minimum,
                                                          field.logical_maximum);
                break;
            case Target::Hat:
                if (value >= field.logical_minimum && value <= field.logical_maximum) {
                    const auto zero_based = value - field.logical_minimum;
                    decoded.hat = zero_based <= 7 ? static_cast<std::uint8_t>(zero_based)
                                                  : 8;
                }
                break;
            case Target::Ignored: break;
        }
    }
    if (found) output = decoded;
    return found;
}

}  // namespace rmh
