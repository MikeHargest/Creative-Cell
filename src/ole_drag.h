#pragma once

#include <string>
#include <vector>

// Drags files out of the app to other programs (Blender, Unreal, Unity,
// Explorer...) the way Explorer does: a CF_HDROP file list. Only copy /
// link are offered, so a drop on a folder never moves a library file.
// Blocks until the drop (Windows runs its own loop meanwhile). OLE must be
// initialized on this thread (OleInitialize). True if something took it.
bool dragFilesOut(const std::vector<std::string>& utf8Paths);
