#pragma once
// GeekNest V5A (classic ESP32, yoRadio mapping). GPIO34-39 are input-only with
// no internal pull-ups; the board supplies them.
enum {
  PIN_POWER_HOLD = 4,          // Latches the battery supply; must go high first.
  PIN_TFT_SCLK = 18, PIN_TFT_MOSI = 19, PIN_TFT_CS = 22, PIN_TFT_DC = 23,
  PIN_BACKLIGHT = 21,
  PIN_KEY_A = 35, PIN_KEY_B = 32, // Side keys, active low.
  PIN_KEY_C = 12,               // Pulled-up key; also the VDD_SDIO strapping pin.
  PIN_ENCODER_A = 38, PIN_ENCODER_B = 37, PIN_ENCODER_BUTTON = 36,
  PIN_BATTERY = 39,             // ADC1 channel 3.
  PIN_AUDIO = 34,               // ADC1 channel 6: SI4732 left audio output.
  PIN_I2C_SDA = 13, PIN_I2C_SCL = 15,
};
#define BATTERY_DIVIDER 2.0f      // R21 = R22 = 100k; BAT_ADC = BAT_IN / 2.
#define BATTERY_CALIBRATION 1.0f

// XL9555 and Si5351 (25 MHz crystal) on the shared I2C bus.
#define EXPANDER_ADDRESS 0x20
#define CLOCK_ADDRESS 0x60
// The PCF8523 RTC (0x68) and SHT30 sensor (0x44) share the same bus.
enum {
  EXPANDER_RADIO_RESET = 0x01,  // Port 0 bit 0: SI4732 RST, active low.
  EXPANDER_AMP_SHUTDOWN = 0x20, // Port 1 bit 5: amplifier SHUTDOWN, active high.
  EXPANDER_ANALOG_AUDIO = 0x40, // Port 1 bit 6: route SI4732 analog audio to the amplifier.
  EXPANDER_RF_MIXER = 0x20,     // Port 0 bit 5: RF switch to the mixer loop (AIR).
  EXPANDER_RF_LNA = 0x40,       // Port 0 bit 6: RF switch to the LNA antenna route.
  EXPANDER_MIXER_POWER = 0x04,  // Port 1 bit 2: SA602 supply.
  EXPANDER_LNA_POWER = 0x10,    // Port 1 bit 4: LNA supply.
};
