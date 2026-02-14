#pragma once

struct ImFont;
struct ImGuiIO;

namespace ntrak::app {

struct Fonts {
    ImFont* mono = nullptr;
    ImFont* jersey = nullptr;
    ImFont* vt323 = nullptr;
};

class App {
public:
    static void loadFonts(ImGuiIO& io);
    static const Fonts& fonts();
    int run();

private:
    static Fonts fonts_;
};

}  // namespace ntrak::app
