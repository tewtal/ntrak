#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/nspc/ItImport.hpp"
#include "ntrak/nspc/NspcProject.hpp"
#include "ntrak/ui/Panel.hpp"
#include "ntrak/ui/SongPortDialog.hpp"

#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ImGuiContext;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

namespace ntrak::ui {

/// Manages dockable panel windows with a main dockspace layout
class UiManager {
public:
    explicit UiManager(app::AppState& appState);

    /// Add a panel to be managed (drawn as its own dockable window)
    void addPanel(std::unique_ptr<Panel> panel);

    /// Get a panel by title (returns nullptr if not found)
    Panel* panel(const std::string& title);

    /// Draw the entire UI (title bar, dockspace, all widget windows)
    void draw();

    /// Set callback for menu bar content (drawn inside the title bar menu)
    using MenuCallback = std::function<void()>;

    void setMenuCallback(MenuCallback callback) { menuCallback_ = std::move(callback); }

    /// Set callback for title bar status area (right side of title bar)
    using StatusCallback = std::function<void()>;

    void setStatusCallback(StatusCallback callback) { statusCallback_ = std::move(callback); }

    /// Set callback for app exit requests (e.g. File -> Exit).
    using ExitCallback = std::function<void()>;

    void setExitCallback(ExitCallback callback) { exitCallback_ = std::move(callback); }

    /// Get the app state
    app::AppState& appState() { return appState_; }

    /// Install a project as the active project (resets selection and playback tracking).
    void installProject(nspc::NspcProject project, std::vector<uint8_t> sourceSpcData,
                        std::optional<std::filesystem::path> sourceSpcPath = std::nullopt);

private:
    void handleGlobalShortcuts();
    void drawTitleBar();
    void drawDockspace();
    void drawPanelWindows();
    void drawWindowMenu();
    bool importSpcFromDialog();
    bool importNspcFromDialog();
    bool openProjectFromDialog();
    bool saveProjectQuick();
    bool saveProjectAsFromDialog();
    bool saveProjectToPath(const std::filesystem::path& path);
    bool exportUserDataFromDialog();
    bool exportSpcFromDialog();
    bool openUserGuide();
    void openItImportDialog();
    void drawItImportDialog();
    bool rebuildItImportPreview();
    bool executeItImportFromWorkbench();
    void drawItImportWarningsModal();
    void setFileStatus(std::string message, bool isError);
    void registerPanelVisibilitySettingsHandler();
    void parsePanelVisibilitySettingsLine(const char* line);
    void writePanelVisibilitySettings(ImGuiTextBuffer& outBuffer) const;
    void markPanelVisibilityDirty();
    static void panelVisibilitySettingsClearAll(ImGuiContext* context, ImGuiSettingsHandler* handler);
    static void* panelVisibilitySettingsReadOpen(ImGuiContext* context, ImGuiSettingsHandler* handler,
                                                 const char* name);
    static void panelVisibilitySettingsReadLine(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry,
                                                const char* line);
    static void panelVisibilitySettingsWriteAll(ImGuiContext* context, ImGuiSettingsHandler* handler,
                                                ImGuiTextBuffer* outBuffer);

    app::AppState& appState_;
    std::vector<std::unique_ptr<Panel>> panels_;
    MenuCallback menuCallback_;
    StatusCallback statusCallback_;
    ExitCallback exitCallback_;
    std::string fileStatus_;
    bool fileStatusIsError_ = false;
    std::optional<std::filesystem::path> currentProjectPath_;

    SongPortDialog songPortDialog_;
    bool firstFrame_ = true;
    std::unordered_map<std::string, bool> persistedPanelVisibility_;
    std::vector<std::string> itImportWarnings_;
    bool showItImportWarnings_ = false;
    bool pendingOpenItImportDialog_ = false;
    std::optional<std::filesystem::path> itImportPath_;
    nspc::ItImportOptions itImportOptions_{};
    std::optional<nspc::ItImportPreview> itImportPreview_;
    std::string itImportDialogError_;
};

}  // namespace ntrak::ui
