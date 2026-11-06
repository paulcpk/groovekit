// The multi-sampler's engine side for the UI and the agent: how its layers are matched and fitted
// (stack.info), and lining up its hits (stack.align).

#include "ApiHandler.h"
#include "Rpc.h"

#include "sampler/Stack.h"

namespace gk
{

Json ApiHandler::callStack (const std::string& method, const Json& params)
{
    const auto id = readParam<std::string> (params, "channel");
    const auto* channel = model::findChannel (session.getProject(), id);

    if (channel == nullptr)
        throw RpcError (invalidParams, "No channel \"" + id + "\"");

    const auto* stack = std::get_if<model::StackInstrument> (&channel->instrument);

    if (stack == nullptr)
        throw RpcError (invalidParams, "\"" + id + "\" isn't a multi-sampler");

    const auto dir = projectDirOf (session);
    const auto& project = session.getProject();

    if (method == "stack.info")
    {
        const auto mix = sampler::mixStack (*stack, dir, project.tempo);
        const auto timings = stack->mode == model::StackInstrumentMode::loops ? sampler::loopTimings (*stack, dir, project.tempo)
                                                                              : std::map<model::Id, sampler::LoopTiming> {};
        const auto barSeconds = 60.0 / project.tempo * 4.0 * project.timeSignature.numerator / project.timeSignature.denominator;
        auto layers = Json::object();

        for (auto& l : stack->layers)
        {
            Json j = Json::object();

            if (auto m = mix.layers.find (l.id); m != mix.layers.end())
                j = { { "loudnessDb", std::round (m->second.loudnessDb * 10) / 10 }, { "matchDb", std::round (m->second.matchDb * 10) / 10 },
                      { "gainDb", std::round (m->second.gainDb * 10) / 10 } };

            if (auto t = timings.find (l.id); t != timings.end())
            {
                j["rate"] = std::round (t->second.rate * 1000) / 1000;
                j["bars"] = std::round (t->second.cycle / barSeconds * 100) / 100;
                j["seconds"] = std::round (t->second.cycle * 1000) / 1000;
            }

            layers[l.id] = j;
        }

        return { { "trimDb", std::round (mix.trimDb * 10) / 10 }, { "layers", layers } };
    }

    if (method == "stack.align")
    {
        const auto offsets = sampler::alignStack (*stack, dir);
        std::vector<model::Command> commands;

        for (auto& [layer, ms] : offsets)
            commands.push_back (model::UpdateStackLayer { id, layer, Json { { "offsetMs", ms } } });

        if (! commands.empty())
        {
            ExecuteOptions options;
            options.origin = params.value ("origin", "ui");
            options.label = "Line up the layers of " + (channel->name.empty() ? id : channel->name);
            session.execute (commands, options);
        }

        return { { "offsets", offsets } };
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

} // namespace gk
