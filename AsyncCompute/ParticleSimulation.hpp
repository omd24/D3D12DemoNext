#pragma once

/******************************************************************************
 * \n-body simulation using async compute
 * \reference:
 *https://docs.microsoft.com/en-us/windows/win32/direct3d12/multi-engine-n-body-gravity-simulation
 * \
 ******************************************************************************/

#include "DemoUtils.hpp"
#include <directxmath.h>
#include "Externals/d3dx12.h"
#include "Camera.hpp"
#include "Timer.hpp"

using namespace DirectX;

struct ParticleSimCtx;

//---------------------------------------------------------------------------//
void onInit();
//---------------------------------------------------------------------------//
void onDestroy();
//---------------------------------------------------------------------------//
void onUpdate();
//---------------------------------------------------------------------------//
void onRender();
//---------------------------------------------------------------------------//
void onKeyDown(UINT8);
//---------------------------------------------------------------------------//
void onKeyUp(UINT8);
//---------------------------------------------------------------------------//
#define FRAME_COUNT 2
#define THREAD_COUNT 1

struct ParticleSimCtx {
  float m_ParticleSpread;
  UINT m_ParticleCount = 10000;

  // Vertex data (color for now)
  struct ParticleVertex {
    XMFLOAT4 m_Color;
  };

  // Position and velocity data
  struct ParticleMotion {
    XMFLOAT4 m_Position;
    XMFLOAT4 m_Velocity;
  };

  struct CbufferGS {
    XMFLOAT4X4 m_WorldViewProjection;
    XMFLOAT4X4 m_InverseView;

    // Constant buffers are 256-byte aligned in GPU memory
    float padding[32];
  };

  struct CbufferCS {
    UINT m_Param[4];
    float m_ParamFloat[4];
  };

  // Pipeline objects.
  CD3DX12_VIEWPORT m_Viewport;
  CD3DX12_RECT m_ScissorRect;
  IDXGISwapChain3Ptr m_SwapChain;
  ID3D12DevicePtr m_Device;
  ID3D12ResourcePtr m_RenderTargets[FRAME_COUNT];
  UINT m_FrameIndex;
  ID3D12CommandAllocatorPtr m_CommandAllocators[FRAME_COUNT];
  ID3D12CommandQueuePtr m_CommandQueue;
  ID3D12RootSignaturePtr m_RootSignature;
  ID3D12RootSignaturePtr m_ComputeRootSignature;
  ID3D12DescriptorHeapPtr m_RtvHeap;
  ID3D12DescriptorHeapPtr m_SrvUavHeap;
  UINT m_RtvDescriptorSize;
  UINT m_SrvUavDescriptorSize;

  // Asset objects.
  ID3D12PipelineStatePtr m_PipelineState;
  ID3D12PipelineStatePtr m_ComputeState;
  ID3D12GraphicsCommandListPtr m_CommandList;
  ID3D12ResourcePtr m_VertexBuffer;
  ID3D12ResourcePtr m_VertexBufferUpload;
  D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
  ID3D12ResourcePtr m_ParticleBuffer0[THREAD_COUNT];
  ID3D12ResourcePtr m_ParticleBuffer1[THREAD_COUNT];
  ID3D12ResourcePtr m_ParticleBuffer0Upload[THREAD_COUNT];
  ID3D12ResourcePtr m_ParticleBuffer1Upload[THREAD_COUNT];
  ID3D12ResourcePtr m_CbufferGS;
  UINT8* m_CbufferGSData;
  ID3D12ResourcePtr m_CbufferCS;

  UINT m_SrvIndex[THREAD_COUNT]; // Denotes which of the particle buffer
                                 // resource views is the SRV (0 or 1). The UAV
                                 // is 1 - srvIndex.
  UINT m_HeightInstances;
  UINT m_WidthInstances;
  Camera m_Camera;
  Timer m_Timer;

  // Compute objects.
  ID3D12CommandAllocatorPtr m_ComputeAllocator[THREAD_COUNT];
  ID3D12CommandQueuePtr m_ComputeCommandQueue[THREAD_COUNT];
  ID3D12GraphicsCommandListPtr m_ComputeCommandList[THREAD_COUNT];

  // Synchronization objects.
  HANDLE m_SwapChainEvent;
  ID3D12FencePtr m_RenderContextFence;
  UINT64 m_RenderContextFenceValue;
  HANDLE m_RenderContextFenceEvent;
  UINT64 m_FrameFenceValues[FRAME_COUNT];

  ID3D12FencePtr m_ThreadFences[THREAD_COUNT];
  volatile HANDLE m_ThreadFenceEvents[THREAD_COUNT];

  // Thread state.
  LONG volatile m_Terminating;
  UINT64 volatile m_RenderContextFenceValues[THREAD_COUNT];
  UINT64 volatile m_ThreadFenceValues[THREAD_COUNT];

  struct ThreadData {
    ParticleSimCtx* m_Context;
    UINT m_ThreadIndex;
  };
  ThreadData m_ThreadData[THREAD_COUNT];
  HANDLE m_ThreadHandles[THREAD_COUNT];

  // Indices of the root signature parameters.
  enum GraphicsRootParameters : UINT32 {
    GraphicsRootCBV = 0,
    GraphicsRootSRVTable,
    GraphicsRootParametersCount
  };

  enum ComputeRootParameters : UINT32 {
    ComputeRootCBV = 0,
    ComputeRootSRVTable,
    ComputeRootUAVTable,
    ComputeRootParametersCount
  };

  // Indices of shader resources in the descriptor heap.
  enum DescriptorHeapIndex : UINT32 {
    UavParticlePosVelo0 = 0,
    UavParticlePosVelo1 = UavParticlePosVelo0 + THREAD_COUNT,
    SrvParticlePosVelo0 = UavParticlePosVelo1 + THREAD_COUNT,
    SrvParticlePosVelo1 = SrvParticlePosVelo0 + THREAD_COUNT,
    DescriptorCount = SrvParticlePosVelo1 + THREAD_COUNT
  };

  ~ParticleSimCtx(){/* Just release ComPtrs */};
};
//---------------------------------------------------------------------------//
inline ParticleSimCtx* g_Ctx;
//---------------------------------------------------------------------------//
