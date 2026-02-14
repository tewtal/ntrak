#include "ntrak/common/UserGuide.hpp"

#include "ntrak/common/Paths.hpp"

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

namespace ntrak::common {
namespace {

std::string htmlEscape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 64);
    for (const char ch : input) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::expected<std::filesystem::path, std::string> writeHtmlGuideFromMarkdown(const std::filesystem::path& mdPath) {
    std::ifstream in(mdPath, std::ios::binary);
    if (!in) {
        return std::unexpected(std::format("Failed to read user guide: {}", mdPath.string()));
    }

    const std::string markdown((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string escaped = htmlEscape(markdown);

    std::ostringstream html;
    html << "<!doctype html>\n"
            "<html lang=\"en\">\n"
            "<head>\n"
            "<meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<title>ntrak User Guide</title>\n"
            "<style>\n"
            "body{margin:0;background:#f6f8fb;color:#1f2937;font-family:Segoe UI,Arial,sans-serif;}\n"
            ".wrap{max-width:1000px;margin:24px auto;padding:0 20px;}\n"
            "h1{font-size:28px;margin:0 0 6px 0;}\n"
            "p{margin:0 0 12px 0;color:#4b5563;}\n"
            "pre{white-space:pre-wrap;word-break:break-word;background:#fff;border:1px solid #d1d5db;"
            "border-radius:10px;padding:16px;line-height:1.45;font-size:14px;}\n"
            "</style>\n"
            "</head>\n"
            "<body>\n"
            "<div class=\"wrap\">\n"
            "<h1>ntrak User Guide</h1>\n"
            "<p>Rendered from USER_GUIDE.md for browser compatibility.</p>\n"
            "<pre>"
         << escaped << "</pre>\n"
                         "</div>\n"
                         "</body>\n"
                         "</html>\n";

    std::error_code ec;
    const auto tempDir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        return std::unexpected("Failed to resolve temp directory for HTML guide");
    }
    const auto outPath = tempDir / "ntrak_user_guide.html";
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(std::format("Failed to write HTML guide: {}", outPath.string()));
    }
    const std::string htmlStr = html.str();
    out.write(htmlStr.data(), static_cast<std::streamsize>(htmlStr.size()));
    if (!out.good()) {
        return std::unexpected(std::format("Failed to flush HTML guide: {}", outPath.string()));
    }
    return outPath;
}

bool openPathInDefaultApp(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto result = reinterpret_cast<intptr_t>(
        ShellExecuteA(nullptr, "open", path.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#elif defined(__APPLE__)
    const std::string cmd = std::format("open \"{}\" >/dev/null 2>&1", path.string());
    return std::system(cmd.c_str()) == 0;
#else
    const std::string cmd = std::format("xdg-open \"{}\" >/dev/null 2>&1", path.string());
    return std::system(cmd.c_str()) == 0;
#endif
}

}  // namespace

std::expected<std::filesystem::path, std::string> openUserGuideInDefaultApp() {
    const std::filesystem::path guidePath = userGuidePath();
    if (!std::filesystem::exists(guidePath)) {
        return std::unexpected(std::format("User guide not found: {}", guidePath.string()));
    }

    auto htmlGuide = writeHtmlGuideFromMarkdown(guidePath);
    if (!htmlGuide.has_value()) {
        return std::unexpected(htmlGuide.error());
    }

    if (!openPathInDefaultApp(*htmlGuide)) {
        return std::unexpected(std::format("Failed to open user guide automatically. Open manually: {}",
                                           htmlGuide->string()));
    }

    return *htmlGuide;
}

}  // namespace ntrak::common

