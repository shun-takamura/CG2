#pragma once
#include <string>

class ConvertStringClass{
public:

	/// <summary>UTF-8 の文字列をワイド文字列（UTF-16）にする</summary>
	/// <param name="str">UTF-8 の文字列</param>
	/// <returns>変換した文字列（空・変換失敗なら空）</returns>
	std::wstring ConvertString(const std::string& str);

	/// <summary>ワイド文字列（UTF-16）を UTF-8 の文字列にする</summary>
	/// <param name="str">ワイド文字列</param>
	/// <returns>変換した文字列（空・変換失敗なら空）</returns>
	std::string ConvertString(const std::wstring& str);
};

