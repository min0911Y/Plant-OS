#include "test_prelude.hpp"

import input;

TEST_CASE("key_to_action maps quit keys")
{
    REQUIRE(InputParser::key_to_action('q') == InputAction::Quit);
    REQUIRE(InputParser::key_to_action('Q') == InputAction::Quit);
    REQUIRE(InputParser::key_to_action('x') == InputAction::None);
    REQUIRE(InputParser::key_to_action(-1) == InputAction::None);
}

TEST_CASE("key_to_action maps pause keys")
{
    REQUIRE(InputParser::key_to_action('p') == InputAction::TogglePause);
    REQUIRE(InputParser::key_to_action('P') == InputAction::TogglePause);
}

TEST_CASE("key_to_action maps GI toggle keys")
{
    REQUIRE(InputParser::key_to_action('g') == InputAction::ToggleGI);
    REQUIRE(InputParser::key_to_action('G') == InputAction::ToggleGI);
}

TEST_CASE("key_to_action maps camera movement keys")
{
    REQUIRE(InputParser::key_to_action('w') == InputAction::MoveForward);
    REQUIRE(InputParser::key_to_action('s') == InputAction::MoveBackward);
    REQUIRE(InputParser::key_to_action('a') == InputAction::MoveLeft);
    REQUIRE(InputParser::key_to_action('d') == InputAction::MoveRight);
    REQUIRE(InputParser::key_to_action('r') == InputAction::MoveUp);
    REQUIRE(InputParser::key_to_action('f') == InputAction::MoveDown);
}

TEST_CASE("parse decodes plain keys one byte at a time")
{
    const InputEvent event = InputParser::parse(std::string_view("wq"));
    REQUIRE(event.consumed == 1);
    REQUIRE(event.action == InputAction::MoveForward);
    REQUIRE_FALSE(event.mouse.has_value());
}

TEST_CASE("parse decodes SGR mouse motion event")
{
    const InputEvent event = InputParser::parse(std::string_view("\x1b[<35;12;8M"));
    REQUIRE(event.consumed == 11);
    REQUIRE(event.action == InputAction::None);
    REQUIRE(event.mouse.has_value());
    REQUIRE(event.mouse->x == 12);
    REQUIRE(event.mouse->y == 8);
}

TEST_CASE("parse reports incomplete escape sequences")
{
    REQUIRE(InputParser::parse(std::string_view("\x1b")).consumed == 0);
    REQUIRE(InputParser::parse(std::string_view("\x1b[")).consumed == 0);
    REQUIRE(InputParser::parse(std::string_view("\x1b[<35;12;")).consumed == 0);
}

TEST_CASE("parse decodes arrow keys")
{
    const InputEvent up = InputParser::parse(std::string_view("\x1b[A"));
    REQUIRE(up.consumed == 3);
    REQUIRE(up.action == InputAction::MoveForward);

    REQUIRE(InputParser::parse(std::string_view("\x1b[B")).action == InputAction::MoveBackward);
    REQUIRE(InputParser::parse(std::string_view("\x1b[C")).action == InputAction::MoveRight);
    REQUIRE(InputParser::parse(std::string_view("\x1b[D")).action == InputAction::MoveLeft);
}

TEST_CASE("parse decodes SS3 application-mode arrows")
{
    const InputEvent up = InputParser::parse(std::string_view("\x1bOA"));
    REQUIRE(up.consumed == 3);
    REQUIRE(up.action == InputAction::MoveForward);
}

TEST_CASE("parse consumes unknown CSI sequences without emitting actions")
{
    const InputEvent modified = InputParser::parse(std::string_view("\x1b[1;5D"));
    REQUIRE(modified.consumed == 6);
    REQUIRE(modified.action == InputAction::None);
    REQUIRE_FALSE(modified.mouse.has_value());

    const InputEvent del = InputParser::parse(std::string_view("\x1b[3~"));
    REQUIRE(del.consumed == 4);
    REQUIRE(del.action == InputAction::None);
}

TEST_CASE("parse ignores alt-modified keys")
{
    const InputEvent event = InputParser::parse(std::string_view("\x1b" "a"));
    REQUIRE(event.consumed == 2);
    REQUIRE(event.action == InputAction::None);
}

TEST_CASE("parse rejects malformed SGR mouse parameters")
{
    const InputEvent event = InputParser::parse(std::string_view("\x1b[<35;;8M"));
    REQUIRE(event.consumed == 9);
    REQUIRE(event.action == InputAction::None);
    REQUIRE_FALSE(event.mouse.has_value());
}

TEST_CASE("movement_intent maps vertical movement directions")
{
    const Vec3 up = InputParser::movement_intent(InputAction::MoveUp);
    const Vec3 down = InputParser::movement_intent(InputAction::MoveDown);

    REQUIRE(up.y == Catch::Approx(1.0));
    REQUIRE(down.y == Catch::Approx(-1.0));
    REQUIRE(up.x == Catch::Approx(0.0));
    REQUIRE(up.z == Catch::Approx(0.0));
}

TEST_CASE("movement_intent maps forward and strafe axes")
{
    REQUIRE(InputParser::movement_intent(InputAction::MoveForward).z == Catch::Approx(1.0));
    REQUIRE(InputParser::movement_intent(InputAction::MoveBackward).z == Catch::Approx(-1.0));
    REQUIRE(InputParser::movement_intent(InputAction::MoveRight).x == Catch::Approx(1.0));
    REQUIRE(InputParser::movement_intent(InputAction::MoveLeft).x == Catch::Approx(-1.0));
}

TEST_CASE("movement_intent is zero for non-movement actions")
{
    for (const InputAction action : {InputAction::None, InputAction::Quit,
                                     InputAction::TogglePause, InputAction::ToggleGI,
                                     InputAction::ToggleAO})
    {
        const Vec3 intent = InputParser::movement_intent(action);
        REQUIRE(intent.x == Catch::Approx(0.0));
        REQUIRE(intent.y == Catch::Approx(0.0));
        REQUIRE(intent.z == Catch::Approx(0.0));
    }
}

TEST_CASE("mouse_look_velocity respects deadzone")
{
    const auto delta = InputParser::mouse_look_velocity({
        .mouse_x = 40,
        .mouse_y = 12,
        .term_width = 80,
        .term_height = 24,
        .deadzone_radius = 2,
        .max_speed = 1.0
    });

    REQUIRE(delta.yaw == Catch::Approx(0.0));
    REQUIRE(delta.pitch == Catch::Approx(0.0));
}

TEST_CASE("mouse_look_velocity scales with distance and direction")
{
    MouseLookParams params {
        .mouse_x = 0,
        .mouse_y = 0,
        .term_width = 80,
        .term_height = 24,
        .deadzone_radius = 2,
        .max_speed = 1.0
    };

    params.mouse_x = 38;
    params.mouse_y = 8;
    const auto near = InputParser::mouse_look_velocity(params);

    params.mouse_x = 1;
    params.mouse_y = 1;
    const auto far = InputParser::mouse_look_velocity(params);

    REQUIRE(near.yaw < 0.0);
    REQUIRE(near.pitch < 0.0);
    REQUIRE(far.yaw < 0.0);
    REQUIRE(far.pitch < 0.0);

    REQUIRE(std::abs(far.yaw) > std::abs(near.yaw));
    REQUIRE(std::abs(far.pitch) > std::abs(near.pitch));
}
