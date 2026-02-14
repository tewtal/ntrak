#include "ntrak/common/Log.hpp"

#include <iostream>

namespace ntrak::common {

void logInfo(std::string_view message) {
    std::cout << "[info] " << message << '\n';
}

}  // namespace ntrak::common
