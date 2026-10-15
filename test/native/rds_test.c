#include "drivers/si473x/rds.h"
#include "drivers/si473x/settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void ps(rds_decoder_t* r, uint16_t pi, const char* name, uint8_t errors, bool version_b) {
  for (int i = 3; i >= 0; --i)
    rds_decoder_accept(r, pi, (version_b ? 0x800 : 0) | i,
                       (uint16_t)((uint8_t)name[i * 2] << 8 | (uint8_t)name[i * 2 + 1]), errors, 0);
}
static void rt(rds_decoder_t* r, const char* text, bool flag, bool version_b, uint8_t errors) {
  char data[64]; memset(data, ' ', sizeof data);
  const int n = (int)strlen(text), capacity = version_b ? 32 : 64; assert(n <= capacity);
  memcpy(data, text, n); if (n < capacity) data[n] = 13;
  const int width = version_b ? 2 : 4;
  for (int i = 0; i < capacity / width && i <= n / width; ++i) {
    const uint16_t c = (uint16_t)((uint8_t)data[i * width] << 8 | (uint8_t)data[i * width + 1]);
    const uint16_t d = version_b ? c : (uint16_t)((uint8_t)data[i * width + 2] << 8 | (uint8_t)data[i * width + 3]);
    rds_decoder_accept(r, 0x1234, 0x2000 | (version_b ? 0x800 : 0) | (flag ? 0x10 : 0) | i, d, errors, c);
  }
}
int main(void) {
  rds_decoder_t decoder, *r = &decoder; rds_decoder_clear(r);
  ps(r, 0x1234, "RADIO   ", 0, false); assert(!rds_ps(r)[0]);
  ps(r, 0x1234, "RADIO   ", 0, true); assert(!strcmp(rds_ps(r), "RADIO"));
  ps(r, 0x4321, "NEW FM  ", 0, false); assert(!rds_ps(r)[0]);
  ps(r, 0x4321, "NEW FM  ", 0, false); assert(!strcmp(rds_ps(r), "NEW FM"));
  rds_decoder_clear(r);
  ps(r, 0x1234, "BAD FM  ", 0x30, false); ps(r, 0x1234, "BAD FM  ", 3, false); assert(!rds_ps(r)[0]);
  ps(r, 0x1234, "GOOD FM ", 0x51, false); ps(r, 0x1234, "GOOD FM ", 0x51, false); assert(!strcmp(rds_ps(r), "GOOD FM"));
  rds_decoder_clear(r); ps(r, 0x1234, "CORRECT ", 0xaa, false); ps(r, 0x1234, "CORRECT ", 0xaa, false);
  assert(!strcmp(rds_ps(r), "CORRECT")); // BLE=2 is corrected, not uncorrectable.
  rds_decoder_clear(r); rds_decoder_accept(r, 0x1234, 0, 0x4142, 0, 0); rds_decoder_accept(r, 0x1234, 1, 0x4344, 0, 0);
  assert(!rds_ps(r)[0]); rds_decoder_clear(r); ps(r, 0x1234, "RESTORED", 0, false); assert(!rds_ps(r)[0]);
  ps(r, 0x1234, "RESTORED", 0, false); assert(!strcmp(rds_ps(r), "RESTORED"));
  const char* message = "Long radio text scrolls across the main display.";
  rt(r, message, false, false, 0); assert(!rds_radio_text(r)[0]);
  rt(r, message, false, false, 0); assert(!strcmp(rds_radio_text(r), message));
  rt(r, "New programme", true, false, 0); assert(!rds_radio_text(r)[0]);
  rt(r, "New programme", true, false, 0); assert(!strcmp(rds_radio_text(r), "New programme"));
  rt(r, "Version B", true, true, 0); assert(!rds_radio_text(r)[0]);
  rt(r, "Version B", true, true, 0); assert(!strcmp(rds_radio_text(r), "Version B"));
  rds_decoder_clear(r); rt(r, "Bad C", false, false, 0x0c); rt(r, "Bad C", false, false, 0x0c); assert(!rds_radio_text(r)[0]);
  rt(r, "Valid C", false, false, 0); rt(r, "Valid C", false, false, 0); assert(!strcmp(rds_radio_text(r), "Valid C"));
  char full[65]; memset(full, 'X', 64); full[64] = 0;
  rt(r, full, true, false, 0); rt(r, full, true, false, 0); assert(strlen(rds_radio_text(r)) == 64);
  char short_full[33]; memset(short_full, 'Y', 32); short_full[32] = 0;
  rt(r, short_full, false, true, 0); rt(r, short_full, false, true, 0); assert(strlen(rds_radio_text(r)) == 32);
  ps(r, 0x4321, "OTHER FM", 0, false); assert(!rds_radio_text(r)[0]);
  for (int mode = SI473X_FM; mode <= SI473X_USB; ++mode) {
    int count; const si473x_bandwidth_t* bw = si473x_bandwidths((si473x_mode_t)mode, &count);
    for (int i = 1; i < count; ++i) assert(bw[i].hz > bw[i - 1].hz);
  }
  puts("RDS PS/RT tests passed");
  return 0;
}
