module;
#include "prelude.hpp"
export module control;

export import render;

export struct RenderInput {
  Vec3 move{0.0, 0.0, 0.0};
  Vec2 rotate{0.0, 0.0};
  bool toggle_pause = false;
  bool toggle_gi = false;
  bool toggle_ao = false;

  void apply(RenderEngine &engine) const {
    if (toggle_pause) {
      engine.settings.paused = !engine.settings.paused;
    }
    if (toggle_gi) {
      engine.settings.gi.enabled = !engine.settings.gi.enabled;
      if (engine.settings.gi.enabled && engine.settings.gi.strength <= 0.0) {
        engine.settings.gi.strength = 1.0;
      }
    }
    if (toggle_ao) {
      engine.settings.ambient_occlusion_enabled =
          !engine.settings.ambient_occlusion_enabled;
    }
    if (rotate.x != 0.0 || rotate.y != 0.0) {
      engine.camera.rotate(rotate);
    }
    if (move.x != 0.0 || move.y != 0.0 || move.z != 0.0) {
      engine.camera.move(move);
    }
  }
};

export struct RenderInputMailbox {
  auto push_move(const Vec3 &intent) -> void {
    std::scoped_lock lock(mutex);
    pending.move = pending.move + intent;
  }

  auto push_rotate(const Vec2 &delta) -> void {
    std::scoped_lock lock(mutex);
    pending.rotate = pending.rotate + delta;
  }

  auto push_toggle_pause() -> void {
    std::scoped_lock lock(mutex);
    pending.toggle_pause = !pending.toggle_pause;
  }

  auto push_toggle_gi() -> void {
    std::scoped_lock lock(mutex);
    pending.toggle_gi = !pending.toggle_gi;
  }

  auto push_toggle_ao() -> void {
    std::scoped_lock lock(mutex);
    pending.toggle_ao = !pending.toggle_ao;
  }

  auto drain() -> RenderInput {
    std::scoped_lock lock(mutex);
    return std::exchange(pending, {});
  }

private:
  std::mutex mutex;
  RenderInput pending{};
};
