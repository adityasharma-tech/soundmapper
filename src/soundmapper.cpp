#define IMGUI_DEFINE_MATH_OPERATORS
#include <application.h>
#include "utilities/builders.h"
#include "utilities/widgets.h"
#include "routing_engine.h"

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
#include <sstream>
#include <set>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <sys/wait.h>


namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

using namespace ax;

using ax::Widgets::IconType;

static ed::EditorContext* m_Editor = nullptr;
static const char* kLogPath = "/home/friday/.data/soundmapper/soundmapper.log";

static void LogMessage(const std::string& message)
{
    std::ofstream log(kLogPath, std::ios::app);
    if (!log)
        return;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    log << std::put_time(std::localtime(&now), "%F %T") << " " << message << "\n";
}

using PinType = soundmapper::EndpointType;
using RouteType = soundmapper::RouteType;

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
    uint32_t PA_Monitor_ID;
    std::string PA_Monitor_Name;
    uint32_t PA_Module_ID;
    bool IsNullSink;
    std::string Details;

    Node(int id, const char* name, uint32_t pa_id = 0, ImColor color = ImColor(255, 255, 255)):
        ID(id), Name(name), Color(color), Type(NodeType::Blueprint), Size(0, 0), DemoOption(0), PA_ID(pa_id),
        PA_Monitor_ID(static_cast<uint32_t>(-1)), PA_Module_ID(0), IsNullSink(false)
    {
    }
};

enum class RouteResourceType
{
    NullSink,
    Loopback
};

struct RouteResource
{
    RouteResourceType Type;
    uint32_t PA_Module_ID;
    std::string PA_Name;

    RouteResource(RouteResourceType type, uint32_t moduleId = 0, const std::string& name = "")
        : Type(type), PA_Module_ID(moduleId), PA_Name(name) {}
};

struct Link
{
    ed::LinkId ID;

    ed::PinId StartPinID;
    ed::PinId EndPinID;

    ImColor Color;
    RouteType Type;
    std::vector<RouteResource> Resources;
    bool Managed;
    uint32_t PreviousSinkID;
    uint32_t PreviousSourceID;

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255), Type(RouteType::Invalid), Managed(false), PreviousSinkID(static_cast<uint32_t>(-1)), PreviousSourceID(static_cast<uint32_t>(-1))
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

        return soundmapper::RoutingEngine::CanConnect(a->Type, b->Type);
    }

    RouteType GetRouteType(Pin* a, Pin* b) const
    {
        return a && b ? soundmapper::RoutingEngine::Decide(a->Type, b->Type)
                      : RouteType::Invalid;
    }

    static const char* RouteName(RouteType type)
    {
        switch (type)
        {
        case RouteType::PlaybackToSink: return "playback-to-sink";
        case RouteType::SourceToCapture: return "source-to-capture";
        case RouteType::SourceToSink: return "source-to-sink";
        case RouteType::PlaybackToCapture: return "playback-to-capture";
        default: return "invalid";
        }
    }

    static std::string DescribePin(const Pin* pin)
    {
        if (!pin || !pin->Node)
            return "<missing pin>";
        return pin->Node->Name + " (id=" + std::to_string(pin->Node->PA_ID) + ")";
    }

    static uint32_t SourceEndpointID(const Pin* pin)
    {
        return pin->Node->IsNullSink ? pin->Node->PA_Monitor_ID : pin->Node->PA_ID;
    }

    static const std::string& SourceEndpointName(const Pin* pin)
    {
        return pin->Node->IsNullSink ? pin->Node->PA_Monitor_Name : pin->Node->PA_Name;
    }

    static std::string ShellQuote(const std::string& value)
    {
        std::string out = "'";
        for (char c : value)
        {
            if (c == '\'')
                out += "'\\''";
            else
                out += c;
        }
        out += "'";
        return out;
    }

    bool ExecPactl(const std::string& command, std::string& output)
    {
        output.clear();
        std::string fullCommand = command + " 2>&1";
        FILE* pipe = popen(fullCommand.c_str(), "r");
        if (!pipe)
        {
            LogMessage("pactl spawn failed: " + command);
            return false;
        }

        std::array<char, 256> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
            output += buffer.data();

        int status = pclose(pipe);
        const bool success = status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        LogMessage("pactl success=" + std::string(success ? "true" : "false") +
                   " status=" + std::to_string(status) + " command=" + command +
                   " output=" + output);
        return success;
    }

    bool PactlMoveSinkInput(uint32_t sinkInputID, uint32_t sinkID)
    {
        std::string output;
        return ExecPactl("pactl move-sink-input " + std::to_string(sinkInputID) + " " + std::to_string(sinkID), output);
    }

    bool PactlMoveSourceOutput(uint32_t sourceOutputID, uint32_t sourceID)
    {
        std::string output;
        return ExecPactl("pactl move-source-output " + std::to_string(sourceOutputID) + " " + std::to_string(sourceID), output);
    }

    bool PactlCreateNullSink(const std::string& sinkName, const std::string& description, uint32_t& moduleID)
    {
        std::string output;
        std::string propertyValue = description;
        std::replace(propertyValue.begin(), propertyValue.end(), ' ', '_');
        std::string command = "pactl load-module module-null-sink sink_name=" + sinkName +
                              " sink_properties=device.description=" + propertyValue;
        if (!ExecPactl(command, output))
            return false;

        std::istringstream iss(output);
        return static_cast<bool>(iss >> moduleID);
    }

    bool PactlCreateLoopback(const std::string& sourceName, const std::string& sinkName, uint32_t& moduleID)
    {
        std::string output;
        std::string command = "pactl load-module module-loopback source=" + ShellQuote(sourceName) +
                              " sink=" + ShellQuote(sinkName);
        if (!ExecPactl(command, output))
            return false;

        std::istringstream iss(output);
        return static_cast<bool>(iss >> moduleID);
    }

    bool PactlUnloadModule(uint32_t moduleID)
    {
        if (!moduleID)
            return true;
        std::string output;
        return ExecPactl("pactl unload-module " + std::to_string(moduleID), output);
    }

    void CreateNullSink()
    {
        const std::string sinkName = "soundmapper_null_" + std::to_string(m_NextId);
        uint32_t moduleID = 0;
        if (PactlCreateNullSink(sinkName, "Soundmapper Null Sink", moduleID))
        {
            LogMessage("null sink created name=" + sinkName + " module=" + std::to_string(moduleID));
            m_WantsRefresh = true;
        }
        else
        {
            LogMessage("null sink creation failed name=" + sinkName);
        }
    }

    bool FindSinkByName(const std::string& name, uint32_t* id = nullptr)
    {
        std::string output = ExecCommand("pactl -f json list sinks");
        try
        {
            auto items = json::parse(output);
            for (auto& item : items)
            {
                if (item.value("name", "") == name)
                {
                    if (id) *id = item.value("index", 0u);
                    return true;
                }
            }
        }
        catch (...) {}
        return false;
    }

    bool FindSourceByName(const std::string& name, uint32_t* id = nullptr)
    {
        std::string output = ExecCommand("pactl -f json list sources");
        try
        {
            auto items = json::parse(output);
            for (auto& item : items)
            {
                if (item.value("name", "") == name)
                {
                    if (id) *id = item.value("index", 0u);
                    return true;
                }
            }
        }
        catch (...) {}
        return false;
    }

    bool GetCurrentSinkForSinkInput(uint32_t sinkInputID, uint32_t& sinkID)
    {
        std::string output = ExecCommand("pactl -f json list sink-inputs");
        try
        {
            auto items = json::parse(output);
            for (auto& item : items)
            {
                if (item.value("index", 0u) == sinkInputID)
                {
                    sinkID = item.value("sink", static_cast<uint32_t>(-1));
                    return sinkID != static_cast<uint32_t>(-1);
                }
            }
        }
        catch (...) {}
        return false;
    }

    bool GetCurrentSourceForSourceOutput(uint32_t sourceOutputID, uint32_t& sourceID)
    {
        std::string output = ExecCommand("pactl -f json list source-outputs");
        try
        {
            auto items = json::parse(output);
            for (auto& item : items)
            {
                if (item.value("index", 0u) == sourceOutputID)
                {
                    sourceID = item.value("source", static_cast<uint32_t>(-1));
                    return sourceID != static_cast<uint32_t>(-1);
                }
            }
        }
        catch (...) {}
        return false;
    }

    bool VerifySinkInput(uint32_t sinkInputID, uint32_t expectedSinkID)
    {
        std::string output = ExecCommand("pactl -f json list sink-inputs");
        try
        {
            auto items = json::parse(output);
            for (auto& item : items)
            {
                if (item.value("index", 0u) == sinkInputID)
                    return item.value("sink", static_cast<uint32_t>(-1)) == expectedSinkID;
            }
        }
        catch (...) {}
        return false;
    }

    bool VerifySourceOutput(uint32_t sourceOutputID, uint32_t expectedSourceID)
    {
        std::string output = ExecCommand("pactl -f json list source-outputs");
        try
        {
            auto items = json::parse(output);
            for (auto& item : items)
            {
                if (item.value("index", 0u) == sourceOutputID)
                    return item.value("source", static_cast<uint32_t>(-1)) == expectedSourceID;
            }
        }
        catch (...) {}
        return false;
    }

    bool VerifyLoopback(uint32_t moduleID, const std::string& sourceName, const std::string& sinkName)
    {
        std::string output = ExecCommand("pactl -f json list modules");
        try
        {
            auto items = json::parse(output);
            for (auto& item : items)
            {
                if (item.value("index", 0u) != moduleID)
                    continue;

                if (item.value("name", "") != "module-loopback")
                    return false;

                std::string args = item.value("argument", item.value("args", ""));
                return args.find("source=" + sourceName) != std::string::npos &&
                       args.find("sink=" + sinkName) != std::string::npos;
            }
        }
        catch (...) {}

        // Some pactl versions do not expose module JSON. Fall back to the
        // human-readable module list, still requiring the exact module ID and
        // both endpoint names.
        output = ExecCommand("pactl list modules");
        std::istringstream lines(output);
        std::string line;
        bool inModule = false;
        bool idOK = false;
        bool nameOK = false;
        bool sourceOK = false;
        bool sinkOK = false;
        while (std::getline(lines, line))
        {
            if (line.rfind("Module #", 0) == 0)
            {
                if (inModule && idOK)
                    return nameOK && sourceOK && sinkOK;
                inModule = true;
                idOK = line.find("#" + std::to_string(moduleID)) != std::string::npos;
                nameOK = sourceOK = sinkOK = false;
            }
            if (!inModule || !idOK)
                continue;
            if (line.find("Name: module-loopback") != std::string::npos) nameOK = true;
            if (line.find("source=") != std::string::npos && line.find(sourceName) != std::string::npos) sourceOK = true;
            if (line.find("sink=") != std::string::npos && line.find(sinkName) != std::string::npos) sinkOK = true;
        }
        return inModule && idOK && nameOK && sourceOK && sinkOK;
    }

    bool VerifyNullSink(const std::string& sinkName, std::string& monitorName, uint32_t* sinkID = nullptr)
    {
        std::string output = ExecCommand("pactl -f json list sinks");
        try
        {
            auto items = json::parse(output);
            for (auto& item : items)
            {
                if (item.value("name", "") == sinkName)
                {
                    if (sinkID) *sinkID = item.value("index", 0u);
                    monitorName = item.value("monitor_source", "");
                    if (monitorName.empty())
                        monitorName = sinkName + ".monitor";
                    return FindSourceByName(monitorName);
                }
            }
        }
        catch (...) {}
        return false;
    }

    bool ApplyRoute(Pin* a, Pin* b, Link& link)
    {
        Pin* sinkInput = nullptr;
        Pin* sourceOutput = nullptr;
        Pin* sink = nullptr;
        Pin* source = nullptr;

        if (a->Type == PinType::SinkInput) sinkInput = a;
        if (b->Type == PinType::SinkInput) sinkInput = b;
        if (a->Type == PinType::SourceOutput) sourceOutput = a;
        if (b->Type == PinType::SourceOutput) sourceOutput = b;
        if (a->Type == PinType::Sink) sink = a;
        if (b->Type == PinType::Sink) sink = b;
        if (a->Type == PinType::Source) source = a;
        if (b->Type == PinType::Source) source = b;

        link.Type = GetRouteType(a, b);
        LogMessage("route attempt type=" + std::string(RouteName(link.Type)) +
                   " from=" + DescribePin(a) + " to=" + DescribePin(b));
        auto fail = [&](const char* reason)
        {
            LogMessage("route failed type=" + std::string(RouteName(link.Type)) + " reason=" + reason);
            return false;
        };
        if (link.Type == RouteType::Invalid)
        {
            return fail("invalid endpoint combination");
        }

        if (link.Type == RouteType::PlaybackToSink)
        {
            if (!GetCurrentSinkForSinkInput(sinkInput->Node->PA_ID, link.PreviousSinkID))
                return fail("cannot read current sink-input destination");
            if (!PactlMoveSinkInput(sinkInput->Node->PA_ID, sink->Node->PA_ID))
                return fail("move-sink-input command failed");
            if (!VerifySinkInput(sinkInput->Node->PA_ID, sink->Node->PA_ID))
                return fail("sink-input destination verification failed");
            link.Managed = true;
            LogMessage("route applied type=playback-to-sink");
            return true;
        }

        if (link.Type == RouteType::SourceToCapture)
        {
            if (!GetCurrentSourceForSourceOutput(sourceOutput->Node->PA_ID, link.PreviousSourceID))
                return fail("cannot read current source-output source");
            if (!PactlMoveSourceOutput(sourceOutput->Node->PA_ID, SourceEndpointID(source)))
                return fail("move-source-output command failed");
            if (!VerifySourceOutput(sourceOutput->Node->PA_ID, SourceEndpointID(source)))
                return fail("source-output source verification failed");
            link.Managed = true;
            LogMessage("route applied type=source-to-capture");
            return true;
        }

        if (link.Type == RouteType::SourceToSink)
        {
            uint32_t moduleID = 0;
            if (!PactlCreateLoopback(SourceEndpointName(source), sink->Node->PA_Name, moduleID))
                return fail("loopback module creation failed");

            if (!VerifyLoopback(moduleID, SourceEndpointName(source), sink->Node->PA_Name))
            {
                PactlUnloadModule(moduleID);
                return fail("loopback module verification failed");
            }

            link.Resources.emplace_back(RouteResourceType::Loopback, moduleID);
            link.Managed = true;
            LogMessage("route applied type=source-to-sink module=" + std::to_string(moduleID));
            return true;
        }

        if (link.Type == RouteType::PlaybackToCapture)
        {
            // Playback -> Capture is implemented as:
            //
            //   SinkInput -> Soundmapper null sink -> null sink monitor
            //              -> SourceOutput
            //
            // A SourceOutput can have only one source, so multiple playback
            // streams targeting the same capture application share one bus.
            std::string busName;
            uint32_t busID = 0;
            uint32_t monitorID = 0;
            std::string monitorName;
            uint32_t sharedModuleID = 0;
            uint32_t sharedPreviousSourceID = static_cast<uint32_t>(-1);
            bool reusedBus = false;

            for (const auto& existing : m_Links)
            {
                if (existing.Type != RouteType::PlaybackToCapture ||
                    existing.EndPinID != sourceOutput->ID && existing.StartPinID != sourceOutput->ID)
                    continue;

                for (const auto& resource : existing.Resources)
                {
                    if (resource.Type == RouteResourceType::NullSink && !resource.PA_Name.empty())
                    {
                        busName = resource.PA_Name;
                        sharedModuleID = resource.PA_Module_ID;
                        sharedPreviousSourceID = existing.PreviousSourceID;
                        reusedBus = true;
                        break;
                    }
                }
                if (reusedBus)
                    break;
            }

            if (!reusedBus)
                busName = "soundmapper_bus_" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(link.ID.AsPointer())));

            if (!VerifyNullSink(busName, monitorName, &busID))
            {
                uint32_t moduleID = 0;
                if (!PactlCreateNullSink(busName, "Soundmapper Bus", moduleID))
                    return fail("virtual bus creation failed");

                link.Resources.emplace_back(RouteResourceType::NullSink, moduleID, busName);

                if (!VerifyNullSink(busName, monitorName, &busID))
                {
                    RemoveRouteResources(link);
                    return fail("virtual bus verification failed");
                }
            }

            if (!GetCurrentSinkForSinkInput(sinkInput->Node->PA_ID, link.PreviousSinkID))
            {
                if (!reusedBus)
                    RemoveRouteResources(link);
                return fail("cannot read current sink-input destination");
            }

            if (reusedBus)
            {
                link.PreviousSourceID = sharedPreviousSourceID;
                link.Resources.emplace_back(RouteResourceType::NullSink, sharedModuleID, busName);
            }

            if (!PactlMoveSinkInput(sinkInput->Node->PA_ID, busID) ||
                !VerifySinkInput(sinkInput->Node->PA_ID, busID))
            {
                if (!reusedBus)
                    RemoveRouteResources(link);
                return fail("move-sink-input or verification failed");
            }

            if (!FindSourceByName(monitorName, &monitorID))
            {
                if (!reusedBus)
                    RemoveRouteResources(link);
                return fail("cannot find virtual bus monitor");
            }

            if (!reusedBus)
            {
                Pin* currentSource = nullptr;
                for (const auto& existing : m_Links)
                {
                    if (existing.Type != RouteType::SourceToCapture)
                        continue;
                    Pin* existingStart = FindPin(existing.StartPinID);
                    Pin* existingEnd = FindPin(existing.EndPinID);
                    if (existingStart && existingStart->Type == PinType::Source &&
                        existingEnd == sourceOutput)
                    {
                        currentSource = existingStart;
                        break;
                    }
                }

                if (currentSource)
                {
                    uint32_t loopbackModuleID = 0;
                    if (!PactlCreateLoopback(currentSource->Node->PA_Name, busName, loopbackModuleID) ||
                        !VerifyLoopback(loopbackModuleID, currentSource->Node->PA_Name, busName))
                    {
                        if (loopbackModuleID)
                            PactlUnloadModule(loopbackModuleID);
                        RemoveRouteResources(link);
                        return fail("existing source loopback creation or verification failed");
                    }
                    link.Resources.emplace_back(RouteResourceType::Loopback, loopbackModuleID);
                    LogMessage("playback-to-capture mixed existing source=" + DescribePin(currentSource));
                }

                if (!GetCurrentSourceForSourceOutput(sourceOutput->Node->PA_ID, link.PreviousSourceID))
                {
                    RemoveRouteResources(link);
                    return fail("cannot read current source-output source");
                }

                // First route owns the bus and must attach the target capture
                // stream to its monitor. Subsequent routes reuse that same
                // source, so moving the SourceOutput again is unnecessary.
                if (!PactlMoveSourceOutput(sourceOutput->Node->PA_ID, monitorID) ||
                    !VerifySourceOutput(sourceOutput->Node->PA_ID, monitorID))
                {
                    RemoveRouteResources(link);
                    return fail("move-source-output or verification failed");
                }
            }
            else if (!VerifySourceOutput(sourceOutput->Node->PA_ID, monitorID))
            {
                return fail("shared virtual bus source-output verification failed");
            }

            link.Managed = true;
            LogMessage("route applied type=playback-to-capture bus=" + busName);
            return true;
        }
        return fail("no implementation");
    }

    void RestoreRouteEndpoints(Link& link)
    {
        Pin* startPin = FindPin(link.StartPinID);
        Pin* endPin = FindPin(link.EndPinID);
        Pin* sinkInput = nullptr;
        Pin* sourceOutput = nullptr;

        if (startPin && startPin->Type == PinType::SinkInput) sinkInput = startPin;
        if (endPin && endPin->Type == PinType::SinkInput) sinkInput = endPin;
        if (startPin && startPin->Type == PinType::SourceOutput) sourceOutput = startPin;
        if (endPin && endPin->Type == PinType::SourceOutput) sourceOutput = endPin;

        if (sinkInput && link.PreviousSinkID != static_cast<uint32_t>(-1))
            PactlMoveSinkInput(sinkInput->Node->PA_ID, link.PreviousSinkID);

        if (sourceOutput && link.PreviousSourceID != static_cast<uint32_t>(-1))
        {
            bool sharedCaptureRoute = false;
            if (link.Type == RouteType::PlaybackToCapture)
            {
                for (const auto& other : m_Links)
                {
                    if (&other == &link || other.Type != RouteType::PlaybackToCapture)
                        continue;
                    if (other.StartPinID == sourceOutput->ID || other.EndPinID == sourceOutput->ID)
                    {
                        sharedCaptureRoute = true;
                        break;
                    }
                }
            }

            if (!sharedCaptureRoute)
                PactlMoveSourceOutput(sourceOutput->Node->PA_ID, link.PreviousSourceID);
        }
    }

    void RemoveRouteResources(Link& link)
    {
        for (auto it = link.Resources.rbegin(); it != link.Resources.rend(); ++it)
        {
            bool shared = false;
            if (it->Type == RouteResourceType::NullSink && !it->PA_Name.empty())
            {
                for (const auto& other : m_Links)
                {
                    if (&other == &link)
                        continue;
                    for (const auto& otherResource : other.Resources)
                    {
                        if (otherResource.Type == RouteResourceType::NullSink &&
                            otherResource.PA_Name == it->PA_Name)
                        {
                            shared = true;
                            break;
                        }
                    }
                    if (shared)
                        break;
                }
            }

            if (!shared)
                PactlUnloadModule(it->PA_Module_ID);
        }
        link.Resources.clear();
    }

    void RemoveLinksUsingPin(ed::PinId pinId)
    {
        for (auto it = m_Links.begin(); it != m_Links.end();)
        {
            if (it->StartPinID == pinId || it->EndPinID == pinId)
            {
                if (it->Managed)
                {
                    RestoreRouteEndpoints(*it);
                    RemoveRouteResources(*it);
                }
                it = m_Links.erase(it);
            }
            else
                ++it;
        }
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

    Node* SpawnNullSinkNode(uint32_t pa_id = 0, const char* name = "Null Sink")
    {
        m_Nodes.emplace_back(GetNextId(), name, pa_id, ImColor(255, 128, 255));
        m_Nodes.back().IsNullSink = true;
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "Input", PinType::Sink);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Output", PinType::Source);
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

    static bool IsMonitorSource(const json& source)
    {
        const std::string name = source.value("name", "");
        const bool nameIsMonitor = name.size() >= 8 &&
            name.compare(name.size() - 8, 8, ".monitor") == 0;
        const bool classIsMonitor = source.contains("properties") &&
            source["properties"].value("device.class", "") == "monitor";
        return nameIsMonitor || classIsMonitor;
    }

    uint32_t GetDefaultSinkID()
    {
        std::string name = ExecCommand("pactl get-default-sink");
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ' || name.back() == '\t'))
            name.pop_back();
        uint32_t id = static_cast<uint32_t>(-1);
        return FindSinkByName(name, &id) ? id : static_cast<uint32_t>(-1);
    }

    uint32_t GetDefaultSourceID()
    {
        std::string name = ExecCommand("pactl get-default-source");
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ' || name.back() == '\t'))
            name.pop_back();
        uint32_t id = static_cast<uint32_t>(-1);
        return FindSourceByName(name, &id) ? id : static_cast<uint32_t>(-1);
    }

    Link* AddGraphLink(Pin* a, Pin* b, RouteType type, bool managed = false,
                       uint32_t moduleID = 0, const std::string& resourceName = "")
    {
        if (!a || !b || !CanCreateLink(a, b) || type == RouteType::Invalid)
            return nullptr;

        // The graph has a stable visual convention: audio always flows from
        // Output -> Input, regardless of which endpoint the user dragged from.
        Pin* start = a;
        Pin* end = b;
        if (start->Kind == PinKind::Input)
            std::swap(start, end);

        // Avoid duplicate logical edges after a PulseAudio rescan.
        for (const auto& existing : m_Links)
        {
            if (existing.StartPinID == start->ID && existing.EndPinID == end->ID)
                return &const_cast<Link&>(existing);
        }

        Link link(GetNextId(), start->ID, end->ID);
        link.Color = GetIconColor(start->Type);
        link.Type = type;
        link.Managed = managed;
        if (moduleID)
        {
            RouteResourceType resourceType =
                type == RouteType::SourceToSink ? RouteResourceType::Loopback : RouteResourceType::NullSink;
            link.Resources.emplace_back(resourceType, moduleID, resourceName);
        }
        m_Links.emplace_back(std::move(link));
        return &m_Links.back();
    }

    static std::string ModuleArgument(const std::string& args, const std::string& key)
    {
        // pactl's module argument syntax is key=value. Names generated by
        // Soundmapper contain no spaces, so this intentionally stays simple.
        const std::string needle = key + "=";
        size_t pos = args.find(needle);
        if (pos == std::string::npos)
            return {};
        pos += needle.size();

        if (pos < args.size() && args[pos] == '\'')
        {
            ++pos;
            size_t end = args.find('\'', pos);
            return args.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        }

        size_t end = args.find_first_of(" \t\n", pos);
        return args.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }

    struct ModuleInfo
    {
        uint32_t ID = 0;
        std::string Name;
        std::string Args;
    };

    std::vector<ModuleInfo> GetPulseModules()
    {
        std::vector<ModuleInfo> modules;
        std::string output = ExecCommand("pactl list modules");
        std::istringstream stream(output);
        std::string line;
        ModuleInfo current;
        bool have = false;

        auto flush = [&]()
        {
            if (have)
                modules.push_back(current);
            current = ModuleInfo{};
            have = false;
        };

        while (std::getline(stream, line))
        {
            if (line.rfind("Module #", 0) == 0)
            {
                flush();
                try
                {
                    current.ID = static_cast<uint32_t>(std::stoul(line.substr(8)));
                    have = true;
                }
                catch (...) {}
            }
            else if (have)
            {
                const size_t first = line.find_first_not_of(" \t");
                if (first == std::string::npos)
                    continue;
                const std::string field = line.substr(first);
                if (field.rfind("Name:", 0) == 0)
                    current.Name = field.substr(5);
                else if (field.rfind("Argument:", 0) == 0)
                    current.Args = field.substr(9);
            }
        }
        flush();
        return modules;
    }

    void RefreshGraph()
    {
        // A refresh rebuilds the *logical* graph from the current PulseAudio
        // state. Internal Soundmapper buses/monitors are deliberately hidden;
        // the user sees SinkInput -> SourceOutput instead of the null-sink
        // implementation used underneath.
        m_Nodes.clear();
        m_Nodes.reserve(1000);
        m_Links.clear();
        m_AvailableSinks.clear();
        m_AvailableSources.clear();
        m_AvailableSinkInputs.clear();
        m_AvailableSourceOutputs.clear();

        std::string sinks_json = ExecCommand("pactl -f json list sinks");
        std::string sources_json = ExecCommand("pactl -f json list sources");
        std::string sink_inputs_json = ExecCommand("pactl -f json list sink-inputs");
        std::string source_outputs_json = ExecCommand("pactl -f json list source-outputs");

        try
        {
            auto sinks = json::parse(sinks_json);
            auto sources = json::parse(sources_json);
            auto sink_inputs = json::parse(sink_inputs_json);
            auto source_outputs = json::parse(source_outputs_json);
            LogMessage("refresh sinks=" + std::to_string(sinks.size()) +
                       " sources=" + std::to_string(sources.size()) +
                       " sink-inputs=" + std::to_string(sink_inputs.size()) +
                       " source-outputs=" + std::to_string(source_outputs.size()));

            std::map<uint32_t, Node*> sink_nodes;
            std::map<uint32_t, Node*> source_nodes;
            std::map<std::string, Node*> sink_name_nodes;
            std::map<std::string, Node*> source_name_nodes;
            std::map<std::string, Node*> null_sink_monitor_nodes;
            const auto modules = GetPulseModules();
            std::map<std::string, uint32_t> null_sink_modules;
            for (const auto& module : modules)
            {
                if (module.Name == "module-null-sink")
                    null_sink_modules[ModuleArgument(module.Args, "sink_name")] = module.ID;
            }

            float y = 0;
            for (auto& s : sinks)
            {
                uint32_t id = s.value("index", 0u);
                std::string pa_name = s.value("name", "");
                std::string desc = s.value("description", pa_name.empty() ? "Unknown Sink" : pa_name);

                auto nullSinkModule = null_sink_modules.find(pa_name);
                const bool isNullSink = s.value("driver", "") == "module-null-sink.c";
                if (isNullSink)
                {
                    Node* n = SpawnNullSinkNode(id, desc.c_str());
                    n->PA_Name = pa_name;
                    n->PA_Monitor_Name = s.value("monitor_source", pa_name + ".monitor");
                    n->PA_Module_ID = nullSinkModule != null_sink_modules.end()
                        ? nullSinkModule->second
                        : s.value("owner_module", 0u);
                    n->Details = ParseDetails(s);
                    ed::SetNodePosition(n->ID, ImVec2(100, y));
                    sink_nodes[id] = n;
                    sink_name_nodes[pa_name] = n;
                    null_sink_monitor_nodes[n->PA_Monitor_Name] = n;
                    y += 150;
                    continue;
                }

                m_AvailableSinks.push_back({id, desc, pa_name});
                Node* n = SpawnSinkNode(id, desc.c_str());
                n->PA_Name = pa_name;
                n->Details = ParseDetails(s);
                ed::SetNodePosition(n->ID, ImVec2(500, y));
                sink_nodes[id] = n;
                sink_name_nodes[pa_name] = n;
                y += 150;
            }

            y = 0;
            for (auto& s : sources)
            {
                uint32_t id = s.value("index", 0u);
                std::string pa_name = s.value("name", "");
                std::string desc = s.value("description", pa_name.empty() ? "Unknown Source" : pa_name);

                auto nullSinkNode = null_sink_monitor_nodes.find(pa_name);
                if (nullSinkNode != null_sink_monitor_nodes.end())
                {
                    nullSinkNode->second->PA_Monitor_ID = id;
                    source_nodes[id] = nullSinkNode->second;
                    source_name_nodes[pa_name] = nullSinkNode->second;
                    continue;
                }

                if (IsMonitorSource(s))
                    continue;

                m_AvailableSources.push_back({id, desc, pa_name});
                Node* n = SpawnSourceNode(id, desc.c_str());
                n->PA_Name = pa_name;
                n->Details = ParseDetails(s);
                ed::SetNodePosition(n->ID, ImVec2(-500, y));
                source_nodes[id] = n;
                source_name_nodes[pa_name] = n;
                y += 150;
            }

            std::map<uint32_t, Node*> sink_input_nodes;
            std::map<uint32_t, Node*> source_output_nodes;
            y = 0;
            for (auto& si : sink_inputs)
            {
                uint32_t id = si.value("index", 0u);
                std::string name = si.value("name", "Unknown Sink-Input");
                if (si.contains("properties"))
                {
                    if (si["properties"].contains("application.name"))
                        name = si["properties"]["application.name"];
                    if (si["properties"].contains("media.name"))
                    {
                        std::string media = si["properties"]["media.name"];
                        if (media.length() > 25) media = media.substr(0, 22) + "...";
                        name += " - " + media;
                    }
                }

                std::string pa_name = si.value("name", name);
                m_AvailableSinkInputs.push_back({id, name, pa_name});
                Node* n = SpawnSinkInputNode(id, name.c_str());
                n->PA_Name = pa_name;
                n->Details = ParseDetails(si);
                ed::SetNodePosition(n->ID, ImVec2(0, y));
                sink_input_nodes[id] = n;
                y += 150;

                uint32_t target_sink = si.value("sink", static_cast<uint32_t>(-1));
                if (target_sink != static_cast<uint32_t>(-1) && sink_nodes.count(target_sink))
                    AddGraphLink(&n->Outputs[0],
                                 &sink_nodes[target_sink]->Inputs[0], RouteType::PlaybackToSink);
            }

            y = 0;
            for (auto& so : source_outputs)
            {
                uint32_t id = so.value("index", 0u);
                std::string name = so.value("name", "Unknown Source-Output");
                if (so.contains("properties"))
                {
                    if (so["properties"].contains("application.name"))
                        name = so["properties"]["application.name"];
                    if (so["properties"].contains("media.name"))
                    {
                        std::string media = so["properties"]["media.name"];
                        if (media.length() > 25) media = media.substr(0, 22) + "...";
                        name += " - " + media;
                    }
                }

                std::string pa_name = so.value("name", name);
                m_AvailableSourceOutputs.push_back({id, name, pa_name});
                Node* n = SpawnSourceOutputNode(id, name.c_str());
                n->PA_Name = pa_name;
                n->Details = ParseDetails(so);
                ed::SetNodePosition(n->ID, ImVec2(-250, y));
                source_output_nodes[id] = n;
                y += 150;

                uint32_t target_source = so.value("source", static_cast<uint32_t>(-1));
                if (target_source != static_cast<uint32_t>(-1) && source_nodes.count(target_source))
                    AddGraphLink(&source_nodes[target_source]->Outputs[0], &n->Inputs[0], RouteType::SourceToCapture);
            }

            for (const auto& module : modules)
            {
                if (module.Name != "module-loopback")
                    continue;

                const std::string sourceName = ModuleArgument(module.Args, "source");
                const std::string sinkName = ModuleArgument(module.Args, "sink");
                auto sourceIt = source_name_nodes.find(sourceName);
                auto sinkIt = sink_name_nodes.find(sinkName);
                if (sourceIt != source_name_nodes.end() && sinkIt != sink_name_nodes.end())
                    AddGraphLink(&sourceIt->second->Outputs[0], &sinkIt->second->Inputs[0],
                                 RouteType::SourceToSink, true, module.ID);
            }

        }
        catch (...) {
            printf("Failed to parse PulseAudio JSON\n");
            LogMessage("refresh failed: PulseAudio JSON parse error");
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
        std::ofstream(kLogPath, std::ios::trunc) << "Soundmapper log started\n";
        LogMessage("application started");
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
                } else if (!node->IsNullSink && node->Inputs.size() && node->Inputs[0].Type == PinType::Sink) {
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
            CreateNullSink();
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
                                LogMessage("link rejected: incompatible endpoints from=" + DescribePin(startPin) + " to=" + DescribePin(endPin));
                                showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                                ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                            }
                            else
                            {
                                showLabel("+ Create Link", ImColor(32, 45, 32, 180));
                                if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                                {
                                    LogMessage("link accepted by UI from=" + DescribePin(startPin) + " to=" + DescribePin(endPin));
                                    // SinkInput has exactly one playback destination.
                                    // SourceOutput has exactly one capture source, but multiple
                                    // logical Playback->Capture links can share the same bus.
                                    if (startPin->Type == PinType::SinkInput)
                                        RemoveLinksUsingPin(startPinId);
                                    if (endPin->Type == PinType::SinkInput)
                                        RemoveLinksUsingPin(endPinId);

                                    // A SourceOutput can consume only one Source.
                                    // Multiple Playback->Capture routes are the
                                    // exception: they intentionally share one bus.
                                    if (startPin->Type == PinType::SourceOutput || endPin->Type == PinType::SourceOutput)
                                    {
                                        Pin* so = startPin->Type == PinType::SourceOutput ? startPin : endPin;
                                        RouteType newType = GetRouteType(startPin, endPin);
                                        if (newType != RouteType::PlaybackToCapture)
                                            RemoveLinksUsingPin(so->ID);
                                    }

                                    Link candidate(GetNextId(), startPinId, endPinId);
                                    candidate.Color = GetIconColor(startPin->Type);

                                    if (ApplyRoute(startPin, endPin, candidate))
                                    {
                                        m_Links.emplace_back(std::move(candidate));
                                        LogMessage("link added to graph");
                                    }
                                    else
                                    {
                                        LogMessage("link not added: route application failed");
                                        showLabel("x Route failed / verification failed", ImColor(70, 32, 32, 220));
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
                                if (id->Managed)
                                {
                                    LogMessage("link deleted: restoring managed route");
                                    RestoreRouteEndpoints(*id);
                                    RemoveRouteResources(*id);
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
            if (ImGui::MenuItem("Null Sink"))
                CreateNullSink();

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

                            Link candidate(GetNextId(), startPin->ID, endPin->ID);
                            candidate.Color = GetIconColor(startPin->Type);
                            if (ApplyRoute(startPin, endPin, candidate))
                                m_Links.emplace_back(std::move(candidate));

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
