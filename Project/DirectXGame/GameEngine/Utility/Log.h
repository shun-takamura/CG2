#pragma once
#include <string>

/// <summary>デバッガの出力ウィンドウに出し、session.log にも Info で残す</summary>
/// <param name="message">出す文字列（末尾の改行はあってもよい）</param>
void Log(const std::string& message);