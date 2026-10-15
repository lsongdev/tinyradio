#include "board.h"
#include "pins.h"
#include "drivers/si5351/si5351.h"
#include "drivers/xl9555/xl9555.h"
#include "hal/button.h"
#include "hal/encoder.h"
#include "hal/i2c.h"
#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <soc/rtc_cntl_reg.h>

static hal_encoder_t encoder;
static hal_button_t button;
// Bound each bus transaction independently of the SI4732 command-level CTS deadline.
static hal_i2c_t i2c = {0, 20};
static xl9555_t expander;
static si5351_t clock_generator;
static bool reference_ready = false;

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
void board_begin(void) {
  take_power_latch();
  hal_i2c_begin(&i2c, PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  expander.bus = hal_i2c_bus(&i2c); expander.address = EXPANDER_ADDRESS;
  clock_generator.bus = hal_i2c_bus(&i2c); clock_generator.address = CLOCK_ADDRESS;
  board_speaker_set_enabled(false);
  pinMode(PIN_BATTERY, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY, ADC_11db);
  hal_encoder_begin(&encoder, PIN_ENCODER_A, PIN_ENCODER_B, false);
  hal_button_begin(&button, PIN_ENCODER_BUTTON, false);
  pinMode(PIN_DOWN, INPUT_PULLUP);
}

void board_audio_set_muted(bool muted) { (void)muted; }
void board_speaker_set_enabled(bool enabled) {
  // Select the analog route before releasing SHUTDOWN; shut down before anything else on disable.
  if (enabled) xl9555_set_outputs(&expander, 1, EXPANDER_ANALOG_AUDIO, EXPANDER_ANALOG_AUDIO);
  xl9555_set_outputs(&expander, 1, EXPANDER_AMP_SHUTDOWN, enabled ? 0 : EXPANDER_AMP_SHUTDOWN);
}

hal_battery_reading_t board_battery_read(void) {
  uint32_t sum = 0;
  for (int i = 0; i < 32; ++i) sum += analogReadMilliVolts(PIN_BATTERY);
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

void board_power_off(void) {
  board_speaker_set_enabled(false);
  delay(5);
  si5351_disable_outputs(&clock_generator);
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
