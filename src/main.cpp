#include "gl_loader.h"

#include <ole2.h>
#include <shellapi.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "app.h"
#include "stb/stb_image_write.h"
#include "ui_style.h"
#include "util.h"

namespace {
App* g_app = nullptr;
GLFWwindow* g_window = nullptr;

// --- one window: a second launch (Explorer's Open on another file) hands
// its files to the running one, which opens them in tabs ---
constexpr ULONG_PTR kHandOver = 0x43430001;

// One running app per library: test runs with --data stay apart.
std::wstring instanceKey() { return std::to_wstring(std::hash<std::string>{}(lower(pathU8(dataDir())))); }
std::wstring ipcClass() { return L"CreativeCell.IPC." + instanceKey(); }

// Lines "o<TAB>path" (open in a tab) and "i<TAB>path" (import); none —
// just come to the front.
bool handOver(const LaunchOptions& o) {
  HWND h = FindWindowExW(HWND_MESSAGE, nullptr, ipcClass().c_str(), nullptr);
  if (!h) return false;
  std::string payload;
  for (const std::string& p : o.openPaths) payload += "o\t" + normalizePath(p) + "\n";
  for (const std::string& p : o.importPaths) payload += "i\t" + normalizePath(p) + "\n";
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  AllowSetForegroundWindow(pid);  // (Windows lets only the foreground app give focus)
  COPYDATASTRUCT cds = {kHandOver, static_cast<DWORD>(payload.size()), payload.data()};
  DWORD_PTR result = 0;
  return SendMessageTimeoutW(h, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 5000, &result) != 0;
}

LRESULT CALLBACK ipcProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_COPYDATA && g_app) {
    const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(l);
    if (cds->dwData != kHandOver) return FALSE;
    const std::string payload(static_cast<const char*>(cds->lpData), cds->cbData);
    std::vector<std::string> open, import;
    size_t start = 0;
    for (size_t nl; (nl = payload.find('\n', start)) != std::string::npos; start = nl + 1) {
      const std::string line = payload.substr(start, nl - start);
      if (line.size() > 2 && line[1] == '\t') (line[0] == 'o' ? open : import).push_back(line.substr(2));
    }
    g_app->openFiles(open);
    g_app->importLater(import);
    HWND main = glfwGetWin32Window(g_window);
    if (IsIconic(main)) ShowWindow(main, SW_RESTORE);
    SetForegroundWindow(main);
    glfwPostEmptyEvent();
    return TRUE;
  }
  return DefWindowProcW(h, msg, w, l);
}

HWND createIpcWindow() {
  const std::wstring cls = ipcClass();
  WNDCLASSW wc = {};
  wc.lpfnWndProc = ipcProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = cls.c_str();
  RegisterClassW(&wc);
  return CreateWindowExW(0, cls.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}

void onDrop(GLFWwindow*, int count, const char** paths) {
  if (!g_app) return;
  std::vector<std::string> list(paths, paths + count);  // GLFW gives UTF-8
  g_app->onDrop(list);
}

bool g_inFrame = false;

// One frame: the app's UI, drawn into the back buffer. Its size, for the
// screenshot.
void drawFrame(GLFWwindow* window, int& w, int& h) {
  g_inFrame = true;
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  g_app->frame();
  ImGui::Render();
  glfwGetFramebufferSize(window, &w, &h);
  glViewport(0, 0, w, h);
  glClearColor(0.04f, 0.04f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  g_inFrame = false;
}

// While the window's edge is dragged, Windows runs its own loop and the
// main loop stands still: without this the last frame is stretched to the
// new size. Frames are drawn from here instead, at the size of the moment.
void onRefresh(GLFWwindow* window) {
  if (!g_app || g_inFrame) return;
  int w, h;
  drawFrame(window, w, h);
  glfwSwapBuffers(window);
}

void onFramebufferSize(GLFWwindow* window, int, int) { onRefresh(window); }

void onScale(GLFWwindow*, float xs, float) {
  ui::setScale(xs);
  ui::applyStyle();
}

// The title bar dark, the same shade as the top bar.
void darkTitleBar(GLFWwindow* window) {
  HWND hwnd = glfwGetWin32Window(window);
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
  const COLORREF caption = RGB(15, 15, 15), text = RGB(200, 200, 200), border = RGB(40, 40, 40);
  DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
  DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof text);
  DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof border);
}

LaunchOptions parseArgs() {
  LaunchOptions o;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    const std::string a = toUtf8(argv[i]);
    auto next = [&]() { return i + 1 < argc ? toUtf8(argv[++i]) : std::string(); };
    if (a == "--data") {
      setDataDir(u8(next()));
    } else if (a == "--shot") {
      o.shotPath = next();
    } else if (a == "--frames") {
      o.shotFrames = std::atoi(next().c_str());
    } else if (a == "--select") {
      o.selectIndex = std::atoi(next().c_str());
    } else if (a == "--import") {
      o.importPaths.push_back(next());
    } else if (a.rfind("--", 0) != 0) {
      // Given to the exe (Explorer's Open, a drop on the icon): a file is
      // looked at in the viewer, a folder goes into the library.
      std::error_code ec;
      if (std::filesystem::is_directory(u8(a), ec)) o.importPaths.push_back(a);
      else o.openPaths.push_back(a);
    }
  }
  LocalFree(argv);
  return o;
}
}  // namespace

int main() {
  OleInitialize(nullptr);  // COM for the dialogs, OLE for dragging files out
  const LaunchOptions opts = parseArgs();
  // Already running: the files go there, this launch ends. (Launches are
  // taken one at a time: Explorer opening several files starts several.)
  HANDLE startLock = nullptr;
  if (opts.shotPath.empty()) {
    startLock = CreateMutexW(nullptr, FALSE, (L"Local\\CreativeCell.Start." + instanceKey()).c_str());
    if (startLock) WaitForSingleObject(startLock, 10000);
    if (handOver(opts)) {
      if (startLock) ReleaseMutex(startLock), CloseHandle(startLock);
      OleUninitialize();
      return 0;
    }
  }
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  GLFWwindow* window = glfwCreateWindow(1600, 960, "Creative Cell", nullptr, nullptr);
  if (!window) {
    MessageBoxW(nullptr, L"Couldn't open an OpenGL 3.3 window.", L"Creative Cell", MB_ICONERROR);
    glfwTerminate();
    return 1;
  }
  g_window = window;
  darkTitleBar(window);
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  if (!glx::init()) {
    MessageBoxW(nullptr, L"The graphics driver is missing OpenGL 3.3 functions.", L"Creative Cell", MB_ICONERROR);
    return 1;
  }

  float xs = 1.0f, ys = 1.0f;
  glfwGetWindowContentScale(window, &xs, &ys);
  ui::setScale(xs);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  // (No keyboard navigation: the arrows move the selection in the grid.)
  ui::loadFonts();
  ui::applyStyle();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  App app;
  g_app = &app;
  if (!app.init(window, opts)) return 1;
  app.restoreWindowPlace();
  glfwShowWindow(window);
  glfwSetDropCallback(window, onDrop);
  glfwSetWindowContentScaleCallback(window, onScale);
  glfwSetWindowRefreshCallback(window, onRefresh);
  glfwSetFramebufferSizeCallback(window, onFramebufferSize);
  // Ready for the next launches' files.
  HWND ipc = createIpcWindow();
  if (startLock) ReleaseMutex(startLock);

  int frame = 0;
  while (!glfwWindowShouldClose(window) && !app.wantsQuit()) {
    // Idle: wake on input or a few times a second (background work posts
    // its results; the pulses on waiting tiles keep moving).
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
      glfwWaitEventsTimeout(0.2);
      continue;
    }
    glfwWaitEventsTimeout(1.0 / 30.0);
    int w, h;
    drawFrame(window, w, h);
    ++frame;
    if (!opts.shotPath.empty() && frame >= opts.shotFrames) {
      std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadBuffer(GL_BACK);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      stbi_flip_vertically_on_write(1);
      stbi_write_png(opts.shotPath.c_str(), w, h, 4, px.data(), w * 4);
      break;
    }
    glfwSwapBuffers(window);
  }

  if (ipc) DestroyWindow(ipc);
  if (startLock) CloseHandle(startLock);
  app.shutdown();
  g_app = nullptr;
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  OleUninitialize();
  return 0;
}
