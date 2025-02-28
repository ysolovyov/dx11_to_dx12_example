#include "pch.h"

using Microsoft::WRL::ComPtr;

int main()
{
    ComPtr<ID3D11Device> d3d11Device;
    ComPtr<ID3D11DeviceContext> d3d11Context;

    ComPtr<ID3D12Device> d3d12Device;

    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    UINT64 fenceValue = 0;
    ComPtr<ID3D12Fence> fence;
    HANDLE eventHandle;

    ComPtr<ID3D11Buffer> dx11Buffer;
    ComPtr<ID3D12Resource> dx12BufferUAV;
    ComPtr<ID3D12Resource> dx12BufferReadback; // used to print the data

    const int numElements = 32;

    float* pInitData = new float[numElements];
    for (int i = 0; i < numElements; ++i) {
        pInitData[i] = sqrtf(i);
    }

    THROW_IF_FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3d11Device, nullptr, &d3d11Context));
    THROW_IF_FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device)));

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    THROW_IF_FAILED(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

    THROW_IF_FAILED(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
    THROW_IF_FAILED(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

    THROW_IF_FAILED(d3d12Device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Create DX11 buffer with data
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = numElements * sizeof(float);
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

        D3D11_SUBRESOURCE_DATA initData;
        initData.pSysMem = pInitData;
        initData.SysMemPitch = 0;
        initData.SysMemSlicePitch = 0;

        THROW_IF_FAILED(d3d11Device->CreateBuffer(&bufferDesc, &initData, &dx11Buffer));
    }

    // Crate DX12 buffer
    {
        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = numElements * sizeof(float);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        THROW_IF_FAILED(d3d12Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&dx12BufferUAV)));

        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        THROW_IF_FAILED(d3d12Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&dx12BufferReadback)));
    }

    // Get the DirectX 11 resource interface
    ComPtr<IDXGIResource> dxgiResource;
    THROW_IF_FAILED(dx11Buffer->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

    // Get the shared handle for the DX11 buffer
    HANDLE sharedHandle = nullptr;
    THROW_IF_FAILED(dxgiResource->GetSharedHandle(&sharedHandle));

    // Open the shared buffer in DirectX 12
    ID3D12Resource* pSharedDX12Buffer = nullptr;
    THROW_IF_FAILED(d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(&pSharedDX12Buffer)));

    commandList->CopyResource(dx12BufferUAV.Get(), pSharedDX12Buffer);
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dx12BufferUAV.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE));
    commandList->CopyResource(dx12BufferReadback.Get(), dx12BufferUAV.Get());

    commandList->Close();

    ID3D12CommandList* commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(ARRAYSIZE(commandLists), commandLists);

    commandQueue->Signal(fence.Get(), ++fenceValue);
    fence->SetEventOnCompletion(fenceValue, eventHandle);
    WaitForSingleObject(eventHandle, INFINITE);

    THROW_IF_FAILED(commandAllocator->Reset());
    THROW_IF_FAILED(commandList->Reset(commandAllocator.Get(), nullptr));

    float* pData = nullptr;
    dx12BufferReadback->Map(0, nullptr, reinterpret_cast<void**>(&pData));
    for (int i = 0; i < numElements; ++i) {
        printf("pData[%d]=%f\n", i, pData[i]);
    }
    dx12BufferReadback->Unmap(0, nullptr);

    delete[] pInitData;
    CloseHandle(eventHandle);

    return 0;
}
