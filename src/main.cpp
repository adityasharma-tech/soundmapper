#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <cmath>

// ---------- data model ----------
struct Pin {
    ImVec2 pos;      // screen-space position (filled in each frame after drawing)
    bool   isInput;
};

struct Node {
    int    id;
    char   title[32];
    ImVec2 pos;      // top-left, in canvas space
    ImVec2 size = { 160, 70 };
    Pin    inputPin  { {0,0}, true };
    Pin    outputPin { {0,0}, false };
};

struct Link {
    int fromNode, toNode; // output -> input
};

static std::vector<Node> nodes;
static std::vector<Link> links;
static ImVec2 canvasScroll{ 0, 0 };

// link currently being dragged from an output pin
static bool  draggingLink = false;
static int   dragSourceNode = -1;

static float PinRadius = 6.0f;

static bool HitPin(ImVec2 mouse, ImVec2 pinPos) {
    ImVec2 d = { mouse.x - pinPos.x, mouse.y - pinPos.y };
    return (d.x * d.x + d.y * d.y) <= (PinRadius + 4) * (PinRadius + 4);
}

static void DrawLinkCurve(ImDrawList* draw, ImVec2 p1, ImVec2 p2, ImU32 color) {
    float dist = fabsf(p2.x - p1.x) * 0.5f + 30.0f;
    ImVec2 c1 = { p1.x + dist, p1.y };
    ImVec2 c2 = { p2.x - dist, p2.y };
    draw->AddBezierCubic(p1, c1, c2, p2, color, 2.5f);
}

static void DrawNode(Node& node, ImVec2 origin) {
    ImGui::PushID(node.id);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec2 topLeft = { origin.x + node.pos.x, origin.y + node.pos.y };
    ImVec2 botRight = { topLeft.x + node.size.x, topLeft.y + node.size.y };

    // body
    draw->AddRectFilled(topLeft, botRight, IM_COL32(45, 45, 48, 255), 6.0f);
    draw->AddRect(topLeft, botRight, IM_COL32(90, 90, 95, 255), 6.0f);

    // title bar (drag handle)
    ImVec2 titleBotRight = { botRight.x, topLeft.y + 22 };
    draw->AddRectFilled(topLeft, titleBotRight, IM_COL32(60, 90, 150, 255), 6.0f, ImDrawFlags_RoundCornersTop);
    draw->AddText({ topLeft.x + 8, topLeft.y + 3 }, IM_COL32_WHITE, node.title);

    // drag behaviour: invisible button over the title bar
    ImGui::SetCursorScreenPos(topLeft);
    ImGui::InvisibleButton("titlebar", { node.size.x, 22 });
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        node.pos = { node.pos.x + ImGui::GetIO().MouseDelta.x,
                     node.pos.y + ImGui::GetIO().MouseDelta.y };

    // pins
    node.inputPin.pos  = { topLeft.x,     topLeft.y + node.size.y * 0.5f + 11 };
    node.outputPin.pos = { botRight.x,    topLeft.y + node.size.y * 0.5f + 11 };

    draw->AddCircleFilled(node.inputPin.pos,  PinRadius, IM_COL32(200, 200, 80, 255));
    draw->AddCircleFilled(node.outputPin.pos, PinRadius, IM_COL32(80, 200, 120, 255));
    draw->AddText({ topLeft.x + 12, node.inputPin.pos.y - 8 }, IM_COL32(200,200,200,255), "in");
    draw->AddText({ node.outputPin.pos.x - 24, node.outputPin.pos.y - 8 }, IM_COL32(200,200,200,255), "out");

    ImGui::PopID();
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Minimal Node UI", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    nodes.push_back({ 1, "Node A", {80, 80} });
    nodes.push_back({ 2, "Node B", {420, 220} });

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Canvas", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 mouse = ImGui::GetIO().MousePos;

        // draw existing links first (under nodes)
        for (auto& l : links)
            DrawLinkCurve(draw, nodes[l.fromNode].outputPin.pos, nodes[l.toNode].inputPin.pos, IM_COL32(200, 200, 200, 255));

        for (auto& n : nodes) DrawNode(n, origin);

        // start a link drag from an output pin
        if (!draggingLink && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            for (int i = 0; i < (int)nodes.size(); ++i) {
                if (HitPin(mouse, nodes[i].outputPin.pos)) {
                    draggingLink = true;
                    dragSourceNode = i;
                    break;
                }
            }
        }

        if (draggingLink) {
            DrawLinkCurve(draw, nodes[dragSourceNode].outputPin.pos, mouse, IM_COL32(255, 200, 80, 255));
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                for (int i = 0; i < (int)nodes.size(); ++i) {
                    if (i != dragSourceNode && HitPin(mouse, nodes[i].inputPin.pos)) {
                        links.push_back({ dragSourceNode, i });
                        break;
                    }
                }
                draggingLink = false;
                dragSourceNode = -1;
            }
        }

        ImGui::End();
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
