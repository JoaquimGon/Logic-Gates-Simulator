#pragma once

#include <string_view>

struct GLFWwindow;

/** @brief Display the embedded project and dependency notices in a scrollable native window. */
void showLicenseNotices(GLFWwindow* parent, std::string_view notices);
