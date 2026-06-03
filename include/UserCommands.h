#pragma once

#include <Arduino.h>

namespace app {

void printPrompt();
void printHelp();
void handleCommand(const char* line);
void handleSerial();

}  // namespace app
