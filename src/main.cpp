#include <Arduino.h>

extern "C" {
  int entry();
}

extern void ble_gamepad_setup();
extern void ble_gamepad_loop(void*);

uint32_t cpu_frequency;

void setup()
{
  cpu_frequency = getCpuFrequencyMhz();
  ble_gamepad_setup();
  xTaskCreatePinnedToCore(ble_gamepad_loop, "BLE Gamepad connect", 3 * 1024, NULL, 1, NULL, 0);
  entry();
}

void loop()
{
  delay(1000);
}
