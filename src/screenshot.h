#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <filesystem>

namespace meteor {
bool saveBackBufferBmp(ID3D11Device* device, ID3D11DeviceContext* context,
    IDXGISwapChain* swapChain, const std::filesystem::path& path);
bool saveUiScreenshotPng(ID3D11Device* device, ID3D11DeviceContext* context,
    IDXGISwapChain* swapChain, ID3D11RenderTargetView* renderTarget,
    const std::filesystem::path& path, const ImVec2& windowPos, const ImVec2& windowSize);
} // namespace meteor
