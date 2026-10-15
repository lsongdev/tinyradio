#include "board.h"
#include "pins.h"
#include "drivers/pcf8523/pcf8523.h"
#include "drivers/sht3x/sht3x.h"
#include "drivers/si5351/si5351.h"
#include "drivers/xl9555/xl9555.h"
#include "hal/button.h"
#include "hal/encoder.h"
#include "hal/i2c.h"
#include "services/calendar.h"
#include <Arduino.h>
#include <driver/adc.h>
#include <driver/i2s.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <soc/rtc_cntl_reg.h>

static hal_encoder_t encoder;
static hal_button_t button, keys[3];
static const struct { int pin; bool pull_up; } key_pins[3] = {{PIN_KEY_A, false}, {PIN_KEY_B, true}, {PIN_KEY_C, true}};
// Bound each bus transaction independently of the SI4732 command-level CTS deadline.
static hal_i2c_t i2c = {0, 20};
static xl9555_t expander;
static si5351_t clock_generator;
static bool reference_ready = false;
static pcf8523_t rtc;
static sht3x_t environment;
static bool measuring = false, measured = false;
enum { AIR_IF_HZ = 10700000 };
enum route { ROUTE_UNKNOWN, ROUTE_DIRECT, ROUTE_LNA, ROUTE_AIR };
static enum route route = ROUTE_UNKNOWN;
static uint32_t lo_hz = 0;
enum { AUDIO_BLOCK = 256 };
static bool audio_sampling = false, audio_discard = true;
static uint32_t measurement_started = 0;

static void take_power_latch(void) {
  // A button-held battery start sags the rail; a brownout reset would drop
  // the latch before it is taken. The vendor V5A firmware disables it too.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  // Wake from deep sleep leaves these pads in RTC mode.
  rtc_gpio_hold_dis(PIN_POWER_HOLD);
  rtc_gpio_deinit(PIN_POWER_HOLD);
  digitalWrite(PIN_POWER_HOLD, HIGH);
  pinMode(PIN_POWER_HOLD, OUTPUT);
  rtc_gpio_deinit(PIN_ENCODER_BUTTON);
}
// I2S0 clocks ADC1 by DMA in its built-in ADC mode; no I2S pins are used.
// The battery is on ADC1 too, so board_battery_read() pauses sampling.
static void audio_begin(void) {
  const i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
    .sample_rate = BOARD_AUDIO_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = AUDIO_BLOCK,
  };
  if (i2s_driver_install(I2S_NUM_0, &config, 0, NULL) != ESP_OK) return;
  if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK || adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_12) != ESP_OK ||
      i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_6) != ESP_OK || i2s_adc_enable(I2S_NUM_0) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }
  audio_sampling = true; audio_discard = true;
}
static void audio_pause(void) { if (audio_sampling) i2s_adc_disable(I2S_NUM_0); }
static void audio_resume(void) {
  if (!audio_sampling) return;
  i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_6);
  i2s_adc_enable(I2S_NUM_0);
  audio_discard = true; // The first block after resuming mixes channels.
}
bool board_audio_read(uint16_t* samples, size_t count) {
  size_t bytes = 0;
  if (!audio_sampling || count > AUDIO_BLOCK ||
      i2s_read(I2S_NUM_0, samples, count * sizeof *samples, &bytes, 0) != ESP_OK || bytes != count * sizeof *samples)
    return false;
  if (audio_discard) { audio_discard = false; return false; }
  for (size_t i = 0; i < count; ++i) samples[i] &= 0x0FFF; // Upper bits carry the channel number.
  return true;
}
void board_begin(void) {
  take_power_latch();
  hal_i2c_begin(&i2c, PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  expander.bus = hal_i2c_bus(&i2c); expander.address = EXPANDER_ADDRESS;
  clock_generator.bus = hal_i2c_bus(&i2c); clock_generator.address = CLOCK_ADDRESS;
  rtc.bus = hal_i2c_bus(&i2c); environment.bus = hal_i2c_bus(&i2c);
  board_speaker_set_enabled(false);
  pinMode(PIN_BATTERY, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY, ADC_11db);
  audio_begin();
  hal_encoder_begin(&encoder, PIN_ENCODER_A, PIN_ENCODER_B, false);
  hal_button_begin(&button, PIN_ENCODER_BUTTON, false);
  for (int i = 0; i < 3; ++i) hal_button_begin(&keys[i], key_pins[i].pin, key_pins[i].pull_up);
}

void board_audio_set_muted(bool muted) { (void)muted; }
void board_speaker_set_enabled(bool enabled) {
  // Select the analog route before releasing SHUTDOWN; shut down before anything else on disable.
  if (enabled) xl9555_set_outputs(&expander, 1, EXPANDER_ANALOG_AUDIO, EXPANDER_ANALOG_AUDIO);
  xl9555_set_outputs(&expander, 1, EXPANDER_AMP_SHUTDOWN, enabled ? 0 : EXPANDER_AMP_SHUTDOWN);
}

hal_battery_reading_t board_battery_read(void) {
  uint32_t sum = 0;
  audio_pause();
  for (int i = 0; i < 32; ++i) sum += analogReadMilliVolts(PIN_BATTERY);
  audio_resume();
  const uint32_t mv = (sum + 16) / 32;
  const float volts = mv * BATTERY_DIVIDER * BATTERY_CALIBRATION / 1000.0f;
  int percent = (int)((volts - 3.2f) * 100.0f);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  const hal_battery_reading_t reading = {mv, volts, (uint8_t)percent, volts > 4.35f, volts >= 2.5f && volts <= 5.5f};
  return reading;
}

int board_encoder_read(void) { return hal_encoder_read(&encoder); }
bool board_button_update(uint32_t now) { return hal_button_update(&button, now); }
bool board_button_pressed(void) { return button.pressed; }
uint8_t board_keys_read(uint32_t now, uint8_t* repeats) {
  enum { REPEAT_DELAY_MS = 700, REPEAT_INTERVAL_MS = 150 };
  uint8_t pressed = 0;
  *repeats = 0;
  for (int i = 0; i < 3; ++i) {
    if (hal_button_update(&keys[i], now)) pressed |= (uint8_t)(1 << i);
    if (hal_button_repeat(&keys[i], now, REPEAT_DELAY_MS, REPEAT_INTERVAL_MS)) *repeats |= (uint8_t)(1 << i);
  }
  return pressed;
}

bool board_radio_begin(void) {
  // The Si5351 keeps its registers while powered, so this is safe to repeat.
  reference_ready = si5351_start_clk1_32768(&clock_generator);
  return reference_ready;
}
void board_radio_set_powered(bool powered) { (void)powered; }
void board_radio_reset(void) {
  if (!reference_ready) board_radio_begin();
  xl9555_set_outputs(&expander, 0, EXPANDER_RADIO_RESET, EXPANDER_RADIO_RESET);
  xl9555_update(&expander, XL9555_OUTPUT0, EXPANDER_RADIO_RESET, 0); delay(10);
  xl9555_update(&expander, XL9555_OUTPUT0, EXPANDER_RADIO_RESET, EXPANDER_RADIO_RESET); delay(100);
}
i2c_bus_t board_radio_bus(void) { return hal_i2c_bus(&i2c); }

static enum route route_for(uint32_t rf_hz) {
  return rf_hz >= 110000000 ? ROUTE_AIR : rf_hz >= 2000000 ? ROUTE_LNA : ROUTE_DIRECT;
}
uint32_t board_frontend_receiver_hz(uint32_t rf_hz) { return route_for(rf_hz) == ROUTE_AIR ? AIR_IF_HZ : rf_hz; }
bool board_frontend_set(uint32_t rf_hz) {
  const enum route next = route_for(rf_hz);
  if (next != route) {
    const bool air = next == ROUTE_AIR, lna = next != ROUTE_DIRECT;
    route = ROUTE_UNKNOWN;
    if (!xl9555_set_outputs(&expander, 0, EXPANDER_RF_MIXER | EXPANDER_RF_LNA,
                            (air ? EXPANDER_RF_MIXER : 0) | (lna ? EXPANDER_RF_LNA : 0)) ||
        !xl9555_set_outputs(&expander, 1, EXPANDER_MIXER_POWER | EXPANDER_LNA_POWER,
                            (air ? EXPANDER_MIXER_POWER : 0) | (lna ? EXPANDER_LNA_POWER : 0))) return false;
    if (!air && lo_hz) { if (!si5351_disable_clk0(&clock_generator)) return false; lo_hz = 0; }
    if (air) delay(10); // Let the SA602 supply settle before locking the LO.
    route = next;
  }
  if (next != ROUTE_AIR || lo_hz == rf_hz - AIR_IF_HZ) return true;
  // Low-side injection: LO = RF - IF.
  lo_hz = 0;
  if (!si5351_set_clk0(&clock_generator, rf_hz - AIR_IF_HZ)) return false;
  lo_hz = rf_hz - AIR_IF_HZ;
  return true;
}
static void frontend_off(void) {
  xl9555_set_outputs(&expander, 1, EXPANDER_MIXER_POWER | EXPANDER_LNA_POWER, 0);
  route = ROUTE_UNKNOWN; lo_hz = 0;
}
bool board_rtc_read(int64_t* utc_seconds) {
  pcf8523_time_t t;
  bool valid;
  if (!pcf8523_read(&rtc, &t, &valid) || !valid) return false;
  *utc_seconds = calendar_days(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
  return true;
}
bool board_rtc_write(int64_t utc_seconds) {
  const int64_t days = calendar_day_of(utc_seconds), second_of_day = utc_seconds - days * 86400;
  int64_t year;
  unsigned month, day;
  calendar_date(days, &year, &month, &day);
  if (year < 2000 || year > 2099) return false;
  const pcf8523_time_t t = {(uint16_t)year, (uint8_t)month, (uint8_t)day,
                            (uint8_t)((days % 7 + 11) % 7), // 1970-01-01 was a Thursday; Sunday = 0.
                            (uint8_t)(second_of_day / 3600), (uint8_t)(second_of_day / 60 % 60), (uint8_t)(second_of_day % 60)};
  return pcf8523_write(&rtc, &t);
}
bool board_environment_poll(uint32_t now, hal_environment_reading_t* reading) {
  enum { CONVERSION_MS = 20, INTERVAL_MS = 5000 };
  if (measuring) {
    if (now - measurement_started < CONVERSION_MS) return false;
    measuring = false;
    reading->valid = sht3x_fetch(&environment, &reading->milli_celsius, &reading->milli_percent);
    return true;
  }
  if (measured && now - measurement_started < INTERVAL_MS) return false;
  measured = true; measurement_started = now;
  if (sht3x_start(&environment)) { measuring = true; return false; }
  reading->valid = false;
  return true;
}

void board_power_off(void) {
  board_speaker_set_enabled(false);
  delay(5);
  si5351_disable_outputs(&clock_generator);
  frontend_off();
  audio_pause();
  board_display_sleep();
  esp_sleep_enable_ext0_wakeup(PIN_ENCODER_BUTTON, 0);
  digitalWrite(PIN_POWER_HOLD, LOW);
  // On battery, stay awake after releasing the latch: active current drains
  // the rail to a clean cutoff. Deep sleep would leave it decaying at uA and
  // the next key press finds a half-powered chip instead of a cold start.
  const uint32_t start = millis();
  while (millis() - start < 2000) {}
  // Still running: external power keeps the MCU alive. Hold the latch low and
  // sleep until the encoder key wakes it.
  rtc_gpio_init(PIN_POWER_HOLD);
  rtc_gpio_set_direction(PIN_POWER_HOLD, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level(PIN_POWER_HOLD, 0);
  rtc_gpio_hold_en(PIN_POWER_HOLD);
  esp_deep_sleep_start();
}
