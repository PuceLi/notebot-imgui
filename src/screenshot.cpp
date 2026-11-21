#include "screenshot.h"
#include <gdiplus.h>
#include <imgui_impl_dx11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace meteor {
bool saveBackBufferBmp(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain, const std::filesystem::path& path) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    const HRESULT createResult = device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(createResult)) { backBuffer->Release(); return false; }
    context->CopyResource(staging, backBuffer);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        staging->Release(); backBuffer->Release(); return false;
    }
    const int stride = int(desc.Width) * 4;
    const int imageSize = stride * int(desc.Height);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + imageSize;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = int(desc.Width);
    info.biHeight = -int(desc.Height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = imageSize;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    std::vector<std::uint8_t> row(static_cast<size_t>(stride), 0);
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* source = static_cast<const std::uint8_t*>(map.pData) + size_t(y) * map.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) {
            row[x*4] = source[x*4+2]; row[x*4+1] = source[x*4+1];
            row[x*4+2] = source[x*4]; row[x*4+3] = source[x*4+3];
        }
        out.write(reinterpret_cast<const char*>(row.data()), stride);
    }
    context->Unmap(staging, 0);
    staging->Release(); backBuffer->Release();
    return out.good();
}

bool saveUiScreenshotPng(ID3D11Device* device, ID3D11DeviceContext* context,
    IDXGISwapChain* swapChain, ID3D11RenderTargetView* renderTarget,
    const std::filesystem::path& path, const ImVec2& windowPos, const ImVec2& windowSize) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    const int left = std::clamp(static_cast<int>(std::floor(windowPos.x)), 0, static_cast<int>(desc.Width));
    const int top = std::clamp(static_cast<int>(std::floor(windowPos.y)), 0, static_cast<int>(desc.Height));
    const int right = std::clamp(static_cast<int>(std::ceil(windowPos.x + windowSize.x)), left, static_cast<int>(desc.Width));
    const int bottom = std::clamp(static_cast<int>(std::ceil(windowPos.y + windowSize.y)), top, static_cast<int>(desc.Height));
    const int width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0) return false;

    // Render a UI-only pass; the gray demo backdrop must not enter the PNG.
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> captureTexture;
    ComPtr<ID3D11RenderTargetView> captureTarget;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, captureTexture.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(captureTexture.Get(), nullptr, captureTarget.GetAddressOf()))) return false;
    const float transparent[4]{};
    auto* target = captureTarget.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    context->ClearRenderTargetView(target, transparent);
    ImDrawData capture = *ImGui::GetDrawData();
    capture.CmdLists.clear();
    for (const auto* list : ImGui::GetDrawData()->CmdLists) {
        ImDrawList* copy = list->CloneOutput();
        for (auto& vertex : copy->VtxBuffer) {
            if (vertex.col == IM_COL32(80, 80, 80, 255))
                vertex.col = IM_COL32(20, 20, 20, 200);
        }
        capture.CmdLists.push_back(copy);
    }
    ImGui_ImplDX11_RenderDrawData(&capture);
    for (auto* list : capture.CmdLists) IM_DELETE(list);
    context->OMSetRenderTargets(1, &renderTarget, nullptr);

    // Read back only the selected window region.
    desc.Width = width;
    desc.Height = height;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) return false;
    const D3D11_BOX box{static_cast<UINT>(left), static_cast<UINT>(top), 0,
                       static_cast<UINT>(right), static_cast<UINT>(bottom), 1};
    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, captureTexture.Get(), 0, &box);
    std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    for (int y = 0; y < height; ++y) {
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        auto* dest = pixels.data() + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            // Convert premultiplied RGBA to straight-alpha BGRA for GDI+.
            const unsigned alpha = source[x*4+3];
            auto straight = [alpha](unsigned channel) -> std::uint8_t {
                return alpha ? static_cast<std::uint8_t>(std::min(255u, (channel * 255u + alpha / 2) / alpha)) : 0;
            };
            dest[x*4] = straight(source[x*4+2]);
            dest[x*4+1] = straight(source[x*4+1]);
            dest[x*4+2] = straight(source[x*4]);
            dest[x*4+3] = static_cast<std::uint8_t>(alpha);
        }
    }
    context->Unmap(staging.Get(), 0);
    Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppARGB, pixels.data());
    const CLSID pngEncoder{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    return bitmap.Save(path.c_str(), &pngEncoder) == Gdiplus::Ok;
}
} // namespace meteor
