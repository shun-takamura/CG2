#pragma once
#include <string>

class ConvertStringClass{
public:

	// string->wstring
	std::wstring ConvertString(const std::string& str);

	// wstring->string
	std::string ConvertString(const std::wstring& str);
};

