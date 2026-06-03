#pragma once

#include <Arduino.h>

namespace app {

void printPrompt();
void printHelp();
void handleCommand(String line);
void handleSerial();

}  // namespace app
