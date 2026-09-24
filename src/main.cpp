#include "gui/MainWindow.h"
#include "gui/Theme.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <array>
#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

namespace {
void loadSystemFont(ImFontAtlas& atlas)
{
    // Ask the host's Fontconfig utility for its configured sans-serif face.
    // No font file or Fontconfig library is bundled with the application.
    std::string path;
    if (FILE* query = ::popen("fc-match --format='%{file}' sans-serif 2>/dev/null", "r")) {
        std::array<char, 1024> buffer{};
        while (const auto count = std::fread(buffer.data(), 1, buffer.size(), query))
            path.append(buffer.data(), count);
        if (::pclose(query) != 0) path.clear();
    }
    if (!path.empty() && std::ifstream(path, std::ios::binary).good()) {
        if (atlas.AddFontFromFileTTF(path.c_str(), 16.f)) return;
    }
    ImFontConfig fallback;
    fallback.SizePixels = 16.f;
    atlas.AddFontDefault(&fallback);
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
    glfwSetErrorCallback([](int code, const char* message) { std::fprintf(stderr,"GLFW %d: %s\n",code,message); });
    if (!glfwInit()) return 1;
    auto terminate = [](void*) { glfwTerminate(); };
    std::unique_ptr<void, decltype(terminate)> guard(reinterpret_cast<void*>(1), terminate);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,0);
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(1180,700,"HHKBS",nullptr,nullptr),glfwDestroyWindow);
    if (!window) return 1;
    glfwSetWindowSizeLimits(window.get(),940,620,GLFW_DONT_CARE,GLFW_DONT_CARE);
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    loadSystemFont(*io.Fonts);
    theme::mode();
    if (!ImGui_ImplGlfw_InitForOpenGL(window.get(),true) || !ImGui_ImplOpenGL3_Init("#version 130")) {
        std::fprintf(stderr,"Could not initialize the GUI backend.\n");
        ImGui::DestroyContext();
        return 1;
    }
    int result = 0;
    try {
        MainWindow app(demo);
        int frames = 0;
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
            // Follow the desktop's color scheme when the window regains focus.
            const bool focused = glfwGetWindowAttrib(window.get(),GLFW_FOCUSED) == GLFW_TRUE;
            if (focused && !wasFocused) theme::refresh();
            wasFocused = focused;
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            app.draw();
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
