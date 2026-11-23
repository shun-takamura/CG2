#pragma once
#include <string>
#include <format>
#include <Windows.h>

/// <summary>ワイド文字列（UTF-16）を UTF-8 の文字列にする</summary>
/// <param name="str">ワイド文字列</param>
/// <returns>UTF-8 の文字列（空なら空）</returns>
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

/// <summary>UTF-8 の文字列をワイド文字列（UTF-16）にする</summary>
/// <param name="str">UTF-8 の文字列</param>
/// <returns>ワイド文字列（空なら空）</returns>
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
