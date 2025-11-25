#include "Log.h"
#include <Windows.h>

void Log(const std::string& message) {
    OutputDebugStringA(message.c_str());
}