#pragma once

#include <string>

#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

namespace esphome {
namespace jc3248_media {

class JC3248Select;
class JC3248MediaPlayer;

// ESPHome front-end for the shared browser/player core. The entities are thin
// wrappers: they post commands into the same queue the touch UI is driven by,
// and mirror the state the browser publishes back.
class JC3248Media : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_upload_port(uint16_t port) { this->upload_port_ = port; }
  void set_upload_username(const std::string &username) { this->upload_username_ = username; }
  void set_upload_password(const std::string &password) { this->upload_password_ = password; }
  void set_max_upload_bytes(uint32_t bytes) { this->max_upload_bytes_ = bytes; }

  // Actions exposed to the button templates in the YAML.
  void play_selected();
  void play_path(const std::string &path);
  void start_slideshow();
  void next();
  void previous();
  void stop();
  void rescan();
  void show_browser();
  void set_delete_path(const std::string &path);
  void delete_selected();

  void set_sleep(bool sleeping);
  void set_brightness(float percent);
  void set_screen_color(bool on, float red, float green, float blue);

  // Child entities register themselves so the core can publish state.
  void set_select(JC3248Select *select) { this->select_ = select; }
  void set_delete_select(JC3248Select *select) { this->delete_select_ = select; }
  void set_media_player(JC3248MediaPlayer *player) { this->media_player_ = player; }
  void set_sleep_switch(switch_::Switch *sw) { this->sleep_switch_ = sw; }
  void set_brightness_number(number::Number *number) { this->brightness_number_ = number; }
  void set_color_light(light::LightState *light) { this->color_light_ = light; }

  bool is_sleeping() const { return this->sleeping_; }
  bool is_playing() const;
  const std::string &current_file() const { return this->current_file_; }
  float brightness() const { return this->brightness_; }
  // CPU load in percent over the interval since the previous call, from the
  // FreeRTOS per-task run-time counters.
  float cpu_load();

  void publish_sleep_state(bool state) {
    if (this->sleep_switch_ != nullptr)
      this->sleep_switch_->publish_state(state);
  }
  void publish_brightness_state(float value) {
    if (this->brightness_number_ != nullptr)
      this->brightness_number_->publish_state(value);
  }

 protected:
  void wake_for_playback_();
  void refresh_options_();
  void disconnect_api_clients_();
  std::string first_file_();

  uint16_t upload_port_{8080};
  std::string upload_username_;
  std::string upload_password_;
  uint32_t max_upload_bytes_{64u * 1024u * 1024u};

  float brightness_{100.0f};
  bool sleeping_{false};
  bool color_on_{false};
  bool browser_color_mode_{false};
  bool http_started_{false};
  bool log_registered_{false};
  uint32_t upload_retry_ms_{0};
  uint32_t files_generation_{0};

  std::string selected_path_;
  std::string current_file_;
  std::string delete_path_;

  JC3248Select *select_{nullptr};
  JC3248Select *delete_select_{nullptr};
  JC3248MediaPlayer *media_player_{nullptr};
  switch_::Switch *sleep_switch_{nullptr};
  number::Number *brightness_number_{nullptr};
  light::LightState *color_light_{nullptr};
};

}  // namespace jc3248_media
}  // namespace esphome
