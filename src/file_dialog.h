#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

// Native Windows dialogs (paths UTF-8; empty — cancelled). COM must be
// initialized on the calling thread.
std::vector<std::string> openModelFilesDialog(GLFWwindow* window);
std::string pickFolderDialog(GLFWwindow* window, const char* title);
