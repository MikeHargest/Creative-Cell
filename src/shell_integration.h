#pragma once

#include <string>

// Creative Cell in Windows, for the current user only (HKCU, no admin):
// "Open with" for FBX / OBJ / glTF / GLB / STL (the files open in the
// viewer, not into the library) and Explorer's thumbnails of them drawn by
// cc_thumbs.dll next to the exe. Windows doesn't let a program make itself
// the default: after this, Creative Cell is offered in "Open with" and the
// user ticks "Always" once.
bool registerShellIntegration(std::string& error);
void unregisterShellIntegration();
// Registered (by Settings or the installer), pointing at this exe's folder.
bool shellIntegrationRegistered();
// Set up by the installer for every user (its uninstaller removes it).
bool shellIntegrationByInstaller();
// Windows' Default apps page.
void openDefaultAppsSettings();
