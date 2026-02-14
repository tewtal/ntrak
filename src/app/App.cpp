#include <glad/glad.h>

#include "ntrak/app/App.hpp"
#include "ntrak/app/AppState.hpp"
#include "ntrak/audio/AudioEngine.hpp"
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
#include <memory>

#ifdef __linux__
#include <gtk/gtk.h>
#endif

namespace {
constexpr int kWindowWidth = 1920;
constexpr int kWindowHeight = 1080;
}  // namespace

namespace ntrak::app {

Fonts App::fonts_ = {};

void App::loadFonts(ImGuiIO& io) {
    fonts_.mono = io.Fonts->AddFontFromFileTTF(ntrak::common::assetPath("NotoSansMono.ttf").string().c_str(), 18.0f);
    fonts_.jersey = io.Fonts->AddFontFromFileTTF(ntrak::common::assetPath("Jersey10-Regular.ttf").string().c_str());
    fonts_.vt323 = io.Fonts->AddFontFromFileTTF(ntrak::common::assetPath("VT323-Regular.ttf").string().c_str());

    ImFont* fallback = io.Fonts->AddFontDefault();
    if (!fonts_.mono) {
        fonts_.mono = fallback;
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
#ifdef __linux__
    gtk_init_check(0, NULL);
#endif

    if (!glfwInit()) {
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#if __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "ntrak", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Enable docking
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ntrak::app::App::loadFonts(io);
    io.FontDefault = ntrak::app::App::fonts().mono;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 450")) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    ntrak::audio::AudioEngine audio_engine;
    const bool audio_ready = audio_engine.initialize();

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

    glfwDestroyWindow(window);
    glfwTerminate();
    audio_engine.shutdown();
    return 0;
}

}  // namespace ntrak::app
