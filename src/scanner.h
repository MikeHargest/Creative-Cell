#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "library.h"

// Every model file under `folder` (all levels; folders starting with "."
// skipped), with size and time. Stops early when `cancel` is set.
std::vector<FileEntry> scanFolder(const std::string& folder, const std::atomic<bool>* cancel = nullptr);

// One file's entry; false when it isn't there.
bool statFile(const std::string& path, FileEntry& out);
