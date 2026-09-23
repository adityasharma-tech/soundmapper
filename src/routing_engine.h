#pragma once

#include <array>

namespace soundmapper
{

// These are logical graph endpoint types, not PulseAudio object names. Keeping
// them independent of the UI makes the routing policy straightforward to test.
enum class EndpointType
{
    SinkInput,
    SourceOutput,
    Sink,
    Source
};

enum class RouteType
{
    Invalid,
    PlaybackToSink,
    SourceToCapture,
    SourceToSink,
    PlaybackToCapture
};

struct RouteRule
{
    EndpointType From;
    EndpointType To;
    RouteType Type;
};

class RoutingEngine
{
public:
    // The complete, user-facing compatibility matrix. Monitor sources do not
    // have an EndpointType and therefore can never be offered as a route.
    static const std::array<RouteRule, 4>& Rules()
    {
        static const std::array<RouteRule, 4> rules = {{
            {EndpointType::SinkInput,    EndpointType::Sink,         RouteType::PlaybackToSink},
            {EndpointType::Source,       EndpointType::SourceOutput, RouteType::SourceToCapture},
            {EndpointType::Source,       EndpointType::Sink,         RouteType::SourceToSink},
            {EndpointType::SinkInput,    EndpointType::SourceOutput, RouteType::PlaybackToCapture},
        }};
        return rules;
    }

    // A graph drag may start at either end. Return the same route for the
    // reverse pair; callers normalize visual direction separately.
    static RouteType Decide(EndpointType first, EndpointType second)
    {
        for (const auto& rule : Rules())
        {
            if ((rule.From == first && rule.To == second) ||
                (rule.From == second && rule.To == first))
                return rule.Type;
        }
        return RouteType::Invalid;
    }

    static bool CanConnect(EndpointType first, EndpointType second)
    {
        return Decide(first, second) != RouteType::Invalid;
    }
};

} // namespace soundmapper
