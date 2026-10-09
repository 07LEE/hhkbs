#include "gui/MainWindow.h"
#include "gui/HangulSyllables.h"
#include "gui/Theme.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <string>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace {
volatile std::sig_atomic_t terminateRequested = 0;
extern "C" void onTerminate(int) { terminateRequested = 1; }

struct FontFile {
    std::string path;
    int index = 0;  // which face of a collection (.ttc)
};

// Asks the host's Fontconfig utility which file serves `pattern`. No font file or Fontconfig library is bundled with
// the application.
std::optional<FontFile> findFont(const char* pattern)
{
    std::string output;
    const std::string command = std::string("fc-match --format='%{file}\\n%{index}' '") + pattern + "' 2>/dev/null";
    if (FILE* query = ::popen(command.c_str(), "r")) {
        std::array<char, 1024> buffer{};
        while (const auto count = std::fread(buffer.data(), 1, buffer.size(), query))
            output.append(buffer.data(), count);
        if (::pclose(query) != 0) return std::nullopt;
    }
    const auto newline = output.find('\n');
    FontFile font{output.substr(0, newline), 0};
    if (newline != std::string::npos) font.index = std::atoi(output.c_str() + newline + 1);
    if (font.path.empty() || !std::ifstream(font.path, std::ios::binary).good()) return std::nullopt;
    return font;
}

// The interface font at `size` pixels: the configured sans-serif face, and when that has no Hangul (as most Latin
// faces have none) a Korean-capable face merged into it, so profile and backup names in Korean can be read.
void loadSystemFont(ImFontAtlas& atlas, const float size)
{
    const auto base = findFont("sans-serif");
    bool loaded = false;
    if (base) {
        ImFontConfig config;
        config.FontNo = base->index;
        loaded = atlas.AddFontFromFileTTF(base->path.c_str(), size, &config) != nullptr;
    }
    if (!loaded) {
        ImFontConfig fallback;
        fallback.SizePixels = size;
        atlas.AddFontDefault(&fallback);
    }
    const auto korean = findFont("sans-serif:lang=ko");
    if (korean && (!base || korean->path != base->path || korean->index != base->index)) {
        ImFontConfig config;
        config.MergeMode = true;  // only adds the glyphs the first face lacks
        config.FontNo = korean->index;
        // The ranges have to stay alive until the atlas is built, which is after this returns.
        static ImVector<ImWchar> ranges;
        if (ranges.empty()) {
            ImFontGlyphRangesBuilder builder;
            static const ImWchar jamo[] = {0x3131, 0x3163, 0};  // the letters on their own, as typed one by one
            builder.AddRanges(jamo);
            builder.AddText(theme::commonHangulSyllables);
            builder.BuildRanges(&ranges);
        }
        atlas.AddFontFromFileTTF(korean->path.c_str(), size, &config, ranges.Data);
    }
}

// How large to draw: HHKBS_SCALE if set, else what the desktop reports for the screen, never below 1.
float displayScale()
{
    if (const char* text = std::getenv("HHKBS_SCALE")) {
        const float requested = static_cast<float>(std::atof(text));
        if (requested >= 0.5f && requested <= 4.f) return requested;
    }
    float x = 1.f, y = 1.f;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) glfwGetMonitorContentScale(monitor, &x, &y);
    return std::clamp(std::max(x, y), 1.f, 4.f);
}

// BMP output keeps capture support independent of any image/GUI runtime.
void capture(const char* path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*4);
    glReadPixels(0,0,width,height,GL_BGRA,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream file(path,std::ios::binary);
    const auto word = [&](unsigned value, int count) {
        for (int i=0; i<count; ++i) file.put(static_cast<char>((value>>(i*8))&255));
    };
    file.write("BM",2);
    word(54+pixels.size(),4); word(0,4); word(54,4); word(40,4);
    word(width,4); word(height,4); word(1,2); word(32,2);
    word(0,4); word(pixels.size(),4); word(0,4); word(0,4); word(0,4); word(0,4);
    file.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
    file.close();
    if (!file) throw std::runtime_error("Could not save screenshot.");
}
}

int main(int argc, char* argv[])
{
    bool demo = false;
    int smokeFrames = 0;
    for (int i=1; i<argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--demo") demo = true;
        else if (arg == "--smoke-test") { demo = true; smokeFrames = 5; }
        else if (arg == "--help") {
            std::puts("Usage: hhkbs [--demo] [--smoke-test]\nHHKBS: HHKB Studio keymap editor for Linux");
            return 0;
        } else { std::fprintf(stderr,"Unknown option: %s\n", argv[i]); return 1; }
    }
    // Ctrl-C, a logout or a terminal closing ask the window to close like its X button does, so a write to the keyboard
    // is finished and unsaved work is asked about. A second signal takes the default action and ends the program.
    struct sigaction onSignal{};
    onSignal.sa_handler = onTerminate;
    sigemptyset(&onSignal.sa_mask);
    onSignal.sa_flags = SA_RESETHAND;
    for (const int signal : {SIGINT, SIGTERM, SIGHUP}) ::sigaction(signal, &onSignal, nullptr);
    glfwSetErrorCallback([](int code, const char* message) { std::fprintf(stderr,"GLFW %d: %s\n",code,message); });
    if (!glfwInit()) return 1;
    auto terminate = [](void*) { glfwTerminate(); };
    std::unique_ptr<void, decltype(terminate)> guard(reinterpret_cast<void*>(1), terminate);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,0);
    // The layout is drawn for an ordinary display; a screen that shows everything larger gets a larger window,
    // larger text and larger gaps in the same proportion.
    const float scale = displayScale();
    const auto scaled = [scale](const int pixels) { return static_cast<int>(std::lround(pixels * scale)); };
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(scaled(1180),scaled(700),"HHKBS",nullptr,nullptr),glfwDestroyWindow);
    if (!window) return 1;
    glfwSetWindowSizeLimits(window.get(),scaled(940),scaled(620),GLFW_DONT_CARE,GLFW_DONT_CARE);
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    loadSystemFont(*io.Fonts, 16.f * scale);
    theme::setScale(scale);
    theme::mode();
    if (!ImGui_ImplGlfw_InitForOpenGL(window.get(),true)) {
        std::fprintf(stderr,"Could not initialize the GUI backend.\n");
        ImGui::DestroyContext();
        return 1;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 130")) {
        std::fprintf(stderr,"Could not initialize the GUI backend.\n");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return 1;
    }
    int result = 0;
    try {
        MainWindow app(demo);
        glfwSetWindowUserPointer(window.get(), &app);
        glfwSetDropCallback(window.get(), [](GLFWwindow* target, int count, const char** paths) {
            if (auto* window = static_cast<MainWindow*>(glfwGetWindowUserPointer(target)))
                window->dropFiles(std::vector<std::filesystem::path>(paths, paths + count));
        });
        int frames = 0;
        std::string lastDrawError;
        bool wasFocused = true;
        const auto start = std::chrono::steady_clock::now();
        const char* screenshot = std::getenv("HHKBS_SCREENSHOT");
        const char* delayText = std::getenv("HHKBS_SCREENSHOT_DELAY_MS");
        const auto screenshotDelay = std::chrono::milliseconds(delayText ? std::max(0,std::atoi(delayText)) : 250);
        while (!app.shouldClose()) {
            glfwPollEvents();
            if (glfwWindowShouldClose(window.get())) {
                glfwSetWindowShouldClose(window.get(),GLFW_FALSE);
                app.requestClose();
            }
            if (terminateRequested) {
                terminateRequested = 0;
                app.requestClose();
            }
            // Follow the desktop's color scheme when the window regains focus.
            const bool focused = glfwGetWindowAttrib(window.get(),GLFW_FOCUSED) == GLFW_TRUE;
            if (focused && !wasFocused) theme::refresh();
            wasFocused = focused;
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            // An exception while drawing must not take the unsaved work of every profile with it: the frame is put
            // back in order, the reason is shown, and the program carries on.
            ImGuiErrorRecoveryState frameState;
            ImGui::ErrorRecoveryStoreState(&frameState);
            try { app.draw(); }
            catch (const std::exception& error) {
                // Recovering is the point, so it should not abort, log or draw a warning about the half-drawn frame.
                const auto settings = std::array<bool, 3>{io.ConfigErrorRecoveryEnableAssert, io.ConfigErrorRecoveryEnableDebugLog,
                                                          io.ConfigErrorRecoveryEnableTooltip};
                io.ConfigErrorRecoveryEnableAssert = io.ConfigErrorRecoveryEnableDebugLog = io.ConfigErrorRecoveryEnableTooltip = false;
                ImGui::ErrorRecoveryTryToRecoverState(&frameState);
                io.ConfigErrorRecoveryEnableAssert = settings[0];
                io.ConfigErrorRecoveryEnableDebugLog = settings[1];
                io.ConfigErrorRecoveryEnableTooltip = settings[2];
                app.reportError(error.what());
                if (lastDrawError != error.what()) std::fprintf(stderr,"HHKBS: %s\n",error.what());
                lastDrawError = error.what();
            }
            ImGui::Render();
            int width=0, height=0;
            glfwGetFramebufferSize(window.get(),&width,&height);
            glViewport(0,0,width,height);
            const auto& bg = theme::palette().window;
            glClearColor(bg.x,bg.y,bg.z,1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (screenshot && width>0 && height>0 && std::chrono::steady_clock::now()-start >= screenshotDelay) {
                capture(screenshot,width,height);
                break;
            }
            glfwSwapBuffers(window.get());
            if (smokeFrames && ++frames >= smokeFrames) break;
            glfwWaitEventsTimeout(.01);
        }
    } catch (const std::exception& error) { std::fprintf(stderr,"HHKBS: %s\n",error.what()); result=1; }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    return result;
}
