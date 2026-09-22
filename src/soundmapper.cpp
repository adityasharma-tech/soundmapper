#define IMGUI_DEFINE_MATH_OPERATORS
#include <application.h>
#include "utilities/builders.h"
#include "utilities/widgets.h"

#include <imgui_node_editor.h>

#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <memory>
#include <array>
#include <string>

using json = nlohmann::json;

std::string ExecCommand(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

#include <vector>
#include <map>
#include <algorithm>
#include <utility>


namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

using namespace ax;

using ax::Widgets::IconType;

static ed::EditorContext* m_Editor = nullptr;

enum class PinType
{
    SinkInput,
    SourceOutput,
    Sink,
    Source
};

enum class PinKind
{
    Output,
    Input
};

enum class NodeType
{
    Blueprint,
};

struct Node;

struct Pin
{
    ed::PinId   ID;
    ::Node*     Node;
    std::string Name;
    PinType     Type;
    PinKind     Kind;

    Pin(int id, const char* name, PinType type):
        ID(id), Node(nullptr), Name(name), Type(type), Kind(PinKind::Input)
    {
    }
};

struct Node
{
    ed::NodeId ID;
    std::string Name;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;
    NodeType Type;
    ImVec2 Size;

    std::string State;
    std::string SavedState;
    int DemoOption;
    uint32_t PA_ID;
    std::string PA_Name;
    std::string Details;

    Node(int id, const char* name, uint32_t pa_id = 0, ImColor color = ImColor(255, 255, 255)):
        ID(id), Name(name), Color(color), Type(NodeType::Blueprint), Size(0, 0), DemoOption(0), PA_ID(pa_id)
    {
    }
};

struct Link
{
    ed::LinkId ID;

    ed::PinId StartPinID;
    ed::PinId EndPinID;

    ImColor Color;
    uint32_t PA_Module_ID;

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255), PA_Module_ID(0)
    {
    }
};

static bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f)
{
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID("##Splitter");
    ImRect bb;
    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f);
}

struct NodeIdLess
{
    bool operator()(const ed::NodeId& lhs, const ed::NodeId& rhs) const
    {
        return lhs.AsPointer() < rhs.AsPointer();
    }
};

struct Example:
    public Application
{
    using Application::Application;

    struct PAEntity {
        uint32_t ID;
        std::string Name;
        std::string PA_Name;
    };
    std::vector<PAEntity> m_AvailableSinks;
    std::vector<PAEntity> m_AvailableSources;
    std::vector<PAEntity> m_AvailableSinkInputs;
    std::vector<PAEntity> m_AvailableSourceOutputs;
    bool m_WantsRefresh = true;

    int GetNextId()
    {
        return m_NextId++;
    }

    ed::LinkId GetNextLinkId()
    {
        return ed::LinkId(GetNextId());
    }

    void TouchNode(ed::NodeId id)
    {
        m_NodeTouchTime[id] = m_TouchTime;
    }

    float GetTouchProgress(ed::NodeId id)
    {
        auto it = m_NodeTouchTime.find(id);
        if (it != m_NodeTouchTime.end() && it->second > 0.0f)
            return (m_TouchTime - it->second) / m_TouchTime;
        else
            return 0.0f;
    }

    void UpdateTouch()
    {
        const auto deltaTime = ImGui::GetIO().DeltaTime;
        for (auto& entry : m_NodeTouchTime)
        {
            if (entry.second > 0.0f)
                entry.second -= deltaTime;
        }
    }

    Node* FindNode(ed::NodeId id)
    {
        for (auto& node : m_Nodes)
            if (node.ID == id)
                return &node;

        return nullptr;
    }

    Link* FindLink(ed::LinkId id)
    {
        for (auto& link : m_Links)
            if (link.ID == id)
                return &link;

        return nullptr;
    }

    Pin* FindPin(ed::PinId id)
    {
        if (!id)
            return nullptr;

        for (auto& node : m_Nodes)
        {
            for (auto& pin : node.Inputs)
                if (pin.ID == id)
                    return &pin;

            for (auto& pin : node.Outputs)
                if (pin.ID == id)
                    return &pin;
        }

        return nullptr;
    }

    bool IsPinLinked(ed::PinId id)
    {
        if (!id)
            return false;

        for (auto& link : m_Links)
            if (link.StartPinID == id || link.EndPinID == id)
                return true;

        return false;
    }

    bool CanCreateLink(Pin* a, Pin* b)
    {
        if (!a || !b || a == b || a->Kind == b->Kind || a->Node == b->Node)
            return false;

        bool validSink = (a->Type == PinType::SinkInput && b->Type == PinType::Sink) ||
                         (b->Type == PinType::SinkInput && a->Type == PinType::Sink);

        bool validSource = (a->Type == PinType::SourceOutput && b->Type == PinType::Source) ||
                           (b->Type == PinType::SourceOutput && a->Type == PinType::Source);

        bool validLoopback = (a->Type == PinType::Source && b->Type == PinType::Sink) ||
                             (b->Type == PinType::Source && a->Type == PinType::Sink);

        if (!validSink && !validSource && !validLoopback)
            return false;

        return true;
    }

    void BuildNode(Node* node)
    {
        for (auto& input : node->Inputs)
        {
            input.Node = node;
            input.Kind = PinKind::Input;
        }

        for (auto& output : node->Outputs)
        {
            output.Node = node;
            output.Kind = PinKind::Output;
        }
    }

    Node* SpawnSinkInputNode(uint32_t pa_id = 0, const char* name = "Sink-Input")
    {
        m_Nodes.emplace_back(GetNextId(), name, pa_id, ImColor(255, 128, 128));
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::SinkInput);
        BuildNode(&m_Nodes.back());
        return &m_Nodes.back();
    }

    Node* SpawnSourceOutputNode(uint32_t pa_id = 0, const char* name = "Source-Output")
    {
        m_Nodes.emplace_back(GetNextId(), name, pa_id, ImColor(128, 255, 128));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::SourceOutput);
        BuildNode(&m_Nodes.back());
        return &m_Nodes.back();
    }

    Node* SpawnSinkNode(uint32_t pa_id = 0, const char* name = "Sink")
    {
        m_Nodes.emplace_back(GetNextId(), name, pa_id, ImColor(128, 128, 255));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Sink);
        BuildNode(&m_Nodes.back());
        return &m_Nodes.back();
    }

    Node* SpawnSourceNode(uint32_t pa_id = 0, const char* name = "Source")
    {
        m_Nodes.emplace_back(GetNextId(), name, pa_id, ImColor(255, 255, 128));
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::Source);
        BuildNode(&m_Nodes.back());
        return &m_Nodes.back();
    }

    Node* SpawnLoopbackNode(uint32_t pa_id = 0, const char* name = "Loopback")
    {
        m_Nodes.emplace_back(GetNextId(), name, pa_id, ImColor(255, 128, 255));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Sink);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::Source);
        BuildNode(&m_Nodes.back());
        return &m_Nodes.back();
    }




    std::string ParseDetails(const json& item) {
        std::string details;
        auto add_str = [&](const char* key, const char* label) {
            if (item.contains(key)) {
                if (item[key].is_string()) details += std::string(label) + ": " + item[key].get<std::string>() + "\n";
                else if (item[key].is_number_integer()) details += std::string(label) + ": " + std::to_string(item[key].get<int>()) + "\n";
                else if (item[key].is_number_unsigned()) details += std::string(label) + ": " + std::to_string(item[key].get<unsigned int>()) + "\n";
                else if (item[key].is_number_float()) details += std::string(label) + ": " + std::to_string(item[key].get<double>()) + "\n";
                else if (item[key].is_boolean()) details += std::string(label) + ": " + (item[key].get<bool>() ? "yes" : "no") + "\n";
            }
        };
        add_str("driver", "Driver");
        add_str("state", "State");
        add_str("corked", "Corked");
        add_str("mute", "Mute");
        add_str("sample_specification", "Sample Spec");
        add_str("channel_map", "Channel Map");
        add_str("sink", "Sink ID");
        add_str("source", "Source ID");
        add_str("owner_module", "Owner Module");
        add_str("client", "Client ID");
        add_str("buffer_latency_usec", "Buffer Latency (us)");
        add_str("sink_latency_usec", "Sink Latency (us)");
        add_str("resample_method", "Resample Method");

        if (item.contains("volume") && item["volume"].is_object()) {
            details += "Volume:\n";
            for (auto iter = item["volume"].begin(); iter != item["volume"].end(); ++iter) {
                if (iter.value().is_object() && iter.value().contains("value_percent")) {
                    details += "  " + iter.key() + ": " + iter.value()["value_percent"].get<std::string>() + "\n";
                }
            }
        }
        return details;
    }

    void RefreshGraph()
    {
        m_Nodes.clear();
        m_Nodes.reserve(1000); // Prevent reallocation invalidating Node* pointers
        m_Links.clear();
        m_AvailableSinks.clear();
        m_AvailableSources.clear();
        m_AvailableSinkInputs.clear();
        m_AvailableSourceOutputs.clear();

        std::string sinks_json = ExecCommand("pactl -f json list sinks");
        std::string sources_json = ExecCommand("pactl -f json list sources");
        std::string sink_inputs_json = ExecCommand("pactl -f json list sink-inputs");
        std::string source_outputs_json = ExecCommand("pactl -f json list source-outputs");

        try {
            auto sinks = json::parse(sinks_json);
            auto sources = json::parse(sources_json);
            auto sink_inputs = json::parse(sink_inputs_json);
            auto source_outputs = json::parse(source_outputs_json);

            std::map<uint32_t, Node*> sink_nodes;
            std::map<uint32_t, Node*> source_nodes;

            float y = 0;
            for (auto& s : sinks) {
                uint32_t id = s.value("index", 0);
                std::string desc = s.value("description", s.value("name", "Unknown Sink"));
                std::string pa_name = s.value("name", "");
                m_AvailableSinks.push_back({id, desc, pa_name});
                Node* n = SpawnSinkNode(id, desc.c_str());
                n->PA_Name = pa_name;
                n->Details = ParseDetails(s);
                ed::SetNodePosition(n->ID, ImVec2(500, y));
                sink_nodes[id] = n;
                y += 150;
            }

            y = 0;
            for (auto& s : sources) {
                uint32_t id = s.value("index", 0);
                std::string desc = s.value("description", s.value("name", "Unknown Source"));
                std::string pa_name = s.value("name", "");
                m_AvailableSources.push_back({id, desc, pa_name});
                Node* n = SpawnSourceNode(id, desc.c_str());
                n->PA_Name = pa_name;
                n->Details = ParseDetails(s);
                ed::SetNodePosition(n->ID, ImVec2(-500, y));
                source_nodes[id] = n;
                y += 150;
            }

            y = 0;
            for (auto& si : sink_inputs) {
                uint32_t id = si.value("index", 0);
                std::string name = si.value("name", "Unknown Sink-Input");
                if (si.contains("properties")) {
                    if (si["properties"].contains("application.name")) {
                        name = si["properties"]["application.name"];
                    }
                    if (si["properties"].contains("media.name")) {
                        std::string media = si["properties"]["media.name"];
                        if (media.length() > 25) media = media.substr(0, 22) + "...";
                        name += " - " + media;
                    }
                }
                m_AvailableSinkInputs.push_back({id, name, ""});
                Node* n = SpawnSinkInputNode(id, name.c_str());
                n->Details = ParseDetails(si);
                ed::SetNodePosition(n->ID, ImVec2(0, y));
                y += 150;

                uint32_t target_sink = si.value("sink", (uint32_t)-1);
                if (target_sink != (uint32_t)-1 && sink_nodes.count(target_sink)) {
                    m_Links.emplace_back(Link(GetNextId(), n->Outputs[0].ID, sink_nodes[target_sink]->Inputs[0].ID));
                    m_Links.back().Color = GetIconColor(PinType::SinkInput);
                }
            }

            y = 0;
            for (auto& so : source_outputs) {
                uint32_t id = so.value("index", 0);
                std::string name = so.value("name", "Unknown Source-Output");
                if (so.contains("properties")) {
                    if (so["properties"].contains("application.name")) {
                        name = so["properties"]["application.name"];
                    }
                    if (so["properties"].contains("media.name")) {
                        std::string media = so["properties"]["media.name"];
                        if (media.length() > 25) media = media.substr(0, 22) + "...";
                        name += " - " + media;
                    }
                }
                m_AvailableSourceOutputs.push_back({id, name, ""});
                Node* n = SpawnSourceOutputNode(id, name.c_str());
                n->Details = ParseDetails(so);
                ed::SetNodePosition(n->ID, ImVec2(-250, y));
                y += 150;

                uint32_t target_source = so.value("source", (uint32_t)-1);
                if (target_source != (uint32_t)-1 && source_nodes.count(target_source)) {
                    m_Links.emplace_back(Link(GetNextId(), source_nodes[target_source]->Outputs[0].ID, n->Inputs[0].ID));
                    m_Links.back().Color = GetIconColor(PinType::Source);
                }
            }

            // Auto-link monitor sources to their parent sinks
            for (auto& s : sources) {
                if (s.contains("monitor_source") || s.value("name", "").find(".monitor") != std::string::npos) {
                    // Find if this source monitors a sink
                    uint32_t source_id = s.value("index", 0);
                    std::string source_pa_name = s.value("name", "");

                    // Check monitor_of_sink field if available, otherwise match by name
                    for (auto& sk : sinks) {
                        std::string sink_monitor = sk.value("monitor_source", "");
                        if (!sink_monitor.empty() && sink_monitor == source_pa_name) {
                            uint32_t sink_id = sk.value("index", 0);
                            if (source_nodes.count(source_id) && sink_nodes.count(sink_id)) {
                                m_Links.emplace_back(Link(GetNextId(), source_nodes[source_id]->Outputs[0].ID, sink_nodes[sink_id]->Inputs[0].ID));
                                m_Links.back().Color = GetIconColor(PinType::Source);
                            }
                            break;
                        }
                    }
                }
            }

        } catch (...) {
            printf("Failed to parse PulseAudio JSON\n");
        }

        BuildNodes();
    }

    void BuildNodes()
    {
        for (auto& node : m_Nodes)
            BuildNode(&node);
    }

    void OnStart() override
    {
        ed::Config config;

        config.SettingsFile = "/home/friday/.data/soundmapper/build/bin/Blueprints.json";

        config.UserPointer = this;

        config.LoadNodeSettings = [](ed::NodeId nodeId, char* data, void* userPointer) -> size_t
        {
            auto self = static_cast<Example*>(userPointer);

            auto node = self->FindNode(nodeId);
            if (!node)
                return 0;

            if (data != nullptr)
                memcpy(data, node->State.data(), node->State.size());
            return node->State.size();
        };

        config.SaveNodeSettings = [](ed::NodeId nodeId, const char* data, size_t size, ed::SaveReasonFlags reason, void* userPointer) -> bool
        {
            auto self = static_cast<Example*>(userPointer);

            auto node = self->FindNode(nodeId);
            if (!node)
                return false;

            node->State.assign(data, size);

            self->TouchNode(nodeId);

            return true;
        };

        m_Editor = ed::CreateEditor(&config);
        ed::SetCurrentEditor(m_Editor);

        m_WantsRefresh = true;

        m_HeaderBackground = LoadTexture("data/BlueprintBackground.png");
    }

    void OnStop() override
    {
        auto releaseTexture = [this](ImTextureID& id)
        {
            if (id)
            {
                DestroyTexture(id);
                id = nullptr;
            }
        };

        releaseTexture(m_HeaderBackground);

        if (m_Editor)
        {
            ed::DestroyEditor(m_Editor);
            m_Editor = nullptr;
        }
    }

    ImColor GetIconColor(PinType type)
    {
        switch (type)
        {
            case PinType::SinkInput:    return ImColor(255, 128, 128);
            case PinType::SourceOutput: return ImColor(128, 255, 128);
            case PinType::Sink:         return ImColor(128, 128, 255);
            case PinType::Source:       return ImColor(255, 255, 128);
            default:                    return ImColor(255, 255, 255);
        }
    };

    void DrawPinIcon(const Pin& pin, bool connected, int alpha)
    {
        ImColor color = GetIconColor(pin.Type);
        color.Value.w = alpha / 255.0f;

        ax::Widgets::Icon(ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)), IconType::Flow, connected, color, ImColor(32, 32, 32, alpha));
    };

    void ShowLeftPane(float paneWidth)
    {
        auto& io = ImGui::GetIO();

        ImGui::BeginChild("LeftPane", ImVec2(paneWidth, 0));

        ImGui::BeginChild("Selection", ImVec2(paneWidth, -150.0f));
        paneWidth = ImGui::GetContentRegionAvail().x;

        ImGui::TextUnformatted("Node Details");
        ImGui::Separator();

        std::vector<ed::NodeId> selectedNodes;
        selectedNodes.resize(ed::GetSelectedObjectCount());
        int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
        selectedNodes.resize(nodeCount);

        if (nodeCount > 0)
        {
            auto node = FindNode(selectedNodes[0]);
            if (node)
            {
                ImGui::Text("Name: %s", node->Name.c_str());
                ImGui::Text("ID: %p", node->ID.AsPointer());
                ImGui::Text("Type: Blueprint");
                ImGui::Text("Inputs: %d", (int)node->Inputs.size());
                ImGui::Text("Outputs: %d", (int)node->Outputs.size());

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("PulseAudio Target Entity:");
                std::vector<PAEntity>* entities = nullptr;
                bool is_sink_input = false;
                bool is_source_output = false;

                if (node->Outputs.size() && node->Outputs[0].Type == PinType::SinkInput) {
                    entities = &m_AvailableSinkInputs;
                    is_sink_input = true;
                } else if (node->Inputs.size() && node->Inputs[0].Type == PinType::SourceOutput) {
                    entities = &m_AvailableSourceOutputs;
                    is_source_output = true;
                } else if (node->Inputs.size() && node->Inputs[0].Type == PinType::Sink) {
                    entities = &m_AvailableSinks;
                } else if (node->Outputs.size() && node->Outputs[0].Type == PinType::Source) {
                    entities = &m_AvailableSources;
                }

                if (entities && entities->size() > 0) {
                    int current_idx = -1;
                    for (size_t i = 0; i < entities->size(); ++i) {
                        if ((*entities)[i].ID == node->PA_ID) {
                            current_idx = i;
                            break;
                        }
                    }
                    const char* current_name = current_idx >= 0 ? (*entities)[current_idx].Name.c_str() : "Unknown";

                    ImGui::SetNextItemWidth(paneWidth - 20);
                    if (ImGui::BeginCombo("##pa_combo", current_name)) {
                        for (size_t i = 0; i < entities->size(); ++i) {
                            bool is_selected = (current_idx == (int)i);
                            if (ImGui::Selectable((*entities)[i].Name.c_str(), is_selected)) {
                                node->PA_ID = (*entities)[i].ID;
                                node->Name = (*entities)[i].Name;
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    ImGui::Text("No entities available.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Properties:");
                ImGui::TextWrapped("%s", node->Details.c_str());

            }
        }
        else
        {
            ImGui::Text("No node selected.");
        }

        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Global Actions:");
        if (ImGui::Button("Refresh Graph", ImVec2(paneWidth - 20, 30))) {
            m_WantsRefresh = true;
        }
        if (ImGui::Button("Create Loopback Module", ImVec2(paneWidth - 20, 30))) {
            ExecCommand("pactl load-module module-loopback");
            m_WantsRefresh = true;
        }
        if (ImGui::Button("Create Null Sink", ImVec2(paneWidth - 20, 30))) {
            ExecCommand("pactl load-module module-null-sink");
            m_WantsRefresh = true;
        }
        ImGui::EndChild();
    }

    void OnFrame(float deltaTime) override
    {
        if (m_WantsRefresh) {
            RefreshGraph();
            m_WantsRefresh = false;
        }

        UpdateTouch();

        ed::SetCurrentEditor(m_Editor);

        static ed::NodeId contextNodeId      = 0;
        static ed::LinkId contextLinkId      = 0;
        static ed::PinId  contextPinId       = 0;
        static bool createNewNode  = false;
        static Pin* newNodeLinkPin = nullptr;
        static Pin* newLinkPin     = nullptr;

        static float leftPaneWidth  = 400.0f;
        static float rightPaneWidth = 800.0f;
        Splitter(true, 4.0f, &leftPaneWidth, &rightPaneWidth, 50.0f, 50.0f);

        ShowLeftPane(leftPaneWidth - 4.0f);

        ImGui::SameLine(0.0f, 12.0f);

        ed::Begin("Node editor");
        {
            auto cursorTopLeft = ImGui::GetCursorScreenPos();

            util::BlueprintNodeBuilder builder(m_HeaderBackground, GetTextureWidth(m_HeaderBackground), GetTextureHeight(m_HeaderBackground));

            for (auto& node : m_Nodes)
            {
                if (node.Type != NodeType::Blueprint)
                    continue;

                builder.Begin(node.ID);
                    builder.Header(node.Color);
                        ImGui::Spring(0);
                        ImGui::TextUnformatted(node.Name.c_str());
                        ImGui::Spring(1);
                        ImGui::Dummy(ImVec2(0, 28));
                        ImGui::Spring(0);
                    builder.EndHeader();

                    for (auto& input : node.Inputs)
                    {
                        auto alpha = ImGui::GetStyle().Alpha;
                        if (newLinkPin && !CanCreateLink(newLinkPin, &input) && &input != newLinkPin)
                            alpha = alpha * (48.0f / 255.0f);

                        builder.Input(input.ID);
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                        DrawPinIcon(input, IsPinLinked(input.ID), (int)(alpha * 255));
                        ImGui::Spring(0);
                        if (!input.Name.empty())
                        {
                            ImGui::TextUnformatted(input.Name.c_str());
                            ImGui::Spring(0);
                        }
                        ImGui::PopStyleVar();
                        builder.EndInput();
                    }

                    for (auto& output : node.Outputs)
                    {
                        auto alpha = ImGui::GetStyle().Alpha;
                        if (newLinkPin && !CanCreateLink(newLinkPin, &output) && &output != newLinkPin)
                            alpha = alpha * (48.0f / 255.0f);

                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                        builder.Output(output.ID);
                        if (!output.Name.empty())
                        {
                            ImGui::Spring(0);
                            ImGui::TextUnformatted(output.Name.c_str());
                        }
                        ImGui::Spring(0);
                        DrawPinIcon(output, IsPinLinked(output.ID), (int)(alpha * 255));
                        ImGui::PopStyleVar();
                        builder.EndOutput();
                    }

                builder.End();
            }

            for (auto& link : m_Links)
                ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);

            if (!createNewNode)
            {
                if (ed::BeginCreate(ImColor(255, 255, 255), 2.0f))
                {
                    auto showLabel = [](const char* label, ImColor color)
                    {
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
                        auto size = ImGui::CalcTextSize(label);

                        auto padding = ImGui::GetStyle().FramePadding;
                        auto spacing = ImGui::GetStyle().ItemSpacing;

                        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));

                        auto rectMin = ImGui::GetCursorScreenPos() - padding;
                        auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

                        auto drawList = ImGui::GetWindowDrawList();
                        drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
                        ImGui::TextUnformatted(label);
                    };

                    ed::PinId startPinId = 0, endPinId = 0;
                    if (ed::QueryNewLink(&startPinId, &endPinId))
                    {
                        auto startPin = FindPin(startPinId);
                        auto endPin   = FindPin(endPinId);

                        newLinkPin = startPin ? startPin : endPin;

                        if (startPin->Kind == PinKind::Input)
                        {
                            std::swap(startPin, endPin);
                            std::swap(startPinId, endPinId);
                        }

                        if (startPin && endPin)
                        {
                            if (endPin == startPin)
                            {
                                ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                            }
                            else if (endPin->Kind == startPin->Kind)
                            {
                                showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                                ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                            }
                            else if (endPin->Node == startPin->Node)
                            {
                               showLabel("x Cannot connect to self", ImColor(45, 32, 32, 180));
                               ed::RejectNewItem(ImColor(255, 0, 0), 1.0f);
                            }
                            else if (!CanCreateLink(startPin, endPin))
                            {
                                showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                                ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                            }
                            else
                            {
                                showLabel("+ Create Link", ImColor(32, 45, 32, 180));
                                if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                                {
                                    // Enforce single-link on pins that need it
                                    if (startPin->Type == PinType::SourceOutput || startPin->Type == PinType::SinkInput)
                                    {
                                        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                                            [startPinId](const Link& l) { return l.StartPinID == startPinId || l.EndPinID == startPinId; }), m_Links.end());
                                    }
                                    if (endPin->Type == PinType::SourceOutput || endPin->Type == PinType::SinkInput)
                                    {
                                        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                                            [endPinId](const Link& l) { return l.StartPinID == endPinId || l.EndPinID == endPinId; }), m_Links.end());
                                    }

                                    m_Links.emplace_back(Link(GetNextId(), startPinId, endPinId));
                                    m_Links.back().Color = GetIconColor(startPin->Type);

                                    // Run pactl command for sink-input -> sink
                                    if (startPin->Type == PinType::SinkInput || endPin->Type == PinType::SinkInput) {
                                        Pin* sinkInputPin = startPin->Type == PinType::SinkInput ? startPin : endPin;
                                        Pin* sinkPin = startPin->Type == PinType::Sink ? startPin : endPin;
                                        if (sinkInputPin && sinkPin) {
                                            char cmd[256];
                                            snprintf(cmd, sizeof(cmd), "pactl move-sink-input %u %u", sinkInputPin->Node->PA_ID, sinkPin->Node->PA_ID);
                                            ExecCommand(cmd);
                                        }
                                    }

                                    // Run pactl command for source-output -> source
                                    if (startPin->Type == PinType::SourceOutput || endPin->Type == PinType::SourceOutput) {
                                        Pin* sourceOutputPin = startPin->Type == PinType::SourceOutput ? startPin : endPin;
                                        Pin* sourcePin = startPin->Type == PinType::Source ? startPin : endPin;
                                        if (sourceOutputPin && sourcePin) {
                                            char cmd[256];
                                            snprintf(cmd, sizeof(cmd), "pactl move-source-output %u %u", sourceOutputPin->Node->PA_ID, sourcePin->Node->PA_ID);
                                            ExecCommand(cmd);
                                        }
                                    }

                                    // Run pactl command for source -> sink (loopback module)
                                    if ((startPin->Type == PinType::Source && endPin->Type == PinType::Sink) ||
                                        (startPin->Type == PinType::Sink && endPin->Type == PinType::Source)) {
                                        Pin* sourcePin = startPin->Type == PinType::Source ? startPin : endPin;
                                        Pin* sinkPin = startPin->Type == PinType::Sink ? startPin : endPin;
                                        if (sourcePin && sinkPin) {
                                            char cmd[512];
                                            snprintf(cmd, sizeof(cmd), "pactl load-module module-loopback source=%s sink=%s",
                                                sourcePin->Node->PA_Name.c_str(), sinkPin->Node->PA_Name.c_str());
                                            std::string output = ExecCommand(cmd);
                                            try {
                                                m_Links.back().PA_Module_ID = std::stoul(output);
                                            } catch (...) {}
                                        }
                                    }
                                }
                            }
                        }
                    }

                    ed::PinId pinId = 0;
                    if (ed::QueryNewNode(&pinId))
                    {
                        newLinkPin = FindPin(pinId);
                        if (newLinkPin)
                            showLabel("+ Create Node", ImColor(32, 45, 32, 180));

                        if (ed::AcceptNewItem())
                        {
                            createNewNode  = true;
                            newNodeLinkPin = FindPin(pinId);
                            newLinkPin = nullptr;
                            ed::Suspend();
                            ImGui::OpenPopup("Create New Node");
                            ed::Resume();
                        }
                    }
                }
                else
                    newLinkPin = nullptr;

                ed::EndCreate();

                if (ed::BeginDelete())
                {
                    ed::NodeId nodeId = 0;
                    while (ed::QueryDeletedNode(&nodeId))
                    {
                        if (ed::AcceptDeletedItem())
                        {
                            auto id = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](auto& node) { return node.ID == nodeId; });
                            if (id != m_Nodes.end())
                                m_Nodes.erase(id);
                        }
                    }

                    ed::LinkId linkId = 0;
                    while (ed::QueryDeletedLink(&linkId))
                    {
                        if (ed::AcceptDeletedItem())
                        {
                            auto id = std::find_if(m_Links.begin(), m_Links.end(), [linkId](auto& link) { return link.ID == linkId; });
                            if (id != m_Links.end())
                            {
                                if (id->PA_Module_ID > 0)
                                {
                                    char cmd[256];
                                    snprintf(cmd, sizeof(cmd), "pactl unload-module %u", id->PA_Module_ID);
                                    ExecCommand(cmd);
                                }
                                m_Links.erase(id);
                            }
                        }
                    }
                }
                ed::EndDelete();
            }

            ImGui::SetCursorScreenPos(cursorTopLeft);
        }

    # if 1
        auto openPopupPosition = ImGui::GetMousePos();
        ed::Suspend();
        if (ed::ShowNodeContextMenu(&contextNodeId))
            ImGui::OpenPopup("Node Context Menu");
        else if (ed::ShowPinContextMenu(&contextPinId))
            ImGui::OpenPopup("Pin Context Menu");
        else if (ed::ShowLinkContextMenu(&contextLinkId))
            ImGui::OpenPopup("Link Context Menu");
        else if (ed::ShowBackgroundContextMenu())
        {
            ImGui::OpenPopup("Create New Node");
            newNodeLinkPin = nullptr;
        }
        ed::Resume();

        ed::Suspend();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        if (ImGui::BeginPopup("Node Context Menu"))
        {
            auto node = FindNode(contextNodeId);

            ImGui::TextUnformatted("Node Context Menu");
            ImGui::Separator();
            if (node)
            {
                ImGui::Text("ID: %p", node->ID.AsPointer());
                ImGui::Text("Inputs: %d", (int)node->Inputs.size());
                ImGui::Text("Outputs: %d", (int)node->Outputs.size());
            }
            else
                ImGui::Text("Unknown node: %p", contextNodeId.AsPointer());
            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))
                ed::DeleteNode(contextNodeId);
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("Pin Context Menu"))
        {
            auto pin = FindPin(contextPinId);

            ImGui::TextUnformatted("Pin Context Menu");
            ImGui::Separator();
            if (pin)
            {
                ImGui::Text("ID: %p", pin->ID.AsPointer());
                if (pin->Node)
                    ImGui::Text("Node: %p", pin->Node->ID.AsPointer());
                else
                    ImGui::Text("Node: %s", "<none>");
            }
            else
                ImGui::Text("Unknown pin: %p", contextPinId.AsPointer());

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("Link Context Menu"))
        {
            auto link = FindLink(contextLinkId);

            ImGui::TextUnformatted("Link Context Menu");
            ImGui::Separator();
            if (link)
            {
                ImGui::Text("ID: %p", link->ID.AsPointer());
                ImGui::Text("From: %p", link->StartPinID.AsPointer());
                ImGui::Text("To: %p", link->EndPinID.AsPointer());
            }
            else
                ImGui::Text("Unknown link: %p", contextLinkId.AsPointer());
            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))
                ed::DeleteLink(contextLinkId);
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("Create New Node"))
        {
            auto newNodePostion = openPopupPosition;

            Node* node = nullptr;
            if (ImGui::MenuItem("Sink-Input"))
                node = SpawnSinkInputNode();
            if (ImGui::MenuItem("Source-Output"))
                node = SpawnSourceOutputNode();
            if (ImGui::MenuItem("Sink"))
                node = SpawnSinkNode();
            if (ImGui::MenuItem("Source"))
                node = SpawnSourceNode();
            if (ImGui::MenuItem("Loopback"))
                node = SpawnLoopbackNode();

            if (node)
            {
                BuildNodes();

                createNewNode = false;

                ed::SetNodePosition(node->ID, newNodePostion);

                if (auto startPin = newNodeLinkPin)
                {
                    auto& pins = startPin->Kind == PinKind::Input ? node->Outputs : node->Inputs;

                    for (auto& pin : pins)
                    {
                        if (CanCreateLink(startPin, &pin))
                        {
                            auto endPin = &pin;
                            if (startPin->Kind == PinKind::Input)
                                std::swap(startPin, endPin);

                            m_Links.emplace_back(Link(GetNextId(), startPin->ID, endPin->ID));
                            m_Links.back().Color = GetIconColor(startPin->Type);

                            break;
                        }
                    }
                }
            }

            ImGui::EndPopup();
        }
        else
            createNewNode = false;
        ImGui::PopStyleVar();
        ed::Resume();
    # endif

        ed::End();
    }

    int                  m_NextId = 1;
    const int            m_PinIconSize = 24;
    std::vector<Node>    m_Nodes;
    std::vector<Link>    m_Links;
    ImTextureID          m_HeaderBackground = nullptr;
    const float          m_TouchTime = 1.0f;
    std::map<ed::NodeId, float, NodeIdLess> m_NodeTouchTime;
};

int Main(int argc, char** argv)
{
    Example exampe("Blueprints", argc, argv);

    if (exampe.Create())
        return exampe.Run();

    return 0;
}
