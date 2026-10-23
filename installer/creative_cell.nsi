; Creative Cell - Windows installer (NSIS 3, Modern UI 2).
; Build: installer\build_installer.ps1 (after a Release build).
; Picks the install folder, Start menu / desktop shortcuts and the Explorer
; integration (open 3D files in the viewer, thumbnails); registers an
; uninstaller in "Apps".

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define APP_NAME      "Creative Cell"
!define APP_EXE       "CreativeCell.exe"
!define THUMB_DLL     "cc_thumbs.dll"
!ifndef APP_VERSION
  !define APP_VERSION "0.2.1"
!endif
!define APP_PUBLISHER "Mike Hargest"
!define APP_KEY       "Software\CreativeCell"
!define UNINST_KEY    "Software\Microsoft\Windows\CurrentVersion\Uninstall\CreativeCell"
; Kept in step with src/shell_ids.h.
!define PROG_ID       "CreativeCell.Model"
!define THUMB_CLSID   "{A754A1D3-E473-47F8-A3AC-F53738315A69}"
!define THUMB_SHELLEX "{e357fccd-a995-4576-b01f-234630154e96}"
!ifndef BUILD_DIR
  !define BUILD_DIR "..\build\bin\Release"
!endif

Name "${APP_NAME}"
OutFile "..\dist\CreativeCell_Setup_${APP_VERSION}.exe"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "${APP_KEY}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${APP_NAME} ${APP_VERSION}"

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "FileDescription" "${APP_NAME} Setup"
VIAddVersionKey "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "${APP_PUBLISHER}"

!define MUI_ICON "app.ico"
!define MUI_UNICON "app.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_WELCOMEPAGE_TITLE "Welcome to ${APP_NAME} ${APP_VERSION} Setup"
!define MUI_WELCOMEPAGE_TEXT "This will install ${APP_NAME}, a library and viewer for 3D models (FBX, OBJ, glTF, GLB, STL), on your computer.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_TITLE "${APP_NAME} is installed"
!define MUI_FINISHPAGE_TEXT "To open 3D files from Explorer with a double-click: right-click a model > Open with > Choose another app > ${APP_NAME} > Always (once per format). Windows doesn't let programs set this by themselves."
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

!macro ForEachExt MACRO
  !insertmacro ${MACRO} ".fbx"
  !insertmacro ${MACRO} ".obj"
  !insertmacro ${MACRO} ".gltf"
  !insertmacro ${MACRO} ".glb"
  !insertmacro ${MACRO} ".stl"
!macroend

!macro RegisterExt EXT
  WriteRegStr HKLM "Software\Classes\${EXT}\OpenWithProgids" "${PROG_ID}" ""
  WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}\SupportedTypes" "${EXT}" ""
  WriteRegStr HKLM "Software\Classes\${EXT}\ShellEx\${THUMB_SHELLEX}" "" "${THUMB_CLSID}"
!macroend

!macro UnregisterExt EXT
  DeleteRegValue HKLM "Software\Classes\${EXT}\OpenWithProgids" "${PROG_ID}"
  ; The thumbnail slot only if it's still ours.
  ReadRegStr $0 HKLM "Software\Classes\${EXT}\ShellEx\${THUMB_SHELLEX}" ""
  ${If} $0 == "${THUMB_CLSID}"
    DeleteRegKey HKLM "Software\Classes\${EXT}\ShellEx\${THUMB_SHELLEX}"
  ${EndIf}
!macroend

; A copy registered from Settings (HKCU, e.g. a development build) would
; win over the installed one: its keys go.
!macro UnregisterUserExt EXT
  DeleteRegValue HKCU "Software\Classes\${EXT}\OpenWithProgids" "${PROG_ID}"
  ReadRegStr $0 HKCU "Software\Classes\${EXT}\ShellEx\${THUMB_SHELLEX}" ""
  ${If} $0 == "${THUMB_CLSID}"
    DeleteRegKey HKCU "Software\Classes\${EXT}\ShellEx\${THUMB_SHELLEX}"
  ${EndIf}
!macroend

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${APP_NAME} needs 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "${APP_NAME} (required)" SecApp
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${BUILD_DIR}\${APP_EXE}"
  ; Explorer may hold the old thumbnail DLL loaded: moved aside (allowed on a
  ; loaded file), the new one copied, the old one deleted now or at reboot.
  ${If} ${FileExists} "$INSTDIR\${THUMB_DLL}"
    Delete "$INSTDIR\${THUMB_DLL}.old"
    Rename "$INSTDIR\${THUMB_DLL}" "$INSTDIR\${THUMB_DLL}.old"
    Delete /REBOOTOK "$INSTDIR\${THUMB_DLL}.old"
  ${EndIf}
  File "${BUILD_DIR}\${THUMB_DLL}"
  File "app.ico"
  File "..\LICENSE"
  File "..\THIRD_PARTY_LICENSES.txt"
  SetOutPath "$INSTDIR\assets\fonts"
  File "..\assets\fonts\Inter.ttf"
  File "..\assets\fonts\Inter-OFL.txt"
  File "..\assets\fonts\JetBrainsMono-Regular.ttf"
  File "..\assets\fonts\JetBrainsMono-OFL.txt"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${APP_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  SectionGetSize ${SecApp} $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Section "Start menu shortcut" SecStartMenu
  SetShellVarContext all
  CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
SectionEnd

Section "Desktop shortcut" SecDesktop
  SetShellVarContext all
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
SectionEnd

Section "Explorer integration" SecShell
  ; The thumbnail handler.
  WriteRegStr HKLM "Software\Classes\CLSID\${THUMB_CLSID}" "" "Creative Cell Thumbnails"
  WriteRegStr HKLM "Software\Classes\CLSID\${THUMB_CLSID}\InprocServer32" "" "$INSTDIR\${THUMB_DLL}"
  WriteRegStr HKLM "Software\Classes\CLSID\${THUMB_CLSID}\InprocServer32" "ThreadingModel" "Apartment"
  ; The file type Creative Cell opens (in its viewer, not into the library).
  WriteRegStr HKLM "Software\Classes\${PROG_ID}" "" "3D Model"
  WriteRegStr HKLM "Software\Classes\${PROG_ID}\DefaultIcon" "" '"$INSTDIR\${APP_EXE}",0'
  WriteRegStr HKLM "Software\Classes\${PROG_ID}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'
  WriteRegStr HKLM "Software\Classes\${PROG_ID}\ShellEx\${THUMB_SHELLEX}" "" "${THUMB_CLSID}"
  WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}" "FriendlyAppName" "${APP_NAME}"
  WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'
  !insertmacro ForEachExt RegisterExt
  ; This user's own registration (from Settings) steps aside.
  DeleteRegKey HKCU "Software\Classes\CLSID\${THUMB_CLSID}"
  DeleteRegKey HKCU "Software\Classes\${PROG_ID}"
  DeleteRegKey HKCU "Software\Classes\Applications\${APP_EXE}"
  !insertmacro ForEachExt UnregisterUserExt
  WriteRegDWORD HKLM "${APP_KEY}" "ShellIntegration" 1
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "The program itself."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "${APP_NAME} in the Start menu."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A shortcut on the desktop."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecShell} "Thumbnails of 3D files in Explorer, and ${APP_NAME} in Open with: files open in its viewer without going into the library."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  SetShellVarContext all
  ; Explorer integration (only what is ours).
  DeleteRegKey HKLM "Software\Classes\CLSID\${THUMB_CLSID}"
  DeleteRegKey HKLM "Software\Classes\${PROG_ID}"
  DeleteRegKey HKLM "Software\Classes\Applications\${APP_EXE}"
  !insertmacro ForEachExt UnregisterExt
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  Delete "$INSTDIR\${APP_EXE}"
  Delete /REBOOTOK "$INSTDIR\${THUMB_DLL}"
  Delete /REBOOTOK "$INSTDIR\${THUMB_DLL}.old"
  Delete "$INSTDIR\app.ico"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\THIRD_PARTY_LICENSES.txt"
  Delete "$INSTDIR\assets\fonts\Inter.ttf"
  Delete "$INSTDIR\assets\fonts\Inter-OFL.txt"
  Delete "$INSTDIR\assets\fonts\JetBrainsMono-Regular.ttf"
  Delete "$INSTDIR\assets\fonts\JetBrainsMono-OFL.txt"
  RMDir "$INSTDIR\assets\fonts"
  RMDir "$INSTDIR\assets"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /REBOOTOK "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}.lnk"
  Delete "$DESKTOP\${APP_NAME}.lnk"

  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "${APP_KEY}"
  ; The library and thumbnails in %LOCALAPPDATA%\CreativeCell stay: they
  ; belong to the user.
SectionEnd
