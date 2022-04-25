#pragma once

#include "DemoUtils.hpp"

//---------------------------------------------------------------------------//
// Demo context:
//---------------------------------------------------------------------------//
struct Demo {
  // Viewport dimensions
  UINT m_Width;
  UINT m_Height;
  float m_AspectRatio;

  // Adapter info
  bool m_UseWarpDevice;

  // Root assets path
  std::wstring m_AssetsPath;

  // Window title
  std::wstring m_Title;

  // State info
  bool m_IsInitialized;

  // Additional data goes here:
  //
};
Demo* g_Demo = nullptr;
//---------------------------------------------------------------------------//
inline void demoInit(UINT p_Width, UINT p_Height, std::wstring p_Name) {
  g_Demo->m_Width = p_Width;
  g_Demo->m_Height = p_Height;
  g_Demo->m_Title = p_Name;
  g_Demo->m_UseWarpDevice = false;

  WCHAR assetsPath[512];
  getAssetsPath(assetsPath, _countof(assetsPath));
  g_Demo->m_AssetsPath = assetsPath;

  g_Demo->m_AspectRatio =
      static_cast<float>(p_Width) / static_cast<float>(p_Height);

  g_Demo->m_IsInitialized = true;
}
//---------------------------------------------------------------------------//
inline void demoUpdate() {}
inline void demoRender() {}
inline void demoDestroy() {}
inline void demoKeyDown(UINT8) {}
inline void demoKeyUp(UINT8) {}
//---------------------------------------------------------------------------//
inline void
demoParseCommandLineArgs(_In_reads_(p_Argc) WCHAR* p_Argv[], int p_Argc) {
  for (int i = 1; i < p_Argc; ++i) {
    if (_wcsnicmp(p_Argv[i], L"-warp", wcslen(p_Argv[i])) == 0 ||
        _wcsnicmp(p_Argv[i], L"/warp", wcslen(p_Argv[i])) == 0) {
      g_Demo->m_UseWarpDevice = true;
      g_Demo->m_Title = g_Demo->m_Title + L" (WARP)";
    }
  }
}
//---------------------------------------------------------------------------//
inline std::wstring demoGetAssetFullPath(LPCWSTR p_AssetName) {
  return g_Demo->m_AssetsPath + p_AssetName;
}
//---------------------------------------------------------------------------//
inline void demoSetCustomWindowText(LPCWSTR p_Text) {
  std::wstring windowText = g_Demo->m_Title + L": " + p_Text;
  SetWindowText(g_WinHandle, windowText.c_str());
}
//---------------------------------------------------------------------------//
