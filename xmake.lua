add_requires("imgui v1.93.0", {configs = {shared = false, win32 = true, dx11 = true, dx12 = true}})

target("meteor-notebot-ui")
    set_kind("binary")
    add_files("src/*.cpp")
	set_languages("c++20")
    add_packages("imgui")
    add_syslinks("d3d11", "dxgi", "imm32", "user32", "gdi32", "gdiplus", "shell32")
    add_defines("UNICODE", "_UNICODE", "NOMINMAX")
    if is_plat("windows") then
        add_cxxflags("/utf-8", {tools = "cl"})
        add_ldflags("/SUBSYSTEM:WINDOWS", {tools = "link"})
    end
    after_build(function (target)
        os.mkdir(path.join(target:targetdir(), "assets"))
        os.cp(path.join(os.projectdir(), "assets", "Comfortaa.ttf"),
              path.join(target:targetdir(), "assets", "Comfortaa.ttf"))
        for _, icon in ipairs({"reset.png", "copy.png", "paste.png"}) do
            os.cp(path.join(os.projectdir(), "assets", icon),
                  path.join(target:targetdir(), "assets", icon))
        end
    end)
