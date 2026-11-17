#include "jc3248_media.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "esphome/components/logger/logger.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "freertos/task.h"

#ifdef USE_API
#include "esphome/components/api/api_connection.h"
#include "esphome/components/api/api_pb2.h"
#include "esphome/components/api/api_server.h"
#endif

#include "esp_heap_caps.h"

#include "app_control.h"
#include "board.h"
#include "core.h"
#include "display.h"
#include "files.h"
#include "fs_gate.h"
#include "http_upload.h"
#include "jc3248_entities.h"
#include "log_buffer.h"
#include "usb_msc.h"

namespace esphome {
namespace jc3248_media {

static const char *TAG = "jc3248_media";

static const char *const PLAYABLE_HINT =
    "an .avi/.mjpeg/.mjpg/.mjpe/.jpg file on the SD card is required";

#ifdef USE_LOG_LISTENERS
static void strip_ansi(char *text) {
  char *write = text;
  for (char *read = text; *read != '\0';) {
    if (*read == '\x1b') {
      read++;
      if (*read == '[') {
        read++;
        while (*read != '\0' && (*read < 0x40 || *read > 0x7e)) {
          read++;
        }
        if (*read != '\0') {
          read++;
        }
      } else if (*read != '\0') {
        read++;
      }
      continue;
    }
    *write++ = *read++;
  }
  *write = '\0';
}

static void log_to_ring(void *, uint8_t level, const char *tag, const char *message,
                        size_t message_len) {
  char line[240];
  if (message != nullptr && message_len > 0) {
    size_t len = message_len < sizeof(line) - 1 ? message_len : sizeof(line) - 1;
    memcpy(line, message, len);
    line[len] = '\0';
  } else {
    char letter = 'I';
    switch (level) {
      case ESP_LOG_ERROR:
        letter = 'E';
        break;
      case ESP_LOG_WARN:
        letter = 'W';
        break;
      case ESP_LOG_DEBUG:
        letter = 'D';
        break;
      case ESP_LOG_VERBOSE:
        letter = 'V';
        break;
      default:
        break;
    }
    snprintf(line, sizeof(line), "[%c][%s]", letter, tag != nullptr ? tag : "");
  }
  strip_ansi(line);
  log_buffer_add(line);
}
#endif

void JC3248Media::setup() {
  // The core brings up the panel, touch, card and USB drive, then scans the
  // playlist before the API server starts accepting clients.
  jc3248_core_init();
  jc3248_core_start_browser();
  this->files_generation_ = files_generation();
  // Seed the run-time counters so the first published value covers a real
  // interval instead of returning zero.
  this->cpu_load();
}

void JC3248Media::loop() {
#ifdef USE_LOG_LISTENERS
  // Mirror every ESPHome log line into the ring served by /logs.
  if (!this->log_registered_ && logger::global_logger != nullptr) {
    logger::global_logger->add_log_callback(nullptr, log_to_ring);
    this->log_registered_ = true;
  }
#endif

  // esp_netif_init() runs in ESPHome's network component, which may finish
  // after the first loop iterations. Retry until the socket layer is ready;
  // giving up after one failed attempt left the upload page dead whenever a
  // late network component (the web server) changed the init order.
  if (this->upload_port_ != 0 && !this->http_started_) {
    uint32_t now = millis();
    if (now - this->upload_retry_ms_ >= 3000) {
      this->upload_retry_ms_ = now;
      http_upload_config_t cfg;
      memset(&cfg, 0, sizeof(cfg));
      cfg.port = this->upload_port_;
      cfg.username = this->upload_username_.c_str();
      cfg.password = this->upload_password_.c_str();
      cfg.max_upload_bytes = this->max_upload_bytes_;
      esp_err_t err = http_upload_start(&cfg);
      if (err == ESP_OK) {
        this->http_started_ = true;
        ESP_LOGI(TAG, "upload server listening on port %u", (unsigned)this->upload_port_);
      } else {
        ESP_LOGW(TAG, "upload server unavailable (will retry): %s",
                 esp_err_to_name(err));
      }
    }
  }

  uint32_t generation = files_generation();
  if (generation != this->files_generation_) {
    this->files_generation_ = generation;
    bool changed = false;
    if (this->select_ != nullptr && this->select_->refresh_options()) {
      changed = true;
    }
    if (this->delete_select_ != nullptr && this->delete_select_->refresh_options()) {
      changed = true;
    }
    if (changed) {
      // ESPHome sends select options only with ListEntitiesSelectResponse.
      // Drop the API clients so Home Assistant re-reads the entity list and
      // the new files appear in the dropdown without a manual reload.
      this->disconnect_api_clients_();
    }
  }

  char path[FILE_PATH_MAX];
  bool playing = app_control_get_playing(path, sizeof(path));
  if (this->media_player_ != nullptr) {
    this->media_player_->set_playing(playing);
  }
  if (playing) {
    if (this->current_file_ != path) {
      this->current_file_ = path;
      if (this->select_ != nullptr && this->select_->has_option(path)) {
        this->select_->publish_state(path);
      }
    }
  } else if (!this->current_file_.empty()) {
    this->current_file_.clear();
  }

  bool browser_color = app_control_get_color_mode();
  if (browser_color != this->browser_color_mode_) {
    this->browser_color_mode_ = browser_color;
    if (!browser_color && this->color_on_ && this->color_light_ != nullptr) {
      // A touch on the filled screen leaves colour mode; mirror that on the
      // light entity so Home Assistant does not keep showing it as on.
      this->color_on_ = false;
      this->color_light_->turn_off().perform();
    }
  }
}

void JC3248Media::dump_config() {
  ESP_LOGCONFIG(TAG, "JC3248W535 media player");
  ESP_LOGCONFIG(TAG, "  Upload server port: %u", (unsigned)this->upload_port_);
  ESP_LOGCONFIG(TAG, "  Upload auth: %s", this->upload_username_.empty() ? "disabled" : "basic");
  ESP_LOGCONFIG(TAG, "  Brightness: %.0f%%", this->brightness_);
  ESP_LOGCONFIG(TAG, "  Files: %d", files_count());
}

std::string JC3248Media::first_file_() {
  std::string out;
  if (files_lock(1000)) {
    const char *path = files_path(0);
    if (path != nullptr) {
      out = path;
    }
    files_unlock();
  }
  return out;
}

void JC3248Media::play_selected() {
  std::string path = this->selected_path_;
  if (path.empty()) {
    path = this->first_file_();
  }
  if (path.empty()) {
    ESP_LOGW(TAG, "play: %s", PLAYABLE_HINT);
    return;
  }
  this->play_path(path);
}

void JC3248Media::play_path(const std::string &path) {
  if (path.empty()) {
    return;
  }
  ESP_LOGI(TAG, "HA: play %s", path.c_str());
  this->selected_path_ = path;
  this->wake_for_playback_();

  app_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = APP_CMD_PLAY_FILE;
  snprintf(cmd.path, sizeof(cmd.path), "%s", path.c_str());
  if (!app_control_post(&cmd, 0)) {
    ESP_LOGW(TAG, "command queue full, play ignored");
    return;
  }
  if (this->select_ != nullptr && this->select_->has_option(path.c_str())) {
    this->select_->publish_state(path.c_str());
  }
}

void JC3248Media::start_slideshow() {
  std::string path = this->selected_path_;
  if (path.empty()) {
    path = this->first_file_();
  }
  if (path.empty()) {
    ESP_LOGW(TAG, "slideshow: %s", PLAYABLE_HINT);
    return;
  }
  ESP_LOGI(TAG, "HA: slideshow from %s", path.c_str());
  this->selected_path_ = path;
  this->wake_for_playback_();

  app_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = APP_CMD_PLAY_ALL;
  snprintf(cmd.path, sizeof(cmd.path), "%s", path.c_str());
  if (!app_control_post(&cmd, 0)) {
    ESP_LOGW(TAG, "command queue full, slideshow ignored");
  }
}

void JC3248Media::next() {
  ESP_LOGI(TAG, "HA: next");
  this->wake_for_playback_();
  app_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = APP_CMD_NEXT;
  app_control_post(&cmd, 0);
}

void JC3248Media::previous() {
  ESP_LOGI(TAG, "HA: previous");
  this->wake_for_playback_();
  app_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = APP_CMD_PREV;
  app_control_post(&cmd, 0);
}

void JC3248Media::stop() {
  ESP_LOGI(TAG, "HA: stop");
  app_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = APP_CMD_STOP;
  app_control_post(&cmd, 0);
}

void JC3248Media::rescan() {
  ESP_LOGI(TAG, "HA: rescan files");
  app_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = APP_CMD_RESCAN;
  app_control_post(&cmd, 0);
}

void JC3248Media::show_browser() {
  ESP_LOGI(TAG, "HA: show browser");
  app_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = APP_CMD_SHOW_BROWSER;
  app_control_post(&cmd, 0);
}

void JC3248Media::set_delete_path(const std::string &path) {
  this->delete_path_ = path;
  ESP_LOGI(TAG, "HA: delete target %s", path.c_str());
}

void JC3248Media::delete_selected() {
  if (this->delete_path_.empty()) {
    ESP_LOGW(TAG, "HA: delete skipped, no delete target selected");
    return;
  }
  ESP_LOGI(TAG, "HA: delete %s", this->delete_path_.c_str());
  app_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = APP_CMD_DELETE_FILE;
  snprintf(cmd.path, sizeof(cmd.path), "%s", this->delete_path_.c_str());
  if (!app_control_post(&cmd, 0)) {
    ESP_LOGW(TAG, "command queue full, delete ignored");
  }
}

void JC3248Media::wake_for_playback_() {
  if (this->sleeping_) {
    this->set_sleep(false);
  }
}

void JC3248Media::set_sleep(bool sleeping) {
  ESP_LOGI(TAG, "HA: sleep %s (was %s)", sleeping ? "on" : "off",
           this->sleeping_ ? "on" : "off");
  if (this->sleeping_ == sleeping) {
    this->publish_sleep_state(sleeping);
    return;
  }
  this->sleeping_ = sleeping;

  app_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = APP_CMD_SLEEP;
  cmd.on = sleeping;
  app_control_post(&cmd, 0);

  if (sleeping) {
    // Only the backlight and playback are stopped. The panel and the CPU
    // frequency are deliberately left alone: DISPOFF/DISPON and runtime PM
    // changes proved unreliable on this panel and could leave the device
    // unreachable after wake. Wi-Fi/API stay up for the wake switch.
    display_set_backlight_percent(0);
  }
  this->publish_sleep_state(sleeping);
}

void JC3248Media::set_brightness(float percent) {
  ESP_LOGI(TAG, "HA: brightness %.0f%%", percent);
  if (percent < 0.0f) {
    percent = 0.0f;
  } else if (percent > 100.0f) {
    percent = 100.0f;
  }
  this->brightness_ = percent;
  app_control_set_backlight((uint8_t)lroundf(percent));
  if (!this->sleeping_) {
    display_set_backlight_percent((uint8_t)lroundf(percent));
  }
  this->publish_brightness_state(percent);
}

void JC3248Media::set_screen_color(bool on, float red, float green, float blue) {
  ESP_LOGI(TAG, "HA: screen colour %s rgb=%.2f,%.2f,%.2f", on ? "on" : "off", red, green,
           blue);
  if (on) {
    if (this->sleeping_) {
      this->set_sleep(false);
    }
    auto to5 = [](float value) {
      int v = (int)lroundf(value * 31.0f);
      return (uint16_t)(v < 0 ? 0 : (v > 31 ? 31 : v));
    };
    auto to6 = [](float value) {
      int v = (int)lroundf(value * 63.0f);
      return (uint16_t)(v < 0 ? 0 : (v > 63 ? 63 : v));
    };
    uint16_t color = (uint16_t)((to5(red) << 11) | (to6(green) << 5) | to5(blue));

    app_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = APP_CMD_SET_COLOR;
    cmd.on = true;
    cmd.color = color;
    if (!app_control_post(&cmd, 0)) {
      ESP_LOGW(TAG, "command queue full, colour ignored");
      return;
    }
    this->color_on_ = true;
  } else if (this->color_on_) {
    app_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = APP_CMD_SET_COLOR;
    cmd.on = false;
    app_control_post(&cmd, 0);
    this->color_on_ = false;
  }
}

float JC3248Media::cpu_load() {
  static uint32_t previous_total = 0;
  static uint32_t previous_idle = 0;
  TaskStatus_t tasks[40];
  UBaseType_t count = uxTaskGetSystemState(tasks, 40, nullptr);
  uint32_t total = 0;
  uint32_t idle = 0;
  for (UBaseType_t i = 0; i < count; i++) {
    total += tasks[i].ulRunTimeCounter;
    const char *name = tasks[i].pcTaskName;
    if (name != nullptr && strncmp(name, "IDLE", 4) == 0) {
      idle += tasks[i].ulRunTimeCounter;
    }
  }
  if (previous_total == 0 || total < previous_total || idle < previous_idle) {
    previous_total = total;
    previous_idle = idle;
    return 0.0f;
  }
  uint32_t total_delta = total - previous_total;
  uint32_t idle_delta = idle - previous_idle;
  previous_total = total;
  previous_idle = idle;
  if (total_delta == 0) {
    return 0.0f;
  }
  float load = 100.0f * (1.0f - (float)idle_delta / (float)total_delta);
  if (load < 0.0f) {
    return 0.0f;
  }
  return load > 100.0f ? 100.0f : load;
}

bool JC3248Media::is_playing() const {
  char path[FILE_PATH_MAX];
  return app_control_get_playing(path, sizeof(path));
}

void JC3248Media::disconnect_api_clients_() {
#ifdef USE_API
  if (api::global_api_server == nullptr || !api::global_api_server->is_connected()) {
    return;
  }
  size_t count = 0;
  for (auto &connection : api::global_api_server->active_clients()) {
    connection->on_disconnect_request(api::DisconnectRequest{});
    count++;
  }
  if (count > 0) {
    ESP_LOGI(TAG, "file list changed, asking %u API client(s) to reconnect",
             (unsigned)count);
  }
#endif
}

// ---------------------------------------------------------------------------
// Child entities
// ---------------------------------------------------------------------------

void JC3248Select::setup() {
  if (this->play_on_select_) {
    this->parent_->set_select(this);
  } else {
    this->parent_->set_delete_select(this);
  }
  this->refresh_options();
}

void JC3248Select::dump_config() {
  ESP_LOGCONFIG(TAG, "  Select options: %u", (unsigned)this->options_count_);
}

bool JC3248Select::refresh_options() {
  if (this->storage_ == nullptr) {
    this->storage_ = (char *)heap_caps_malloc((size_t)FILES_MAX * FILE_PATH_MAX,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (this->storage_ == nullptr) {
      ESP_LOGE(TAG, "no PSRAM for the select options");
      return false;
    }
  }
  if (!files_lock(1000)) {
    return false;
  }

  int count = files_count();
  if (count > FILES_MAX) {
    count = FILES_MAX;
  }
  uint32_t hash = 2166136261u;
  for (int i = 0; i < count; i++) {
    const char *path = files_path(i);
    if (path == nullptr) {
      continue;
    }
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
      hash = (hash ^ *p) * 16777619u;
    }
    hash = (hash ^ '\n') * 16777619u;
  }

  bool changed = (count != (int)this->options_count_) || (hash != this->options_hash_);
  if (changed) {
    for (int i = 0; i < count; i++) {
      const char *path = files_path(i);
      snprintf(this->storage_ + (size_t)i * FILE_PATH_MAX, FILE_PATH_MAX, "%s",
               path != nullptr ? path : "");
    }
    this->options_count_ = (size_t)count;
    this->options_hash_ = hash;
  }
  files_unlock();

  if (!changed) {
    return false;
  }

  FixedVector<const char *> options;
  options.init((size_t)count);
  for (int i = 0; i < count; i++) {
    options.push_back(this->storage_ + (size_t)i * FILE_PATH_MAX);
  }
  this->traits.set_options(options);

  if (count > 0 && (!this->has_state() || !this->has_option(this->current_option().c_str()))) {
    this->publish_state((size_t)0);
  }
  ESP_LOGI(TAG, "select now has %d option(s)", count);
  return true;
}

void JC3248Select::control(size_t index) {
  const char *path = this->option_at(index);
  if (path == nullptr) {
    return;
  }
  ESP_LOGI(TAG, "HA: select %s", path);
  this->publish_state(index);
  if (this->play_on_select_) {
    this->parent_->play_path(path);
  } else {
    this->parent_->set_delete_path(path);
  }
}

void JC3248SleepSwitch::setup() {
  this->parent_->set_sleep_switch(this);
  this->publish_state(this->parent_->is_sleeping());
}

void JC3248SleepSwitch::write_state(bool state) {
  this->parent_->set_sleep(state);
  this->publish_state(state);
}

void JC3248BrightnessNumber::setup() {
  this->parent_->set_brightness_number(this);
  this->parent_->set_brightness(this->initial_value_);
}

void JC3248BrightnessNumber::control(float value) {
  this->parent_->set_brightness(value);
}

void JC3248MediaPlayer::setup() {
  this->parent_->set_media_player(this);
  this->state = media_player::MEDIA_PLAYER_STATE_IDLE;
  this->volume = 1.0f;
  this->publish_state();
}

void JC3248MediaPlayer::dump_config() {
  ESP_LOGCONFIG(TAG, "Media player entity");
}

media_player::MediaPlayerTraits JC3248MediaPlayer::get_traits() {
  media_player::MediaPlayerTraits traits;
  // Only the transport controls the device actually implements. The base
  // features (media browsing, volume, announcements) would add unusable
  // buttons and sliders to the Home Assistant card.
  traits.clear_feature_flags(media_player::BASE_MEDIA_PLAYER_FEATURES);
  traits.add_feature_flags(media_player::MediaPlayerEntityFeature::PLAY |
                           media_player::MediaPlayerEntityFeature::STOP |
                           media_player::MediaPlayerEntityFeature::NEXT_TRACK |
                           media_player::MediaPlayerEntityFeature::PREVIOUS_TRACK);
  return traits;
}

void JC3248MediaPlayer::set_playing(bool playing) {
  auto state = playing ? media_player::MEDIA_PLAYER_STATE_PLAYING
                       : media_player::MEDIA_PLAYER_STATE_IDLE;
  if (this->state != state) {
    this->state = state;
    this->publish_state();
  }
}

void JC3248MediaPlayer::control(const media_player::MediaPlayerCall &call) {
  if (!call.get_command().has_value()) {
    return;
  }
  ESP_LOGI(TAG, "HA: media_player command %d", (int)call.get_command().value());
  switch (call.get_command().value()) {
    case media_player::MEDIA_PLAYER_COMMAND_PLAY:
      this->parent_->play_selected();
      this->set_playing(true);
      break;
    case media_player::MEDIA_PLAYER_COMMAND_STOP:
      this->parent_->stop();
      this->set_playing(false);
      break;
    case media_player::MEDIA_PLAYER_COMMAND_NEXT:
      this->parent_->next();
      this->set_playing(true);
      break;
    case media_player::MEDIA_PLAYER_COMMAND_PREVIOUS:
      this->parent_->previous();
      this->set_playing(true);
      break;
    default:
      break;
  }
}

light::LightTraits JC3248ScreenColor::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::RGB});
  return traits;
}

void JC3248ScreenColor::setup_state(light::LightState *state) {
  this->state_ = state;
  this->parent_->set_color_light(state);
}

void JC3248ScreenColor::write_state(light::LightState *state) {
  if (state->current_values.is_on()) {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    state->current_values_as_rgb(&red, &green, &blue);
    this->parent_->set_screen_color(true, red, green, blue);
  } else {
    this->parent_->set_screen_color(false, 0.0f, 0.0f, 0.0f);
  }
}

}  // namespace jc3248_media
}  // namespace esphome
