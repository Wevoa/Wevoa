#include "utils/browser_launcher.h"

#include <cstdlib>

namespace wevoaweb {

void openBrowserUrl(const std::string& url) {
#ifdef _WIN32
    const std::string command = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
    const std::string command = "open \"" + url + "\" >/dev/null 2>&1 &";
#else
    const std::string command = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
#endif

    static_cast<void>(std::system(command.c_str()));
}

}  // namespace wevoaweb
