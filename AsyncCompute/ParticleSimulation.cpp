#include "ParticleSimulation.hpp"
#include <pix3.h>

/// <summary>
/// Triangle vertices are generated by geometry shader.
/// Two buffers full of particle data are used, and
/// compute thread alternates writing to each of them.
/// Render thread uses the other buffer
/// (which is not currently used by the compute shader)
/// </summary>

// InterlockedCompareExchange returns the object's value if the
// comparison fails.  If it is already 0, then its value won't
// change and 0 will be returned.
#define InterlockedGetValue(object) InterlockedCompareExchange(object, 0, 0)

//---------------------------------------------------------------------------//
/// Local functions:
//---------------------------------------------------------------------------//
static void _allocSimData() {
  g_ParticleSim =
      reinterpret_cast<ParticleSimData*>(::malloc(sizeof(*g_ParticleSim)));
  ::memset(g_ParticleSim, 0, sizeof(*g_ParticleSim));
  g_ParticleSim->m_ParticleCount = 10000;
}
//---------------------------------------------------------------------------//
static void _deallocSimData() { ::free(g_ParticleSim); }
//---------------------------------------------------------------------------//
static void _loadPipeline() {
  UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
  // Enable the debug layer (requires the Graphics Tools "optional feature").
  // NOTE: Enabling the debug layer after device creation will invalidate the
  // active device.
  {
    ID3D12DebugPtr debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
      debugController->EnableDebugLayer();

      // Enable additional debug layers.
      dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }
#endif

  IDXGIFactory4Ptr factory;
  D3D_EXEC_CHECKED(
      CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  if (g_DemoInfo->m_UseWarpDevice) {
    IDXGIAdapterPtr warpAdapter;
    D3D_EXEC_CHECKED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

    D3D_EXEC_CHECKED(D3D12CreateDevice(
        warpAdapter.GetInterfacePtr(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&g_ParticleSim->m_Device)));
  } else {
    IDXGIAdapter1Ptr hardwareAdapter;
    getHardwareAdapter(factory.GetInterfacePtr(), &hardwareAdapter, true);

    D3D_EXEC_CHECKED(D3D12CreateDevice(
        hardwareAdapter.GetInterfacePtr(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&g_ParticleSim->m_Device)));
  }

  // Describe and create the command queue.
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommandQueue(
      &queueDesc, IID_PPV_ARGS(&g_ParticleSim->m_CommandQueue)));
  D3D_NAME_OBJECT(g_ParticleSim->m_CommandQueue);

  // Describe and create the swap chain.
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = FRAME_COUNT;
  swapChainDesc.Width = g_DemoInfo->m_Width;
  swapChainDesc.Height = g_DemoInfo->m_Height;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

  IDXGISwapChain1Ptr swapChain;
  D3D_EXEC_CHECKED(factory->CreateSwapChainForHwnd(
      g_ParticleSim->m_CommandQueue
          .GetInterfacePtr(), // Swc needs the queue to force a flush on it.
      g_WinHandle,
      &swapChainDesc,
      nullptr,
      nullptr,
      &swapChain));

  // This sample does not support fullscreen transitions.
  D3D_EXEC_CHECKED(
      factory->MakeWindowAssociation(g_WinHandle, DXGI_MWA_NO_ALT_ENTER));

  D3D_EXEC_CHECKED(
      swapChain->QueryInterface(IID_PPV_ARGS(&g_ParticleSim->m_SwapChain)));

  g_ParticleSim->m_FrameIndex =
      g_ParticleSim->m_SwapChain->GetCurrentBackBufferIndex();
  g_ParticleSim->m_SwapChainEvent =
      g_ParticleSim->m_SwapChain->GetFrameLatencyWaitableObject();

  // Create descriptor heaps.
  {
    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FRAME_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(&g_ParticleSim->m_RtvHeap)));

    // Describe and create a shader resource view (SRV) and unordered
    // access view (UAV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC srvUavHeapDesc = {};
    srvUavHeapDesc.NumDescriptors = ParticleSimData::DescriptorCount;
    srvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateDescriptorHeap(
        &srvUavHeapDesc, IID_PPV_ARGS(&g_ParticleSim->m_SrvUavHeap)));
    D3D_NAME_OBJECT(g_ParticleSim->m_SrvUavHeap);

    g_ParticleSim->m_RtvDescriptorSize =
        g_ParticleSim->m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    g_ParticleSim->m_SrvUavDescriptorSize =
        g_ParticleSim->m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }

  // Create frame resources.
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        g_ParticleSim->m_RtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Create a RTV and a command allocator for each frame.
    for (UINT n = 0; n < FRAME_COUNT; n++) {
      D3D_EXEC_CHECKED(g_ParticleSim->m_SwapChain->GetBuffer(
          n, IID_PPV_ARGS(&g_ParticleSim->m_RenderTargets[n])));
      g_ParticleSim->m_Device->CreateRenderTargetView(
          g_ParticleSim->m_RenderTargets[n].GetInterfacePtr(),
          nullptr,
          rtvHandle);
      rtvHandle.Offset(1, g_ParticleSim->m_RtvDescriptorSize);

      D3D_NAME_OBJECT_INDEXED(g_ParticleSim->m_RenderTargets, n);

      D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT,
          IID_PPV_ARGS(&g_ParticleSim->m_CommandAllocators[n])));
    }
  }
}
//---------------------------------------------------------------------------//
static void _waitForRenderContext() {
  // Add a signal command to the queue.
  D3D_EXEC_CHECKED(g_ParticleSim->m_CommandQueue->Signal(
      g_ParticleSim->m_RenderContextFence.GetInterfacePtr(),
      g_ParticleSim->m_RenderContextFenceValue));

  // Instruct the fence to set the event object when the signal command
  // completes.
  D3D_EXEC_CHECKED(g_ParticleSim->m_RenderContextFence->SetEventOnCompletion(
      g_ParticleSim->m_RenderContextFenceValue,
      g_ParticleSim->m_RenderContextFenceEvent));
  g_ParticleSim->m_RenderContextFenceValue++;

  // Wait until the signal command has been processed.
  WaitForSingleObject(g_ParticleSim->m_RenderContextFenceEvent, INFINITE);
}
//---------------------------------------------------------------------------//
static void _simulate(UINT p_ThreadIndex) {
  ID3D12GraphicsCommandList* pCommandList =
      g_ParticleSim->m_ComputeCommandList[p_ThreadIndex].GetInterfacePtr();

  UINT srvIndex;
  UINT uavIndex;
  ID3D12Resource* pUavResource;
  if (g_ParticleSim->m_SrvIndex[p_ThreadIndex] == 0) {
    srvIndex = ParticleSimData::SrvParticlePosVelo0;
    uavIndex = ParticleSimData::UavParticlePosVelo1;
    pUavResource =
        g_ParticleSim->m_ParticleBuffer1[p_ThreadIndex].GetInterfacePtr();
  } else {
    srvIndex = ParticleSimData::SrvParticlePosVelo1;
    uavIndex = ParticleSimData::UavParticlePosVelo0;
    pUavResource =
        g_ParticleSim->m_ParticleBuffer0[p_ThreadIndex].GetInterfacePtr();
  }

  pCommandList->ResourceBarrier(
      1,
      &CD3DX12_RESOURCE_BARRIER::Transition(
          pUavResource,
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

  pCommandList->SetPipelineState(
      g_ParticleSim->m_ComputeState.GetInterfacePtr());
  pCommandList->SetComputeRootSignature(
      g_ParticleSim->m_ComputeRootSignature.GetInterfacePtr());

  ID3D12DescriptorHeap* ppHeaps[] = {
      g_ParticleSim->m_SrvUavHeap.GetInterfacePtr()};
  pCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

  CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
      g_ParticleSim->m_SrvUavHeap->GetGPUDescriptorHandleForHeapStart(),
      srvIndex + p_ThreadIndex,
      g_ParticleSim->m_SrvUavDescriptorSize);
  CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(
      g_ParticleSim->m_SrvUavHeap->GetGPUDescriptorHandleForHeapStart(),
      uavIndex + p_ThreadIndex,
      g_ParticleSim->m_SrvUavDescriptorSize);

  pCommandList->SetComputeRootConstantBufferView(
      ParticleSimData::ComputeRootCBV,
      g_ParticleSim->m_CbufferCS->GetGPUVirtualAddress());
  pCommandList->SetComputeRootDescriptorTable(
      ParticleSimData::ComputeRootSRVTable, srvHandle);
  pCommandList->SetComputeRootDescriptorTable(
      ParticleSimData::ComputeRootUAVTable, uavHandle);

  pCommandList->Dispatch(
      static_cast<int>(ceil(g_ParticleSim->m_ParticleCount / 128.0f)), 1, 1);

  pCommandList->ResourceBarrier(
      1,
      &CD3DX12_RESOURCE_BARRIER::Transition(
          pUavResource,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
}
//---------------------------------------------------------------------------//
static DWORD
_asyncComputeThreadProc(ParticleSimData* p_Context, int p_ThreadIndex) {
  ID3D12CommandQueue* commandQueue =
      g_ParticleSim->m_ComputeCommandQueue[p_ThreadIndex].GetInterfacePtr();
  ID3D12CommandAllocator* commandAllocator =
      g_ParticleSim->m_ComputeAllocator[p_ThreadIndex].GetInterfacePtr();
  ID3D12GraphicsCommandList* commandList =
      g_ParticleSim->m_ComputeCommandList[p_ThreadIndex].GetInterfacePtr();
  ID3D12Fence* fence =
      g_ParticleSim->m_ThreadFences[p_ThreadIndex].GetInterfacePtr();

  while (0 == InterlockedGetValue(&g_ParticleSim->m_Terminating)) {
    // Run the particle simulation.
    _simulate(p_ThreadIndex);

    // Close and execute the command list.
    D3D_EXEC_CHECKED(commandList->Close());
    ID3D12CommandList* ppCommandLists[] = {commandList};

    PIXBeginEvent(
        commandQueue,
        0,
        L"Thread %d: Iterate on the particle simulation",
        p_ThreadIndex);
    commandQueue->ExecuteCommandLists(1, ppCommandLists);
    PIXEndEvent(commandQueue);

    // Wait for the compute shader to complete the simulation.
    UINT64 threadFenceValue = InterlockedIncrement(
        &g_ParticleSim->m_ThreadFenceValues[p_ThreadIndex]);
    D3D_EXEC_CHECKED(commandQueue->Signal(fence, threadFenceValue));
    D3D_EXEC_CHECKED(fence->SetEventOnCompletion(
        threadFenceValue, g_ParticleSim->m_ThreadFenceEvents[p_ThreadIndex]));
    WaitForSingleObject(
        g_ParticleSim->m_ThreadFenceEvents[p_ThreadIndex], INFINITE);

    // Wait for the render thread to be done with the SRV so that
    // the next frame in the simulation can run.
    UINT64 renderContextFenceValue = InterlockedGetValue(
        &g_ParticleSim->m_RenderContextFenceValues[p_ThreadIndex]);
    if (g_ParticleSim->m_RenderContextFence->GetCompletedValue() <
        renderContextFenceValue) {
      D3D_EXEC_CHECKED(commandQueue->Wait(
          g_ParticleSim->m_RenderContextFence.GetInterfacePtr(),
          renderContextFenceValue));
      InterlockedExchange(
          &g_ParticleSim->m_RenderContextFenceValues[p_ThreadIndex], 0);
    }

    // Swap the indices to the SRV and UAV.
    g_ParticleSim->m_SrvIndex[p_ThreadIndex] =
        1 - g_ParticleSim->m_SrvIndex[p_ThreadIndex];

    // Prepare for the next frame.
    D3D_EXEC_CHECKED(commandAllocator->Reset());
    D3D_EXEC_CHECKED(commandList->Reset(
        commandAllocator, g_ParticleSim->m_ComputeState.GetInterfacePtr()));
  }

  return 0;
}
//---------------------------------------------------------------------------//
static DWORD WINAPI ThreadProc(ParticleSimData::ThreadData* p_Data) {
  return _asyncComputeThreadProc(p_Data->m_Context, p_Data->m_ThreadIndex);
}
//---------------------------------------------------------------------------//
static float _randomPercent() {
  float ret = static_cast<float>((rand() % 10000) - 5000);
  return ret / 5000.0f;
}
//---------------------------------------------------------------------------//
static void _loadParticles(
    _Out_writes_(p_ParticleCount) ParticleSimData::ParticleMotion* p_Particles,
    const XMFLOAT3& p_Center,
    const XMFLOAT4& p_Velocity,
    float p_Spread,
    UINT p_ParticleCount) {
  srand(0);
  for (UINT i = 0; i < p_ParticleCount; i++) {
    XMFLOAT3 delta(p_Spread, p_Spread, p_Spread);

    while (XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&delta))) >
           p_Spread * p_Spread) {
      delta.x = _randomPercent() * p_Spread;
      delta.y = _randomPercent() * p_Spread;
      delta.z = _randomPercent() * p_Spread;
    }

    p_Particles[i].m_Position.x = p_Center.x + delta.x;
    p_Particles[i].m_Position.y = p_Center.y + delta.y;
    p_Particles[i].m_Position.z = p_Center.z + delta.z;
    p_Particles[i].m_Position.w = 10000.0f * 10000.0f;

    p_Particles[i].m_Velocity = p_Velocity;
  }
}
//---------------------------------------------------------------------------//
static void _createVertexBuffer() {
  using Vertex = ParticleSimData::ParticleVertex; 
  Vertex * vertices = (Vertex *)::calloc(g_ParticleSim->m_ParticleCount, sizeof(Vertex));
  DEFER(free_vertex_mem) { ::free(vertices); };
  for (UINT i = 0; i < g_ParticleSim->m_ParticleCount; i++) {
    vertices[i].m_Color = XMFLOAT4(1.0f, 1.0f, 0.2f, 1.0f);
  }
  const UINT bufferSize = g_ParticleSim->m_ParticleCount * sizeof(ParticleSimData::ParticleVertex);

  D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&g_ParticleSim->m_VertexBuffer)));

  D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&g_ParticleSim->m_VertexBufferUpload)));

  D3D_NAME_OBJECT(g_ParticleSim->m_VertexBuffer);

  D3D12_SUBRESOURCE_DATA vertexData = {};
  vertexData.pData = reinterpret_cast<UINT8*>(&vertices[0]);
  vertexData.RowPitch = bufferSize;
  vertexData.SlicePitch = vertexData.RowPitch;

  UpdateSubresources<1>(
      g_ParticleSim->m_CommandList.GetInterfacePtr(),
      g_ParticleSim->m_VertexBuffer.GetInterfacePtr(),
      g_ParticleSim->m_VertexBufferUpload.GetInterfacePtr(),
      0,
      0,
      1,
      &vertexData);
  g_ParticleSim->m_CommandList->ResourceBarrier(
      1,
      &CD3DX12_RESOURCE_BARRIER::Transition(
          g_ParticleSim->m_VertexBuffer.GetInterfacePtr(),
          D3D12_RESOURCE_STATE_COPY_DEST,
          D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

  g_ParticleSim->m_VertexBufferView.BufferLocation = g_ParticleSim->m_VertexBuffer->GetGPUVirtualAddress();
  g_ParticleSim->m_VertexBufferView.SizeInBytes = static_cast<UINT>(bufferSize);
  g_ParticleSim->m_VertexBufferView.StrideInBytes = sizeof(Vertex);
}
//---------------------------------------------------------------------------//
static void _createParticleBuffers() {
  using Data = ParticleSimData::ParticleMotion;

  // Initialize the data in the buffers.
  Data* data = (Data*)::calloc(g_ParticleSim->m_ParticleCount, sizeof(Data));
  DEFER(free_data_mem) { ::free(data); };

  const UINT dataSize = g_ParticleSim->m_ParticleCount * sizeof(Data);

  // Split the particles into two groups.
  float centerSpread = g_ParticleSim->m_ParticleSpread * 0.50f;
  _loadParticles(
      &data[0],
      XMFLOAT3(centerSpread, 0, 0),
      XMFLOAT4(0, 0, -20, 1 / 100000000.0f),
      g_ParticleSim->m_ParticleSpread,
      g_ParticleSim->m_ParticleCount / 2);
  _loadParticles(
      &data[g_ParticleSim->m_ParticleCount / 2],
      XMFLOAT3(-centerSpread, 0, 0),
      XMFLOAT4(0, 0, 20, 1 / 100000000.0f),
      g_ParticleSim->m_ParticleSpread,
      g_ParticleSim->m_ParticleCount / 2);

  D3D12_HEAP_PROPERTIES defaultHeapProperties =
      CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
  D3D12_HEAP_PROPERTIES uploadHeapProperties =
      CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
  D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
      dataSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  D3D12_RESOURCE_DESC uploadBufferDesc =
      CD3DX12_RESOURCE_DESC::Buffer(dataSize);

  for (UINT index = 0; index < THREAD_COUNT; index++) {
    // Create two buffers in the GPU, each with a copy of the particles data.
    // The compute shader will update one of them while the rendering thread
    // renders the other. When rendering completes, the threads will swap
    // which buffer they work on.

    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommittedResource(
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&g_ParticleSim->m_ParticleBuffer0[index])));

    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommittedResource(
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&g_ParticleSim->m_ParticleBuffer1[index])));

    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&g_ParticleSim->m_ParticleBuffer0Upload[index])));

    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&g_ParticleSim->m_ParticleBuffer1Upload[index])));

    D3D_NAME_OBJECT_INDEXED(g_ParticleSim->m_ParticleBuffer0, index);
    D3D_NAME_OBJECT_INDEXED(g_ParticleSim->m_ParticleBuffer1, index);

    D3D12_SUBRESOURCE_DATA particleData = {};
    particleData.pData = reinterpret_cast<UINT8*>(&data[0]);
    particleData.RowPitch = dataSize;
    particleData.SlicePitch = particleData.RowPitch;

    UpdateSubresources<1>(
        g_ParticleSim->m_CommandList.GetInterfacePtr(),
        g_ParticleSim->m_ParticleBuffer0[index].GetInterfacePtr(),
        g_ParticleSim->m_ParticleBuffer0Upload[index].GetInterfacePtr(),
        0,
        0,
        1,
        &particleData);
    UpdateSubresources<1>(
        g_ParticleSim->m_CommandList.GetInterfacePtr(),
        g_ParticleSim->m_ParticleBuffer1[index].GetInterfacePtr(),
        g_ParticleSim->m_ParticleBuffer1Upload[index].GetInterfacePtr(),
        0,
        0,
        1,
        &particleData);
    g_ParticleSim->m_CommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            g_ParticleSim->m_ParticleBuffer0[index].GetInterfacePtr(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    g_ParticleSim->m_CommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            g_ParticleSim->m_ParticleBuffer1[index].GetInterfacePtr(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = g_ParticleSim->m_ParticleCount;
    srvDesc.Buffer.StructureByteStride = sizeof(Data);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle0(
        g_ParticleSim->m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        ParticleSimData::SrvParticlePosVelo0 + index,
        g_ParticleSim->m_SrvUavDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle1(
        g_ParticleSim->m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        ParticleSimData::SrvParticlePosVelo1 + index,
        g_ParticleSim->m_SrvUavDescriptorSize);
    g_ParticleSim->m_Device->CreateShaderResourceView(
        g_ParticleSim->m_ParticleBuffer0[index].GetInterfacePtr(),
        &srvDesc,
        srvHandle0);
    g_ParticleSim->m_Device->CreateShaderResourceView(
        g_ParticleSim->m_ParticleBuffer1[index].GetInterfacePtr(),
        &srvDesc,
        srvHandle1);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = g_ParticleSim->m_ParticleCount;
    uavDesc.Buffer.StructureByteStride = sizeof(Data);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle0(
        g_ParticleSim->m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        ParticleSimData::UavParticlePosVelo0 + index,
        g_ParticleSim->m_SrvUavDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle1(
        g_ParticleSim->m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        ParticleSimData::UavParticlePosVelo1 + index,
        g_ParticleSim->m_SrvUavDescriptorSize);
    g_ParticleSim->m_Device->CreateUnorderedAccessView(
        g_ParticleSim->m_ParticleBuffer0[index].GetInterfacePtr(),
        nullptr,
        &uavDesc,
        uavHandle0);
    g_ParticleSim->m_Device->CreateUnorderedAccessView(
        g_ParticleSim->m_ParticleBuffer1[index].GetInterfacePtr(),
        nullptr,
        &uavDesc,
        uavHandle1);
  }
}
//---------------------------------------------------------------------------//
static void _loadAssets() {
  // Create the root signatures.
  {
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport
    // succeeds, the HighestVersion returned will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(g_ParticleSim->m_Device->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
      featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Graphics root signature.
    {
      CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
      ranges[0].Init(
          D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
          1,
          0,
          0,
          D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

      CD3DX12_ROOT_PARAMETER1
      rootParameters[ParticleSimData::GraphicsRootParametersCount];
      rootParameters[ParticleSimData::GraphicsRootCBV].InitAsConstantBufferView(
          0,
          0,
          D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
          D3D12_SHADER_VISIBILITY_ALL);
      rootParameters[ParticleSimData::GraphicsRootSRVTable]
          .InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);

      CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
      rootSignatureDesc.Init_1_1(
          _countof(rootParameters),
          rootParameters,
          0,
          nullptr,
          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

      ID3DBlobPtr signature;
      ID3DBlobPtr error;
      D3D_EXEC_CHECKED(D3DX12SerializeVersionedRootSignature(
          &rootSignatureDesc, featureData.HighestVersion, &signature, &error));
      D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateRootSignature(
          0,
          signature->GetBufferPointer(),
          signature->GetBufferSize(),
          IID_PPV_ARGS(&g_ParticleSim->m_RootSignature)));
      D3D_NAME_OBJECT(g_ParticleSim->m_RootSignature);
    }

    // Compute root signature.
    {
      CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
      ranges[0].Init(
          D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
          1,
          0,
          0,
          D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
      ranges[1].Init(
          D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
          1,
          0,
          0,
          D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

      CD3DX12_ROOT_PARAMETER1
      rootParameters[ParticleSimData::ComputeRootParametersCount];
      rootParameters[ParticleSimData::ComputeRootCBV].InitAsConstantBufferView(
          0,
          0,
          D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
          D3D12_SHADER_VISIBILITY_ALL);
      rootParameters[ParticleSimData::ComputeRootSRVTable]
          .InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
      rootParameters[ParticleSimData::ComputeRootUAVTable]
          .InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);

      CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
      computeRootSignatureDesc.Init_1_1(
          _countof(rootParameters), rootParameters, 0, nullptr);

      ID3DBlobPtr signature;
      ID3DBlobPtr error;
      D3D_EXEC_CHECKED(D3DX12SerializeVersionedRootSignature(
          &computeRootSignatureDesc,
          featureData.HighestVersion,
          &signature,
          &error));
      D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateRootSignature(
          0,
          signature->GetBufferPointer(),
          signature->GetBufferSize(),
          IID_PPV_ARGS(&g_ParticleSim->m_ComputeRootSignature)));
      D3D_NAME_OBJECT(g_ParticleSim->m_ComputeRootSignature);
    }
  }

  // Create the pipeline states, which includes compiling and loading shaders.
  {
    ID3DBlobPtr vertexShader;
    ID3DBlobPtr geometryShader;
    ID3DBlobPtr pixelShader;
    ID3DBlobPtr computeShader;

#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    // Load and compile shaders.
    D3D_EXEC_CHECKED(D3DCompileFromFile(
        demoGetAssetPath(g_DemoInfo, L"ParticleDraw.hlsl").c_str(),
        nullptr,
        nullptr,
        "VSParticleDraw",
        "vs_5_0",
        compileFlags,
        0,
        &vertexShader,
        nullptr));
    D3D_EXEC_CHECKED(D3DCompileFromFile(
        demoGetAssetPath(g_DemoInfo, L"ParticleDraw.hlsl").c_str(),
        nullptr,
        nullptr,
        "GSParticleDraw",
        "gs_5_0",
        compileFlags,
        0,
        &geometryShader,
        nullptr));
    D3D_EXEC_CHECKED(D3DCompileFromFile(
        demoGetAssetPath(g_DemoInfo, L"ParticleDraw.hlsl").c_str(),
        nullptr,
        nullptr,
        "PSParticleDraw",
        "ps_5_0",
        compileFlags,
        0,
        &pixelShader,
        nullptr));
    D3D_EXEC_CHECKED(D3DCompileFromFile(
        demoGetAssetPath(g_DemoInfo, L"NBodyGravityCS.hlsl").c_str(),
        nullptr,
        nullptr,
        "CSMain",
        "cs_5_0",
        compileFlags,
        0,
        &computeShader,
        nullptr));

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"COLOR",
         0,
         DXGI_FORMAT_R32G32B32A32_FLOAT,
         0,
         0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
    };

    // Describe the blend and depth states.
    CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = FALSE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
    psoDesc.pRootSignature = g_ParticleSim->m_RootSignature.GetInterfacePtr();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.GetInterfacePtr());
    psoDesc.GS = CD3DX12_SHADER_BYTECODE(geometryShader.GetInterfacePtr());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.GetInterfacePtr());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;

    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&g_ParticleSim->m_PipelineState)));
    D3D_NAME_OBJECT(g_ParticleSim->m_PipelineState);

    // Describe and create the compute pipeline state object (PSO).
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature =
        g_ParticleSim->m_ComputeRootSignature.GetInterfacePtr();
    computePsoDesc.CS =
        CD3DX12_SHADER_BYTECODE(computeShader.GetInterfacePtr());

    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateComputePipelineState(
        &computePsoDesc, IID_PPV_ARGS(&g_ParticleSim->m_ComputeState)));
    D3D_NAME_OBJECT(g_ParticleSim->m_ComputeState);
  }

  // Create the command list.
  D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommandList(
      0,
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      g_ParticleSim->m_CommandAllocators[g_ParticleSim->m_FrameIndex]
          .GetInterfacePtr(),
      g_ParticleSim->m_PipelineState.GetInterfacePtr(),
      IID_PPV_ARGS(&g_ParticleSim->m_CommandList)));
  D3D_NAME_OBJECT(g_ParticleSim->m_CommandList);

  _createVertexBuffer();
  _createParticleBuffers();

  // Note: ComPtr's are CPU objects but this resource needs to stay in scope
  // until the command list that references it has finished executing on the
  // GPU. We will flush the GPU at the end of this method to ensure the resource
  // is not prematurely destroyed.
  ID3D12ResourcePtr cbufferCSUpload;

  // Create the compute shader's constant buffer.
  {
    const UINT bufferSize = sizeof(ParticleSimData::CbufferCS);

    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&g_ParticleSim->m_CbufferCS)));

    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&cbufferCSUpload)));

    D3D_NAME_OBJECT(g_ParticleSim->m_CbufferCS);

    ParticleSimData::CbufferCS cbufferCS = {};
    cbufferCS.m_Param[0] = g_ParticleSim->m_ParticleCount;
    cbufferCS.m_Param[1] = int(ceil(g_ParticleSim->m_ParticleCount / 128.0f));
    cbufferCS.m_ParamFloat[0] = 0.1f;
    cbufferCS.m_ParamFloat[1] = 1.0f;

    D3D12_SUBRESOURCE_DATA computeCBData = {};
    computeCBData.pData = reinterpret_cast<UINT8*>(&cbufferCS);
    computeCBData.RowPitch = bufferSize;
    computeCBData.SlicePitch = computeCBData.RowPitch;

    UpdateSubresources<1>(
        g_ParticleSim->m_CommandList.GetInterfacePtr(),
        g_ParticleSim->m_CbufferCS.GetInterfacePtr(),
        cbufferCSUpload.GetInterfacePtr(),
        0,
        0,
        1,
        &computeCBData);
    g_ParticleSim->m_CommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            g_ParticleSim->m_CbufferCS.GetInterfacePtr(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
  }

  // Create the geometry shader's constant buffer.
  {
    const UINT cbufferGSSize = sizeof(ParticleSimData::CbufferGS) * FRAME_COUNT;

    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(cbufferGSSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&g_ParticleSim->m_CbufferGS)));

    D3D_NAME_OBJECT(g_ParticleSim->m_CbufferGS);

    CD3DX12_RANGE readRange(
        0, 0); // We do not intend to read from this resource on the CPU.
    D3D_EXEC_CHECKED(g_ParticleSim->m_CbufferGS->Map(
        0,
        &readRange,
        reinterpret_cast<void**>(&g_ParticleSim->m_CbufferGSData)));
    ZeroMemory(g_ParticleSim->m_CbufferGSData, cbufferGSSize);
  }

  // Close the command list and execute it to begin the initial GPU setup.
  D3D_EXEC_CHECKED(g_ParticleSim->m_CommandList->Close());
  ID3D12CommandList* ppCommandLists[] = {
      g_ParticleSim->m_CommandList.GetInterfacePtr()};
  g_ParticleSim->m_CommandQueue->ExecuteCommandLists(
      _countof(ppCommandLists), ppCommandLists);

  // Create synchronization objects and wait until assets have been uploaded to
  // the GPU.
  {
    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateFence(
        g_ParticleSim->m_RenderContextFenceValue,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&g_ParticleSim->m_RenderContextFence)));
    g_ParticleSim->m_RenderContextFenceValue++;

    g_ParticleSim->m_RenderContextFenceEvent =
        CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_ParticleSim->m_RenderContextFenceEvent == nullptr) {
      D3D_EXEC_CHECKED(HRESULT_FROM_WIN32(GetLastError()));
    }

    _waitForRenderContext();
  }
}
//---------------------------------------------------------------------------//
static void _restoreD3DResources();
//---------------------------------------------------------------------------//
static void _releaseD3DResources();
//---------------------------------------------------------------------------//
static void _waitForGpu();
//---------------------------------------------------------------------------//
static void _createAsyncContexts() {
  for (UINT threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex) {
    // Create compute resources.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {
        D3D12_COMMAND_LIST_TYPE_COMPUTE, 0, D3D12_COMMAND_QUEUE_FLAG_NONE};
    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommandQueue(
        &queueDesc,
        IID_PPV_ARGS(&g_ParticleSim->m_ComputeCommandQueue[threadIndex])));
    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_COMPUTE,
        IID_PPV_ARGS(&g_ParticleSim->m_ComputeAllocator[threadIndex])));
    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_COMPUTE,
        g_ParticleSim->m_ComputeAllocator[threadIndex].GetInterfacePtr(),
        nullptr,
        IID_PPV_ARGS(&g_ParticleSim->m_ComputeCommandList[threadIndex])));
    D3D_EXEC_CHECKED(g_ParticleSim->m_Device->CreateFence(
        0,
        D3D12_FENCE_FLAG_SHARED,
        IID_PPV_ARGS(&g_ParticleSim->m_ThreadFences[threadIndex])));

    g_ParticleSim->m_ThreadFenceEvents[threadIndex] =
        CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_ParticleSim->m_ThreadFenceEvents[threadIndex] == nullptr) {
      D3D_EXEC_CHECKED(HRESULT_FROM_WIN32(GetLastError()));
    }

    // (OM) TODO! Check if this is working as intended
    g_ParticleSim->m_ThreadData[threadIndex].m_Context = g_ParticleSim;
    g_ParticleSim->m_ThreadData[threadIndex].m_ThreadIndex = threadIndex;

    g_ParticleSim->m_ThreadHandles[threadIndex] = CreateThread(
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(ThreadProc),
        reinterpret_cast<void*>(&g_ParticleSim->m_ThreadData[threadIndex]),
        CREATE_SUSPENDED,
        nullptr);

    ResumeThread(g_ParticleSim->m_ThreadHandles[threadIndex]);
  }
}
//---------------------------------------------------------------------------//
static void _populateCommandList();
//---------------------------------------------------------------------------//
static void _moveToNextFrame();
//---------------------------------------------------------------------------//
// Core functions:
//---------------------------------------------------------------------------//
void onInit(void) {
  _allocSimData();
  DEBUG_BREAK(g_DemoInfo->m_IsInitialized);

  UINT width = g_DemoInfo->m_Width;
  UINT height = g_DemoInfo->m_Height;
  g_ParticleSim->m_FrameIndex = 0;
  g_ParticleSim->m_Viewport = CD3DX12_VIEWPORT(
      0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
  g_ParticleSim->m_ScissorRect =
      CD3DX12_RECT(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));
  g_ParticleSim->m_RtvDescriptorSize = 0;
  g_ParticleSim->m_SrvUavDescriptorSize = 0;
  g_ParticleSim->m_CbufferGSData = nullptr;
  g_ParticleSim->m_RenderContextFenceValue = 0;
  g_ParticleSim->m_Terminating = 0;
  setArrayToZero(g_ParticleSim->m_SrvIndex);
  setArrayToZero(g_ParticleSim->m_FrameFenceValues);

  for (int i = 0; i < THREAD_COUNT; ++i) {
    g_ParticleSim->m_RenderContextFenceValues[i] = 0;
    g_ParticleSim->m_ThreadFenceValues[i] = 0;
  }

  float sqRootNumAsyncContexts = sqrt(static_cast<float>(THREAD_COUNT));
  g_ParticleSim->m_HeightInstances =
      static_cast<UINT>(ceil(sqRootNumAsyncContexts));
  g_ParticleSim->m_WidthInstances =
      static_cast<UINT>(ceil(sqRootNumAsyncContexts));

  if (g_ParticleSim->m_WidthInstances *
          (g_ParticleSim->m_HeightInstances - 1) >=
      THREAD_COUNT) {
    g_ParticleSim->m_HeightInstances--;
  }

  D3D_EXEC_CHECKED(DXGIDeclareAdapterRemovalSupport());

  cameraInit(&g_ParticleSim->m_Camera, {0.0f, 0.0f, 1500.0f});
  g_ParticleSim->m_Camera.m_MoveSpeed = 250.0f;

  _loadPipeline();
  _loadAssets();
  _createAsyncContexts();
}
//---------------------------------------------------------------------------//
void onDestroy(void) { _deallocSimData(); }
//---------------------------------------------------------------------------//
void onUpdate(void) {}
//---------------------------------------------------------------------------//
void onRender(void) {}
//---------------------------------------------------------------------------//
void onKeyDown(UINT8) {}
//---------------------------------------------------------------------------//
void onKeyUp(UINT8) {}
//---------------------------------------------------------------------------//
