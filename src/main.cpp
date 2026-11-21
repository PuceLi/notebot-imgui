#include "notebot_ui.h"
#include "song_duration.h"
#include "screenshot.h"
#include "translation.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cfloat>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
IDXGISwapChain* swapChain = nullptr;
ID3D11RenderTargetView* renderTarget = nullptr;
std::array<ID3D11ShaderResourceView*,3> iconViews{};
HANDLE logOutput = nullptr;

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool consoleLoggingEnabled(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        std::ofstream defaults(path);
        defaults << "# Set to true to show runtime logs in a console. Restart after changing.\n"
                    "console_logging=false\n";
    }
    std::ifstream config(path);
    std::string line;
    while (std::getline(config, line)) {
        line = trim(line.substr(0, line.find_first_of("#;")));
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (key == "console_logging")
            return value == "true" || value == "1" || value == "yes" || value == "on";
    }
    return false;
}

void enableConsoleLogging() {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!output || output == INVALID_HANDLE_VALUE || GetFileType(output) == FILE_TYPE_UNKNOWN) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        output = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    if (output && output != INVALID_HANDLE_VALUE && GetFileType(output) != FILE_TYPE_UNKNOWN) {
        SetConsoleOutputCP(CP_UTF8);
        logOutput = output;
    }
}

void logEvent(const std::string& message) {
    if (!logOutput) return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char timestamp[16]{};
    std::snprintf(timestamp, sizeof(timestamp), "%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
    const std::string line = std::string("[") + timestamp + "] " + message + "\r\n";
    DWORD written = 0;
    WriteFile(logOutput, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void cleanupTarget() { if (renderTarget) { renderTarget->Release(); renderTarget = nullptr; } }

void createTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        device->CreateRenderTargetView(backBuffer, nullptr, &renderTarget);
        backBuffer->Release();
    }
}

bool createDevice(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 2, D3D11_SDK_VERSION, &desc, &swapChain, &device, &level, &context);
    if (FAILED(hr)) return false;
    createTarget();
    return renderTarget != nullptr;
}

void cleanupDevice() {
    for (auto*& view : iconViews) { if (view) { view->Release(); view = nullptr; } }
    cleanupTarget();
    if (swapChain) { swapChain->Release(); swapChain = nullptr; }
    if (context) { context->Release(); context = nullptr; }
    if (device) { device->Release(); device = nullptr; }
}

ID3D11ShaderResourceView* loadPngIcon(ID3D11Device* gpu, const std::filesystem::path& path) {
    Gdiplus::Bitmap image(path.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok) return nullptr;
    const UINT width=image.GetWidth(), height=image.GetHeight();
    if (width==0 || height==0) return nullptr;
    Gdiplus::Rect rect(0,0,static_cast<INT>(width),static_cast<INT>(height));
    Gdiplus::BitmapData bits{};
    if (image.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&bits)!=Gdiplus::Ok) return nullptr;
    std::vector<std::uint8_t> pixels(size_t(width)*height*4);
    for (UINT y=0;y<height;++y) {
        const auto* row=static_cast<const std::uint8_t*>(bits.Scan0)+ptrdiff_t(y)*bits.Stride;
        std::copy_n(row,width*4,pixels.data()+size_t(y)*width*4);
    }
    image.UnlockBits(&bits);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=pixels.data(); data.SysMemPitch=width*4;
    ID3D11Texture2D* texture=nullptr;
    if (FAILED(gpu->CreateTexture2D(&desc,&data,&texture))) return nullptr;
    ID3D11ShaderResourceView* view=nullptr;
    if (FAILED(gpu->CreateShaderResourceView(texture,nullptr,&view))) view=nullptr;
    texture->Release();
    return view;
}

bool addFontFromPath(ImFontAtlas* atlas, std::vector<unsigned char>& storage,
                     const std::filesystem::path& path, bool merge) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff length = file.tellg();
    if (length <= 0 || length > INT_MAX) return false;
    storage.resize(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(storage.data()), length)) {
        storage.clear();
        return false;
    }
    ImFontConfig config{};
    config.MergeMode = merge;
    config.FontDataOwnedByAtlas = false; // storage lives until after ImGui::DestroyContext().
    config.PixelSnapH = merge;
    if (atlas->AddFontFromMemoryTTF(storage.data(), static_cast<int>(length), 18.0f, &config)) return true;
    storage.clear();
    return false;
}

std::string formatSongTime(int tick) {
    const auto milliseconds = static_cast<long long>(std::max(0, tick)) * 50;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%02lld:%02lld:%03lld", milliseconds / 60000,
                  milliseconds / 1000 % 60, milliseconds % 1000);
    return text;
}

void drawNowPlaying(meteor::NotebotUi& ui, bool& resetNowPlayingPosition) {
    const auto* viewport = ImGui::GetMainViewport();
    const auto defaultPosition = ImVec2(viewport->GetCenter().x, viewport->Pos.y + 72.0f);
    if (resetNowPlayingPosition) {
        ImGui::SetNextWindowPos(defaultPosition, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        resetNowPlayingPosition = false;
    } else {
        ImGui::SetNextWindowPos(defaultPosition, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
    }
    const auto& style = ImGui::GetStyle();
    const float hudHeight = std::max(72.0f, style.WindowPadding.y * 2.0f +
        ImGui::GetFrameHeight() * 2.0f + style.ItemSpacing.y + 4.0f);
    ImGui::SetNextWindowSize(ImVec2(360.0f, hudHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f, hudHeight), ImVec2(FLT_MAX, hudHeight));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(80/255.f, 80/255.f, 80/255.f, 1.0f));
    bool hideRequested = false;
    if (ImGui::Begin("Now Playing##notebot-hud", nullptr, flags)) {
        const float closeWidth = ImGui::CalcTextSize("X").x +
            style.FramePadding.x * 2.0f + style.ItemSpacing.x;
        const ImVec2 titlePos = ImGui::GetCursorScreenPos();
        const float titleWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - closeWidth);
        ImGui::InvisibleButton("##playback-hud-drag", ImVec2(titleWidth, ImGui::GetFrameHeight()));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(pos.x + delta.x, pos.y + delta.y));
        }
        const auto noSong = notebot::ui::translate("notebot.ui.no_song", "No song");
        const char* title = ui.songTitle.empty() ? noSong.c_str() : ui.songTitle.c_str();
        const float titleX = titlePos.x + std::max(0.0f,
            (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title).x) * 0.5f);
        const float titleY = titlePos.y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f;
        ImGui::GetWindowDrawList()->PushClipRect(titlePos,
            ImVec2(titlePos.x + titleWidth, titlePos.y + ImGui::GetFrameHeight()), true);
        ImGui::GetWindowDrawList()->AddText(ImVec2(titleX, titleY),
                                             ImGui::GetColorU32(ImGuiCol_Text), title);
        ImGui::GetWindowDrawList()->PopClipRect();
        ImGui::SameLine();
        hideRequested = ImGui::SmallButton("X##hide-playback-hud");
        const float progress = ui.lastTick > 0
            ? std::clamp(static_cast<float>(ui.currentTick) /
                         static_cast<float>(ui.lastTick), 0.0f, 1.0f)
            : 0.0f;
        const std::string progressText = ui.songLoaded
            ? formatSongTime(ui.currentTick) + " / " + formatSongTime(ui.lastTick)
            : noSong;
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), "");
        const ImVec2 barMin = ImGui::GetItemRectMin();
        const ImVec2 barMax = ImGui::GetItemRectMax();
        const ImVec2 textSize = ImGui::CalcTextSize(progressText.c_str());
        const ImVec2 textPos((barMin.x + barMax.x - textSize.x) * 0.5f,
                             (barMin.y + barMax.y - textSize.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(
            textPos, ImGui::GetColorU32(ImGuiCol_Text), progressText.c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor();
    if (hideRequested) ui.showNowPlaying = false;
}

LRESULT WINAPI wndProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wp, lp)) return true;
    switch (message) {
    case WM_SIZE:
        if (device && wp != SIZE_MINIMIZED) {
            cleanupTarget();
            swapChain->ResizeBuffers(0, static_cast<UINT>(LOWORD(lp)), static_cast<UINT>(HIWORD(lp)), DXGI_FORMAT_UNKNOWN, 0);
            createTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ULONG_PTR gdiplusToken=0;
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken,&gdiplusInput,nullptr)!=Gdiplus::Ok) return 1;
    WNDCLASSEXW windowClass{sizeof(windowClass), CS_CLASSDC, wndProc, 0, 0, instance,
        nullptr, nullptr, nullptr, nullptr, L"MeteorNotebotImGui", nullptr};
    RegisterClassExW(&windowClass);
    HWND window = CreateWindowExW(0,
        windowClass.lpszClassName, L"Meteor Notebot UI", WS_OVERLAPPEDWINDOW,
        100, 100, 1100, 760, nullptr, nullptr, instance, nullptr);
    if (!createDevice(window)) { DestroyWindow(window); UnregisterClassW(windowClass.lpszClassName, instance); Gdiplus::GdiplusShutdown(gdiplusToken); return 1; }
    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = style.FrameRounding = style.ChildRounding = 0;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(80/255.f, 80/255.f, 80/255.f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(20/255.f, 20/255.f, 20/255.f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(20/255.f, 20/255.f, 20/255.f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(30/255.f, 30/255.f, 30/255.f, 200/255.f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(40/255.f, 40/255.f, 40/255.f, 200/255.f);
    style.Colors[ImGuiCol_Header] = ImVec4(145/255.f, 61/255.f, 226/255.f, 1);
    style.Colors[ImGuiCol_HeaderHovered] = style.Colors[ImGuiCol_Header];
    style.Colors[ImGuiCol_CheckMark] = style.Colors[ImGuiCol_Header];
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(130/255.f, 0, 1, 1);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(150/255.f, 60/255.f, 1, 1);
    style.Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 1);
    wchar_t executablePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    const auto fontPath = std::filesystem::path(executablePath).parent_path() / "assets" / "Comfortaa.ttf";
    const auto assetDirectory = std::filesystem::path(executablePath).parent_path() / "assets";
    if (consoleLoggingEnabled(std::filesystem::path(executablePath).parent_path() / "notebot.ini")) {
        enableConsoleLogging();
        logEvent("NoteBot UI started; console logging enabled.");
    }
    iconViews[0]=loadPngIcon(device,assetDirectory/L"reset.png");
    iconViews[1]=loadPngIcon(device,assetDirectory/L"copy.png");
    iconViews[2]=loadPngIcon(device,assetDirectory/L"paste.png");
    std::array<std::vector<unsigned char>, 3> fontStorage;
    if (!addFontFromPath(io.Fonts, fontStorage[0], fontPath, false)) io.Fonts->AddFontDefault();
    wchar_t windowsDirectory[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    const auto systemFonts = std::filesystem::path(windowsDirectory) / L"Fonts";
    auto mergeFirstFont = [&](size_t slot, std::initializer_list<const wchar_t*> candidates) {
        for (const wchar_t* name : candidates)
            if (addFontFromPath(io.Fonts, fontStorage[slot], systemFonts / name, true)) return true;
        return false;
    };
    const bool chineseFont = mergeFirstFont(1, {L"msyh.ttc", L"NotoSansSC-VF.ttf", L"simhei.ttf"});
    const bool japaneseFont = mergeFirstFont(2, {L"YuGothR.ttc", L"msgothic.ttc", L"meiryo.ttc"});
    if (!chineseFont || !japaneseFont) logEvent("A Chinese or Japanese system font was unavailable.");
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device, context);

    std::filesystem::path directory = std::filesystem::path(executablePath).parent_path() / "songs";
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool screenshotMode = argv && argc > 1 &&
        (std::wstring(argv[1]) == L"--screenshot-alpha" || std::wstring(argv[1]) == L"--screenshot" || std::wstring(argv[1]) == L"--screenshot-songs"
         || std::wstring(argv[1]) == L"--screenshot-bottom" || std::wstring(argv[1]) == L"--screenshot-color"
         || std::wstring(argv[1]) == L"--screenshot-color-bottom" || std::wstring(argv[1]) == L"--screenshot-playing");
    const bool screenshotAlpha = argv && argc > 1 && std::wstring(argv[1]) == L"--screenshot-alpha";
    const bool screenshotSongs = argv && argc > 1 && std::wstring(argv[1]) == L"--screenshot-songs";
    const bool screenshotBottom = argv && argc > 1 && std::wstring(argv[1]) == L"--screenshot-bottom";
    const bool screenshotColor = argv && argc > 1 && std::wstring(argv[1]) == L"--screenshot-color";
    const bool screenshotColorBottom = argv && argc > 1 && std::wstring(argv[1]) == L"--screenshot-color-bottom";
    const bool screenshotPlaying = argv && argc > 1 && std::wstring(argv[1]) == L"--screenshot-playing";
    if (argv && argc > 1 && !screenshotMode) directory = argv[1];
    if (argv && argc > 2 && screenshotMode) directory = argv[2];
    if (argv) LocalFree(argv);
    meteor::NotebotUi ui(directory);
    ui.resetIcon=reinterpret_cast<std::uintptr_t>(iconViews[0]);
    ui.copyIcon=reinterpret_cast<std::uintptr_t>(iconViews[1]);
    ui.pasteIcon=reinterpret_cast<std::uintptr_t>(iconViews[2]);
    ui.keybind = "L";
    ui.showSongs = screenshotSongs;
    ui.showModule = !screenshotSongs;
    ui.scrollModuleToBottom = screenshotBottom;
    ui.scrollColorToBottom = screenshotColorBottom;
    if (screenshotColor || screenshotColorBottom) ui.openColorEditor(0);
    if (screenshotPlaying) {
        ui.playing = true;
        ui.songLoaded = true;
        ui.songTitle = "Example song";
        ui.foundNoteBlocks = 16;
        ui.requiredNotes = 16;
        ui.currentTick = 128;
        ui.lastTick = 420;
        ui.scrollModuleToBottom = true;
    }
    ui.actions.loadSong = [&](const std::filesystem::path& path) {
        ui.songLoaded = false;
        ui.playing = false;
        ui.currentTick = 0;
        ui.songTitle.clear();
        ui.statusOverride = "Loading song...";
        if (const auto lastTick = meteor::songLastTick(path)) {
            ui.lastTick = *lastTick;
            ui.songTitle = utf8(path.stem());
            ui.songLoaded = true;
            ui.playing = true;
            ui.statusOverride.clear();
            logEvent("Loaded song: " + utf8(path.filename()));
        } else {
            ui.statusOverride = "Could not read song.";
            logEvent("Could not read song: " + utf8(path.filename()));
        }
    };
    ui.actions.previewSong = [&](const std::filesystem::path& path) {
        ui.actions.loadSong(path);
        if (ui.songLoaded) logEvent("Previewing song: " + utf8(path.filename()));
    };
    ui.actions.alignCenter = [&] {
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        const ImVec2 size = ImGui::GetWindowSize();
        ImGui::SetWindowPos(ImVec2(center.x - size.x / 2, center.y - size.y / 2));
    };
    ui.actions.pauseResume = [&] {
        if (ui.songLoaded) {
            ui.playing = !ui.playing;
            logEvent(ui.playing ? "Playback resumed." : "Playback paused.");
        }
    };
    ui.actions.stop = [&] {
        ui.playing = false;
        ui.songLoaded = false;
        ui.songTitle.clear();
        ui.statusOverride.clear();
        ui.currentTick = 0;
        ui.lastTick = 0;
        logEvent("Playback stopped.");
    };
    ui.actions.settingsChanged = [&](const meteor::NotebotSettings&) { logEvent("NoteBot settings changed."); };
    ui.actions.playbackBindChanged = [&](const std::string& bind) { logEvent("Playback key bind changed to " + bind + "."); };
    ui.actions.bindChanged = [&](const std::string& bind) { logEvent("Key bind changed to " + bind + "."); };
    ui.actions.scanNoteBlocks = [&] { ui.statusOverride = "Scanning note blocks requires the Minecraft mod."; };
    ui.actions.clearNoteBlocks = [&] {
        ui.foundNoteBlocks = 0;
        ui.requiredNotes = 0;
        ui.missingNotes.clear();
        ui.statusOverride = "Note blocks cleared.";
    };
    bool resetNowPlayingPosition = false;
    ui.actions.resetNowPlayingPosition = [&] { resetNowPlayingPosition = true; };
    ui.actions.nowPlayingVisibilityChanged = [&](bool visible) {
        logEvent(visible ? "Now Playing shown." : "Now Playing hidden.");
    };

    bool pendingScreenshot = false;
    ui.screenshotCallback = [&] { pendingScreenshot = true; };

    bool done = false;
    int screenshotFrames = 0;
    const auto openedAt = std::chrono::steady_clock::now();
    auto nextPlaybackTick = openedAt + std::chrono::milliseconds(50);
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        RECT rect; GetClientRect(window, &rect);
        const int width = rect.right - rect.left, height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0 || !renderTarget) { Sleep(10); continue; }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (!ui.isBindingKey() && !io.WantTextInput) {
            for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
                const auto imguiKey = static_cast<ImGuiKey>(key);
                if (!ImGui::IsKeyPressed(imguiKey, false)) continue;
                const std::string name = ImGui::GetKeyName(imguiKey);
                if (name == ui.keybind) {
                    const bool visible = ui.showModule || ui.showSongs;
                    ui.showModule = !visible;
                    ui.showSongs = false;
                } else if (name == ui.playbackKeybind && ui.actions.pauseResume) {
                    ui.actions.pauseResume();
                }
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (!screenshotPlaying && ui.playing && ui.songLoaded) {
            while (now >= nextPlaybackTick && ui.playing) {
                if (ui.currentTick > ui.lastTick) {
                    ui.actions.stop();
                    break;
                }
                ++ui.currentTick;
                nextPlaybackTick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(0.05 / std::clamp(ui.settings.playbackSpeed, 0.02f, 2.0f)));
            }
        } else nextPlaybackTick = now + std::chrono::milliseconds(50);
        ui.updateRainbowColors();
        ui.draw();
        if (ui.showNowPlaying && ui.songLoaded) drawNowPlaying(ui, resetNowPlayingPosition);
        ImGui::Render();
        const float clear[4] = {96/255.f, 96/255.f, 96/255.f, 1.0f};
        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        context->ClearRenderTargetView(renderTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (pendingScreenshot) {
            pendingScreenshot = false;
            if (ui.lastWindowBounds.width > 0 && ui.lastWindowBounds.height > 0) {
                wchar_t documentsPath[MAX_PATH]{};
                if (SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documentsPath) == S_OK) {
                    SYSTEMTIME time{};
                    GetLocalTime(&time);
                    wchar_t filename[128]{};
                    std::swprintf(filename, 128, L"NoteBot_Screenshot_%04d%02d%02d_%02d%02d%02d.png",
                        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
                    std::filesystem::path screenshotPath = std::filesystem::path(documentsPath) / filename;
                    const ImVec2 pos(ui.lastWindowBounds.x, ui.lastWindowBounds.y);
                    const ImVec2 size(ui.lastWindowBounds.width, ui.lastWindowBounds.height);
                    if (meteor::saveUiScreenshotPng(device, context, swapChain, renderTarget, screenshotPath, pos, size)) {
                        logEvent("Screenshot saved: " + utf8(screenshotPath));
                    } else {
                        logEvent("Failed to save screenshot.");
                    }
                } else {
                    logEvent("Could not determine Documents folder path.");
                }
            } else {
                logEvent("No active UI window to screenshot.");
            }
        }

        if (screenshotMode) ++screenshotFrames;
        if (screenshotMode && screenshotFrames >= 3 && std::chrono::steady_clock::now() - openedAt > std::chrono::milliseconds(250)) {
            if (screenshotAlpha) {
                const auto& bounds = ui.lastWindowBounds;
                meteor::saveUiScreenshotPng(device, context, swapChain, renderTarget, std::filesystem::current_path() / "build" / "preview-alpha.png",
                    ImVec2(bounds.x, bounds.y), ImVec2(bounds.width, bounds.height));
            }
            meteor::saveBackBufferBmp(device, context, swapChain, std::filesystem::current_path() / "build" /
                (screenshotPlaying ? "preview-playing.bmp" : screenshotBottom ? "preview-bottom.bmp" : screenshotColor ? "preview-color.bmp" :
                 screenshotColorBottom ? "preview-color-bottom.bmp" : screenshotSongs ? "preview-songs.bmp" : "preview.bmp")); done = true;
        }
        swapChain->Present(1, 0);
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    logEvent("NoteBot UI stopped.");
    cleanupDevice();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    return 0;
}
