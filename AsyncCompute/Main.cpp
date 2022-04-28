#include "appExec.hpp"
#include "ParticleSimulation.hpp"

_Use_decl_annotations_ int WINAPI
WinMain(HINSTANCE p_Instance, HINSTANCE, LPSTR, int p_CmdShow) {
  // Assing function callbacks:
  registerCallbacks();

  // Set up the general demo data
  void* demoMem = reinterpret_cast<void*>(::malloc(sizeof(*g_DemoInfo)));
  DEFER(free_demo_memory) { ::free(demoMem); };
  g_DemoInfo = new (demoMem) DemoInfo;
  demoInit(g_DemoInfo, 1280, 720, L"D3D12 n-Body Gravity Simulation");

  // Run the application:
  return appExec(p_Instance, p_CmdShow, g_FuncReg);
}
