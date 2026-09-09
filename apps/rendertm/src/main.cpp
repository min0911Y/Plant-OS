#include "prelude.hpp"

import input;
import keyboard;
import render;
import terminal;

struct SignalState
{
    static auto install() -> void
    {
        signal(SIGINT, &SignalState::handle);
        signal(SIGTERM, &SignalState::handle);
    }

    [[nodiscard]]
    static auto shutdown() -> bool
    {
        return shutdown_req != 0;
    }

private:
    static auto handle(int sig) -> void
    {
        shutdown_req = 1;
    }

    static inline volatile int shutdown_req = 0;
};

struct TerminalSession
{
    TerminalSession()
    {
        TerminalRender::init();
    }

    ~TerminalSession()
    {
        TerminalRender::shutdown();
    }

    TerminalSession(const TerminalSession&) = delete;
    auto operator=(const TerminalSession&) -> TerminalSession& = delete;

    [[nodiscard]]
    auto read_char() const -> std::optional<unsigned char>
    {
        return keyboard.read_char();
    }

private:
    KeyboardMode keyboard;
};

struct RenderThreads
{
    RenderThreads(RenderEngine& engine, RenderInputMailbox& mailbox):
        output_thread([](std::stop_token token) {
            TerminalRender::output_loop(token);
        }),
        render_thread([&engine, &mailbox](std::stop_token token) {
            while (!token.stop_requested())
            {
                TerminalRender::submit_frame(engine, mailbox);
            }
        })
    {}

    ~RenderThreads()
    {
        render_thread.request_stop();
        output_thread.request_stop();
    }

    RenderThreads(const RenderThreads&) = delete;
    auto operator=(const RenderThreads&) -> RenderThreads& = delete;

private:
    std::jthread output_thread;
    std::jthread render_thread;
};

struct App
{
    auto run() -> int
    {
        while (true)
        {
            if (SignalState::shutdown())
            {
                return 0;
            }
            TerminalRender::update_size();

            wait_for_input(kPollIntervalMs);
            read_keyboard();
            tty_pointer_t pointer;
            if (tty_get_pointer(&pointer) == 0)
                mouse_pos = MousePosition{pointer.column, pointer.row};
            if (!process_input()) return 0;

            const double dt = sample_dt();
            if (dt > 0.0) update_mouse_look(dt);
        }
    }

private:
    static constexpr int kPollIntervalMs = 8;
    static constexpr double kMoveStep = 0.2;
    static constexpr double kMouseMaxSpeed = 1.2;
    static constexpr int kMouseDeadzone = 8;
    static constexpr auto kEscTimeout = std::chrono::milliseconds(50);

    RenderEngine engine;
    RenderInputMailbox mailbox;
    TerminalSession session;
    RenderThreads threads{engine, mailbox};
    std::string input_buffer;
    std::optional<MousePosition> mouse_pos;
    std::chrono::steady_clock::time_point last_input_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_look_time = std::chrono::steady_clock::now();

    static auto wait_for_input(const int timeout_ms) -> void
    {
        PlatformTerminal::wait_input(timeout_ms);
    }

    auto read_keyboard() -> void
    {
        const size_t before = input_buffer.size();
        auto ch = session.read_char();
        while (ch.has_value())
        {
            input_buffer.push_back(static_cast<char>(*ch));
            ch = session.read_char();
        }
        if (input_buffer.size() > before)
        {
            last_input_time = std::chrono::steady_clock::now();
        }
    }

    [[nodiscard]]
    auto process_input() -> bool
    {
        size_t offset = 0;
        while (offset < input_buffer.size())
        {
            std::string_view view(input_buffer);
            view.remove_prefix(offset);

            const InputEvent event = InputParser::parse(view);
            if (event.consumed == 0)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - last_input_time < kEscTimeout)
                {
                    break;
                }
                offset += 1;
                continue;
            }

            offset += event.consumed;
            if (event.mouse)
            {
                mouse_pos = event.mouse;
            }
            if (!handle_action(event.action)) return false;
        }

        if (offset > 0)
        {
            input_buffer.erase(0, offset);
        }
        return true;
    }

    [[nodiscard]]
    auto handle_action(InputAction action) -> bool
    {
        if (PlatformTerminal::testing && action != InputAction::None)
            logkf("RENDERTM ACTION %d\n", static_cast<int>(action));
        switch (action)
        {
            case InputAction::Quit: return false;
            case InputAction::None: return true;
            case InputAction::ToggleAO: mailbox.push_toggle_ao(); return true;
            case InputAction::ToggleGI: mailbox.push_toggle_gi(); return true;
            case InputAction::TogglePause: mailbox.push_toggle_pause(); return true;
            default: break;
        }

        mailbox.push_move(InputParser::movement_intent(action) * kMoveStep);
        return true;
    }

    auto sample_dt() -> double
    {
        const auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_look_time).count();
        last_look_time = now;
        return std::clamp(dt, 0.0, 0.1);
    }

    auto update_mouse_look(double dt) -> void
    {
        if (!mouse_pos) return;

        const TerminalSize term_size = TerminalRender::size();
        if (term_size.width == 0 || term_size.height <= 1) return;

        const int max_x = static_cast<int>(term_size.width);
        const int max_y = static_cast<int>(term_size.height - 1);

        const MouseLookParams look_params{
            .mouse_x = std::clamp(mouse_pos->x, 1, max_x),
            .mouse_y = std::clamp(mouse_pos->y - 1, 1, max_y),
            .term_width = max_x,
            .term_height = max_y,
            .deadzone_radius = kMouseDeadzone,
            .max_speed = kMouseMaxSpeed
        };

        const MouseLookDelta velocity = InputParser::mouse_look_velocity(look_params);

        if (velocity.yaw != 0.0 || velocity.pitch != 0.0)
        {
            mailbox.push_rotate({velocity.yaw * dt, velocity.pitch * dt});
        }
    }
};

extern "C" auto main(int argc, char **argv) -> int
{
    PlatformTerminal::testing = argc == 2 && std::strcmp(argv[1], "--test") == 0;
    if (argc > 1 && !PlatformTerminal::testing) {
        print("RenderTM: WASD/arrows move, R/F rise/fall, mouse look, P pause, G GI, O AO, Q quit\n");
        return std::strcmp(argv[1], "--help") == 0 ? 0 : 1;
    }
    if (PlatformTerminal::testing)
        logkf("RENDERTM START\n");
    SignalState::install();
    App app;
    return app.run();
}
