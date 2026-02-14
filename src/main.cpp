#include "ntrak/app/App.hpp"

namespace {

int runApp() {
    ntrak::app::App app;
    return app.run();
}

}  // namespace

int main() {
    return runApp();
}

#if defined(_WIN32)
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return runApp();
}
#endif
