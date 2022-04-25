#include "AppMgr.hpp"

_Use_decl_annotations_ int WINAPI
WinMain(HINSTANCE p_Instance, HINSTANCE, LPSTR, int p_CmdShow) {
  void * demoMem = reinterpret_cast<void*>(::malloc(sizeof(*g_Demo)));
  g_Demo = new(demoMem) Demo;
  demoInit(1280, 720, L"D3D12 n-Body Gravity Simulation");
  return appMgrRun(p_Instance, p_CmdShow);
}
