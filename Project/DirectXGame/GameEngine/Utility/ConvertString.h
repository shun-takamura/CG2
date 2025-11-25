#pragma once
#include <string>
#include <format>
#include <Windows.h>

inline std::string ConvertString(const std::wstring& str)
{
    if (str.empty()) { return std::string(); }

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0,
        str.data(), (int)str.size(),
        nullptr, 0,
        nullptr, nullptr);

    std::string result(sizeNeeded, 0);

    WideCharToMultiByte(CP_UTF8, 0,
        str.data(), (int)str.size(),
        result.data(), sizeNeeded,
        nullptr, nullptr);

    return result;
}

inline std::wstring ConvertString(const std::string& str)
{
    if (str.empty()) { return std::wstring(); }

    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0,
        str.data(), (int)str.size(),
        nullptr, 0);

    std::wstring result(sizeNeeded, 0);

    MultiByteToWideChar(CP_UTF8, 0,
        str.data(), (int)str.size(),
        result.data(), sizeNeeded);

    return result;
}
