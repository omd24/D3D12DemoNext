#pragma once

/******************************************************************************
 * \n-body simulation using async compute
 * \reference:
 *https://docs.microsoft.com/en-us/windows/win32/direct3d12/multi-engine-n-body-gravity-simulation
 * \
 ******************************************************************************/

#include "DemoUtils.hpp"

//---------------------------------------------------------------------------//
void onInit(void);
//---------------------------------------------------------------------------//
void onDestroy(void);
//---------------------------------------------------------------------------//
void onUpdate(void);
//---------------------------------------------------------------------------//
void onRender(void);
//---------------------------------------------------------------------------//
void onKeyDown(UINT8);
//---------------------------------------------------------------------------//
void onKeyUp(UINT8);
//---------------------------------------------------------------------------//
inline constexpr void registerCallbacks() {
  g_FuncReg.onInit = onInit;
  g_FuncReg.onDestroy = onDestroy;
  g_FuncReg.onUpdate = onUpdate;
  g_FuncReg.onRender = onRender;
  g_FuncReg.onKeyDown = onKeyDown;
  g_FuncReg.onKeyUp = onKeyUp;
}
//---------------------------------------------------------------------------//
struct ParticleSimulationData;
ParticleSimulationData* g_ParticleSimData = nullptr;
//---------------------------------------------------------------------------//
