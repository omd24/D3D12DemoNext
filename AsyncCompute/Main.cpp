#include "Win32App.hpp"
#include "ParticleSimulation.hpp"

_Use_decl_annotations_ int WINAPI
WinMain(HINSTANCE p_Instance, HINSTANCE, LPSTR, int p_CmdShow) {
  // Set up the general demo data
  void* demoMem = reinterpret_cast<void*>(::malloc(sizeof(*g_DemoInfo)));
  g_DemoInfo = new (demoMem) DemoInfo;
  demoInit(1280, 720, L"D3D12 n-Body Gravity Simulation");

  // Create a registery to pass the callbacks
  static constexpr CallBackRegistery cbReg = {
      .onInit = onInit,
      .onDestroy = onDestroy,
      .onUpdate = onUpdate,
      .onRender = onRender,
      .onKeyDown = onKeyDown,
      .onKeyUp = onKeyUp};

  // Run the application:
  return appExec(p_Instance, p_CmdShow, cbReg);
}
