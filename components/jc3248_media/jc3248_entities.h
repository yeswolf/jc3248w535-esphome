#pragma once

#include <string>

#include "esphome/components/light/light_output.h"
#include "esphome/components/media_player/media_player.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "jc3248_media.h"

namespace esphome {
namespace jc3248_media {

class JC3248Select : public select::Select, public Component, public Parented<JC3248Media> {
 public:
  void setup() override;
  void dump_config() override;

  // Rebuild the option list from the shared playlist. Returns true when the
  // list changed, which asks the parent to refresh Home Assistant's entity
  // info (ESPHome only sends select options with that message).
  bool refresh_options();
  void set_play_on_select(bool value) { this->play_on_select_ = value; }

 protected:
  void control(size_t index) override;

  char *storage_{nullptr};
  uint32_t options_hash_{0};
  size_t options_count_{0};
  bool play_on_select_{true};
};

class JC3248SleepSwitch : public switch_::Switch, public Component, public Parented<JC3248Media> {
 public:
  void setup() override;

 protected:
  void write_state(bool state) override;
};

class JC3248BrightnessNumber : public number::Number, public Component, public Parented<JC3248Media> {
 public:
  void setup() override;
  void set_initial_value(float value) { this->initial_value_ = value; }

 protected:
  void control(float value) override;
  float initial_value_{100.0f};
};

class JC3248MediaPlayer : public media_player::MediaPlayer, public Component, public Parented<JC3248Media> {
 public:
  void setup() override;
  void dump_config() override;
  media_player::MediaPlayerTraits get_traits() override;
  void set_playing(bool playing);

 protected:
  void control(const media_player::MediaPlayerCall &call) override;
};

class JC3248ScreenColor : public light::LightOutput, public Parented<JC3248Media> {
 public:
  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override;
  void write_state(light::LightState *state) override;

 protected:
  light::LightState *state_{nullptr};
};

}  // namespace jc3248_media
}  // namespace esphome
