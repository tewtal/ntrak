#include <glad/glad.h>

#include "ntrak/app/App.hpp"
#include "ntrak/app/AppState.hpp"
#include "ntrak/audio/AudioEngine.hpp"
#include "ntrak/common/Logger.hpp"
#include "ntrak/common/Paths.hpp"
#include "ntrak/ui/AramUsagePanel.hpp"
#include "ntrak/ui/AssetsPanel.hpp"
#include "ntrak/ui/BuildPanel.hpp"
#include "ntrak/ui/ControlPanel.hpp"
#include "ntrak/ui/PatternEditorPanel.hpp"
#include "ntrak/ui/ProjectPanel.hpp"
#include "ntrak/ui/QuickGuidePanel.hpp"
#include "ntrak/ui/SequenceEditorPanel.hpp"
#include "ntrak/ui/SpcInfoPanel.hpp"
#include "ntrak/ui/SpcPlayerPanel.hpp"
#include "ntrak/ui/UiManager.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <iostream>
#include <memory>

#ifdef __linux__
#include <gtk/gtk.h>
#endif

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
constexpr int kWindowWidth = 1920;
constexpr int kWindowHeight = 1080;
}  // namespace

namespace ntrak::app {

Fonts App::fonts_ = {};

void App::loadFonts(ImGuiIO& io) {
    // Try to load custom fonts, but use fallback if they fail
    auto monoPath = ntrak::common::assetPath("NotoSansMono.ttf");
    auto jerseyPath = ntrak::common::assetPath("Jersey10-Regular.ttf");
    auto vt323Path = ntrak::common::assetPath("VT323-Regular.ttf");

    fonts_.mono = io.Fonts->AddFontFromFileTTF(monoPath.string().c_str(), 18.0f);
    fonts_.jersey = io.Fonts->AddFontFromFileTTF(jerseyPath.string().c_str(), 20.0f);
    fonts_.vt323 = io.Fonts->AddFontFromFileTTF(vt323Path.string().c_str(), 20.0f);

    ImFont* fallback = io.Fonts->AddFontDefault();
    if (!fonts_.mono) {
        fonts_.mono = fallback;
#ifdef _WIN32
        std::string msg = "Warning: Could not load font: " + monoPath.string();
        OutputDebugStringA(msg.c_str());
#endif
    }
    if (!fonts_.jersey) {
        fonts_.jersey = fallback;
    }
    if (!fonts_.vt323) {
        fonts_.vt323 = fallback;
    }
}

const Fonts& App::fonts() {
    return fonts_;
}

int App::run() {
    ntrak::common::Logger::init();
    ntrak::common::Logger::log("Starting ntrak...");

#ifdef __linux__
    gtk_init_check(0, NULL);
#endif

    glfwSetErrorCallback([](int error, const char* description) {
        ntrak::common::Logger::logError("GLFW Error " + std::to_string(error) + ": " + description);
#ifdef _WIN32
        std::string msg = "GLFW Error " + std::to_string(error) + ": " + description;
        MessageBoxA(nullptr, msg.c_str(), "ntrak - Graphics Error", MB_ICONERROR | MB_OK);
#else
        std::cerr << "GLFW Error " << error << ": " << description << '\n';
#endif
    });

    if (!glfwInit()) {
        ntrak::common::Logger::logError("Failed to initialize GLFW");
#ifdef _WIN32
        MessageBoxA(nullptr, "Failed to initialize GLFW.\n\nPlease update your graphics drivers.",
                    "ntrak - Initialization Error", MB_ICONERROR | MB_OK);
#endif
        ntrak::common::Logger::shutdown();
        return 1;
    }
    ntrak::common::Logger::log("GLFW initialized successfully");

    // Use OpenGL 3.3 for better compatibility (especially with Intel integrated graphics)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#if __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    ntrak::common::Logger::log("Creating window (OpenGL 3.3)...");
    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "ntrak", nullptr, nullptr);
    if (!window) {
        ntrak::common::Logger::logError("Failed to create window - GPU may not support OpenGL 3.3");
#ifdef _WIN32
        MessageBoxA(nullptr,
                    "Failed to create window.\n\n"
                    "Your graphics card may not support OpenGL 3.3.\n"
                    "Please update your graphics drivers or try a different graphics device.",
                    "ntrak - Graphics Error", MB_ICONERROR | MB_OK);
#endif
        glfwTerminate();
        ntrak::common::Logger::shutdown();
        return 1;
    }
    ntrak::common::Logger::log("Window created successfully");

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
#ifdef _WIN32
        MessageBoxA(nullptr,
                    "Failed to load OpenGL functions.\n\n"
                    "Please update your graphics drivers.",
                    "ntrak - OpenGL Error", MB_ICONERROR | MB_OK);
#endif
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Enable docking
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Verify assets directory exists
    auto assetsDir = ntrak::common::executableDir() / "assets";
    ntrak::common::Logger::log("Checking assets directory: " + assetsDir.string());
    std::error_code ec;
    if (!std::filesystem::exists(assetsDir, ec) || ec) {
        ntrak::common::Logger::logError("Assets directory not found at: " + assetsDir.string());
#ifdef _WIN32
        std::string msg = "Assets folder not found at:\n" + assetsDir.string() +
                         "\n\nPlease ensure the 'assets' folder is in the same directory as ntrak.exe";
        MessageBoxA(nullptr, msg.c_str(), "ntrak - Missing Assets", MB_ICONERROR | MB_OK);
#endif
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        ntrak::common::Logger::shutdown();
        return 1;
    }
    ntrak::common::Logger::log("Assets directory found");

    ntrak::app::App::loadFonts(io);
    io.FontDefault = ntrak::app::App::fonts().mono;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Use GLSL 330 for OpenGL 3.3 compatibility
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
#ifdef _WIN32
        MessageBoxA(nullptr,
                    "Failed to initialize ImGui OpenGL3 renderer.\n\n"
                    "Please update your graphics drivers.",
                    "ntrak - Rendering Error", MB_ICONERROR | MB_OK);
#endif
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    ntrak::common::Logger::log("Initializing audio engine...");
    ntrak::audio::AudioEngine audio_engine;
    const bool audio_ready = audio_engine.initialize();
    if (audio_ready) {
        ntrak::common::Logger::log("Audio engine initialized");
    } else {
        ntrak::common::Logger::log("Audio engine failed to initialize (non-critical)");
    }

    ntrak::app::AppState app_state;
    if (audio_ready) {
        app_state.spcPlayer = std::make_unique<ntrak::audio::SpcPlayer>(audio_engine);
    }

    ntrak::ui::UiManager ui_manager(app_state);
    ui_manager.addPanel(std::make_unique<ntrak::ui::ProjectPanel>(app_state));
    ui_manager.addPanel(std::make_unique<ntrak::ui::AssetsPanel>(app_state));
    ui_manager.addPanel(std::make_unique<ntrak::ui::PatternEditorPanel>(app_state));
    ui_manager.addPanel(std::make_unique<ntrak::ui::SequenceEditorPanel>(app_state));
    ui_manager.addPanel(std::make_unique<ntrak::ui::SpcPlayerPanel>(app_state));
    ui_manager.addPanel(std::make_unique<ntrak::ui::SpcInfoPanel>(app_state));
    ui_manager.addPanel(std::make_unique<ntrak::ui::ControlPanel>(app_state));
    ui_manager.addPanel(std::make_unique<ntrak::ui::BuildPanel>(app_state));
    ui_manager.addPanel(std::make_unique<ntrak::ui::AramUsagePanel>(app_state));
    ui_manager.addPanel(std::make_unique<ntrak::ui::QuickGuidePanel>(app_state));
    ui_manager.setExitCallback([window]() { glfwSetWindowShouldClose(window, GLFW_TRUE); });

    // Set status callback to show audio/transport info
    ui_manager.setStatusCallback([&audio_engine, audio_ready]() {
        if (!audio_ready) {
            ImGui::TextDisabled("Audio: N/A");
        } else {
        }
    });

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui_manager.draw();

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    ntrak::common::Logger::log("Shutting down...");
    glfwDestroyWindow(window);
    glfwTerminate();
    audio_engine.shutdown();
    ntrak::common::Logger::log("Shutdown complete");
    ntrak::common::Logger::shutdown();
    return 0;
}

}  // namespace ntrak::app
