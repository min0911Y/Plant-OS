module;

#include "prelude.hpp"

export module input;

export import math;

export enum class InputAction
{
    None,
    Quit,
    TogglePause,
    ToggleGI,
    ToggleAO,
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown
};

export struct MousePosition
{
    int x = 0;
    int y = 0;
};

export struct InputEvent
{
    size_t consumed = 0;
    InputAction action = InputAction::None;
    std::optional<MousePosition> mouse{};
};

export struct MouseLookDelta
{
    double yaw;
    double pitch;
};

export struct MouseLookParams
{
    int mouse_x;
    int mouse_y;
    int term_width;
    int term_height;
    int deadzone_radius;
    double max_speed;
};

export struct InputParser
{
    static auto key_to_action(int ch) -> InputAction;
    static auto parse(std::string_view buffer) -> InputEvent;
    static constexpr auto movement_intent(InputAction action) -> Vec3;
    static auto mouse_look_velocity(const MouseLookParams& params) -> MouseLookDelta;
};

[[nodiscard]]
auto InputParser::key_to_action(const int ch) -> InputAction
{
    if (ch < 0)
    {
        return InputAction::None;
    }
    switch (std::tolower(static_cast<unsigned char>(ch)))
    {
        case 'q': return InputAction::Quit;
        case 'p': return InputAction::TogglePause;
        case 'g': return InputAction::ToggleGI;
        case 'o': return InputAction::ToggleAO;
        case 'w': return InputAction::MoveForward;
        case 's': return InputAction::MoveBackward;
        case 'a': return InputAction::MoveLeft;
        case 'd': return InputAction::MoveRight;
        case 'r': return InputAction::MoveUp;
        case 'f': return InputAction::MoveDown;
        default: return InputAction::None;
    }
}

[[nodiscard]]
constexpr auto InputParser::movement_intent(const InputAction action) -> Vec3
{
    switch (action)
    {
        case InputAction::MoveForward: return {0.0, 0.0, 1.0};
        case InputAction::MoveBackward: return {0.0, 0.0, -1.0};
        case InputAction::MoveRight: return {1.0, 0.0, 0.0};
        case InputAction::MoveLeft: return {-1.0, 0.0, 0.0};
        case InputAction::MoveUp: return {0.0, 1.0, 0.0};
        case InputAction::MoveDown: return {0.0, -1.0, 0.0};
        default: return {0.0, 0.0, 0.0};
    }
}

namespace {

constexpr char kEsc = '\x1b';

[[nodiscard]]
auto arrow_action(const char ch) -> InputAction
{
    switch (ch)
    {
        case 'A': return InputAction::MoveForward;
        case 'B': return InputAction::MoveBackward;
        case 'C': return InputAction::MoveRight;
        case 'D': return InputAction::MoveLeft;
        default: return InputAction::None;
    }
}

[[nodiscard]]
auto parse_sgr_mouse(const std::string_view params) -> std::optional<MousePosition>
{
    std::array<int, 3> values{};
    size_t begin = 0;
    for (size_t field = 0; field < values.size(); ++field)
    {
        const size_t end = field + 1 < values.size() ? params.find(';', begin) : params.size();
        if (end == std::string_view::npos || end < begin)
        {
            return std::nullopt;
        }
        const char* first = params.data() + begin;
        const char* last = params.data() + end;
        const auto result = std::from_chars(first, last, values[field]);
        if (result.ec != std::errc{} || result.ptr != last)
        {
            return std::nullopt;
        }
        begin = end + 1;
    }
    return MousePosition{values[1], values[2]};
}

[[nodiscard]]
auto parse_csi(const std::string_view buffer) -> InputEvent
{
    size_t i = 2;
    while (i < buffer.size() &&
           static_cast<unsigned char>(buffer[i]) >= 0x20 &&
           static_cast<unsigned char>(buffer[i]) <= 0x3F)
    {
        ++i;
    }
    if (i >= buffer.size())
    {
        return {};
    }

    const char final_byte = buffer[i];
    const size_t consumed = i + 1;
    if (static_cast<unsigned char>(final_byte) < 0x40 ||
        static_cast<unsigned char>(final_byte) > 0x7E)
    {
        return {consumed, InputAction::None, {}};
    }
    if (buffer[2] == '<' && (final_byte == 'M' || final_byte == 'm'))
    {
        return {consumed, InputAction::None, parse_sgr_mouse(buffer.substr(3, i - 3))};
    }
    if (i == 2)
    {
        return {consumed, arrow_action(final_byte), {}};
    }
    return {consumed, InputAction::None, {}};
}

}

[[nodiscard]]
auto InputParser::parse(const std::string_view buffer) -> InputEvent
{
    if (buffer.empty())
    {
        return {};
    }
    if (buffer[0] != kEsc)
    {
        return {1, key_to_action(static_cast<unsigned char>(buffer[0])), {}};
    }
    if (buffer.size() < 2)
    {
        return {};
    }
    if (buffer[1] == '[')
    {
        return parse_csi(buffer);
    }
    if (buffer[1] == 'O')
    {
        if (buffer.size() < 3)
        {
            return {};
        }
        return {3, arrow_action(buffer[2]), {}};
    }
    if (buffer[1] == kEsc)
    {
        return {1, InputAction::None, {}};
    }
    return {2, InputAction::None, {}};
}

[[nodiscard]]
auto InputParser::mouse_look_velocity(const MouseLookParams& params) -> MouseLookDelta
{
    if (params.term_width <= 0 || params.term_height <= 0 || params.max_speed <= 0.0)
    {
        return {0.0, 0.0};
    }

    const double center_x = (static_cast<double>(params.term_width) + 1.0) * 0.5;
    const double center_y = (static_cast<double>(params.term_height) + 1.0) * 0.5;
    const double dx = static_cast<double>(params.mouse_x) - center_x;
    const double dy = static_cast<double>(params.mouse_y) - center_y;

    if (std::abs(dx) <= params.deadzone_radius && std::abs(dy) <= params.deadzone_radius)
    {
        return {0.0, 0.0};
    }

    const double max_dx = std::max(center_x - 1.0, static_cast<double>(params.term_width) - center_x);
    const double max_dy = std::max(center_y - 1.0, static_cast<double>(params.term_height) - center_y);
    const double avail_x = max_dx - static_cast<double>(params.deadzone_radius);
    const double avail_y = max_dy - static_cast<double>(params.deadzone_radius);

    if (avail_x <= 0.0 || avail_y <= 0.0) return {0.0, 0.0};

    const double mag_x = std::clamp((std::abs(dx) - params.deadzone_radius) / avail_x, 0.0, 1.0);
    const double mag_y = std::clamp((std::abs(dy) - params.deadzone_radius) / avail_y, 0.0, 1.0);
    const double yaw = std::copysign(mag_x * params.max_speed, dx);
    const double pitch = std::copysign(mag_y * params.max_speed, dy);

    return {yaw, pitch};
}
