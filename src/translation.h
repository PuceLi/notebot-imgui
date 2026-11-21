#pragma once
#include <format>
#include <string>
#include <string_view>

namespace notebot::ui {
std::string translate(std::string_view key, std::string_view fallback);

template<class... Args>
std::string formatText(std::string_view text, Args&&... args) {
    return std::vformat(text, std::make_format_args(args...));
}
template<class... Args>
std::string format(std::string_view key, std::string_view fallback, Args&&... args) {
    return formatText(translate(key, fallback), args...);
}
} // namespace notebot::ui
