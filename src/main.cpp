#include <Arduino.h>

extern "C" {
  int entry();
}

void setup()
{
  entry();
}

void loop()
{
  delay(1000);
}
