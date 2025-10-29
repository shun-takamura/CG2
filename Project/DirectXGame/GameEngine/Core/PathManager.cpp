#include "PathManager.h"
#include <filesystem>
#include <iostream>

const std::wstring& PathManager::GetResourceDir() {
    
    static const std::wstring path =L"Resources/";
    return path;
}

const std::wstring& PathManager::GetShaderDir() {  
   // Resourcesの後ろにShadersを連結 
   static const std::wstring path = GetResourceDir()+ L"Shaders/";  
   return path;  
}