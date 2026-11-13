#include "matrix.h"
#include "ws2818b.pio.h"
#ifdef TREMO_FREERTOS_ENABLED
#include "display_task.h"
#endif

static npLED_t leds[LED_COUNT];
static PIO np_pio;
static int sm = -1;
static uint initialized_pin;

bool npInit(uint pin) {
  if (sm >= 0) return pin == initialized_pin;
  if (pin >= NUM_BANK0_GPIOS) return false;

  PIO ports[] = {pio0, pio1};
  for (uint i = 0; i < 2; ++i) {
    if (!pio_can_add_program(ports[i], &ws2818b_program)) continue;
    int available_sm = pio_claim_unused_sm(ports[i], false);
    if (available_sm < 0) continue;

    np_pio = ports[i];
    sm = available_sm;
    uint offset = pio_add_program(np_pio, &ws2818b_program);
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
    initialized_pin = pin;
    npClear();
    npWrite();
    return true;
  }
  return false;
}

void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b) {
  if (index >= LED_COUNT) return;
  leds[index] = (npLED_t){.R = r, .G = g, .B = b};
}

void npSetPixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
  if (x >= 5 || y >= 5) return;
  // LED 0 no canto inferior direito; linhas alternam o sentido.
  uint index = (4 - y) * 5 + (y % 2 == 0 ? 4 - x : x);
  npSetLED(index, r, g, b);
}

void npClear(void) {
  for (uint i = 0; i < LED_COUNT; ++i) npSetLED(i, 0, 0, 0);
}

void npWrite(void) {
#ifdef TREMO_FREERTOS_ENABLED
  display_task_matrix(leds);
#else
  matrix_write_frame(leds);
#endif
}

void matrix_write_frame(const npLED_t *pixels) {
  if (sm < 0) return;
  for (uint i = 0; i < LED_COUNT; ++i) {
    uint32_t grb = ((uint32_t)pixels[i].G << 16) |
                   ((uint32_t)pixels[i].R << 8) | pixels[i].B;
    pio_sm_put_blocking(np_pio, sm, grb << 8);
  }
  while (!pio_sm_is_tx_fifo_empty(np_pio, sm)) tight_loop_contents();
  // Permite terminar o último pixel (30 us) e manter o intervalo de reset.
  sleep_us(150);
}
