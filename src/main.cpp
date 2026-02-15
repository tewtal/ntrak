#include "ntrak/app/App.hpp"
#include <exception>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

int runApp() {
    try {
        ntrak::app::App app;
        return app.run();
    } catch (const std::exception& e) {
#if defined(_WIN32)
        // On Windows, show a message box for unhandled exceptions
        std::string msg = "Fatal error: ";
        msg += e.what();
        MessageBoxA(nullptr, msg.c_str(), "ntrak Error", MB_ICONERROR | MB_OK);
#else
        std::cerr << "Fatal error: " << e.what() << '\n';
#endif
        return 1;
    } catch (...) {
#if defined(_WIN32)
        MessageBoxA(nullptr, "An unknown fatal error occurred", "ntrak Error", MB_ICONERROR | MB_OK);
#else
        std::cerr << "An unknown fatal error occurred" << '\n';
#endif
        return 1;
    }
}

}  // namespace

int main() {
    return runApp();
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return runApp();
}
#endif
