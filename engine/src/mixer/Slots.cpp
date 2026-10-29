#include "Slots.h"

#include "analysis/Analysis.h"
#include "effects/EqPlugin.h"

#include <juce_core/juce_core.h>

namespace gk::mixer
{

using namespace model;

namespace
{
    constexpr const char* slotId = "slot";
    constexpr ChannelPart allParts[] = { ChannelPart::kick, ChannelPart::rumble, ChannelPart::drums, ChannelPart::bass, ChannelPart::percussion,
                                         ChannelPart::hats, ChannelPart::synth, ChannelPart::vocal, ChannelPart::fx };

    double typeIndex (effects::EqPlugin::BandType t)    { return static_cast<double> (t); }

    double round (double v, double step)
    {
        return std::round (v / step) * step;
    }

    /** How far the references may move a part's cuts. */
    struct Range { double lowMin, lowMax, highMin, highMax; };

    std::optional<Range> rangeFor (ChannelPart part)
    {
        switch (part)
        {
            case ChannelPart::bass:     return Range { 25, 60, 800, 5000 };
            case ChannelPart::synth:    return Range { 100, 350, 5000, 14000 };
            case ChannelPart::vocal:    return Range { 90, 250, 8000, 16000 };
            default:                    return std::nullopt;
        }
    }

    bool hasWord (const std::vector<std::string>& words, std::initializer_list<const char*> any)
    {
        for (auto& w : words)
            for (auto* a : any)
                if (w.starts_with (a))
                    return true;

        return false;
    }
}

Json Slot::toJson() const
{
    Json j { { "from", from } };
    j["lowCutHz"] = lowCutHz > 0 ? Json (round (lowCutHz, 1)) : Json();
    j["highCutHz"] = highCutHz > 0 ? Json (round (highCutHz, 1)) : Json();

    if (steep)
        j["steep"] = true;

    if (topShelfDb != 0)
        j["topShelf"] = { { "db", round (topShelfDb, 0.1) }, { "hz", round (topShelfHz, 1) } };

    return j;
}

Slot defaultSlot (ChannelPart part)
{
    switch (part)
    {
        case ChannelPart::kick:         return { 30, 5000, true };
        case ChannelPart::rumble:       return { 30, 250, true };
        case ChannelPart::drums:        return { 30, 0, false, -1.5, 10000 };
        case ChannelPart::bass:         return { 35, 2500 };
        case ChannelPart::percussion:   return { 250, 16000, false, -2, 9000 };
        case ChannelPart::hats:         return { 450, 0, false, -3, 11000 };
        case ChannelPart::synth:        return { 180, 9000 };
        case ChannelPart::vocal:        return { 140, 14000, false, -1, 10000 };
        case ChannelPart::fx:           return { 200, 12000 };
    }

    return {};
}

Slot fitToReferences (Slot slot, ChannelPart part, const std::vector<float>& stem)
{
    const auto range = rangeFor (part);
    const auto& edges = analysis::bandEdges();

    if (! range || stem.size() != edges.size())
        return slot;

    // The part lives in the bands within 15 dB of its strongest: cut below and above them.
    const auto peak = *std::max_element (stem.begin(), stem.end());

    if (peak <= analysis::silenceDb)
        return slot;

    const auto threshold = peak - 15;
    std::optional<size_t> first, last;

    for (size_t b = 0; b < stem.size(); ++b)
        if (stem[b] >= threshold)
        {
            first = first.value_or (b);
            last = b;
        }

    slot.lowCutHz = std::clamp (edges[*first].first, range->lowMin, range->lowMax);
    slot.highCutHz = std::clamp (edges[*last].second, range->highMin, range->highMax);
    slot.from = "references";
    return slot;
}

ChannelPart guessPart (const std::string& name, const std::string& category, bool loop, float lowShare)
{
    std::vector<std::string> words;

    for (auto& w : juce::StringArray::fromTokens (juce::String (name).toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789 _-"), " _-", ""))
        words.push_back (w.toStdString());

    if (hasWord (words, { "rumble" }))                                          return ChannelPart::rumble;
    if (hasWord (words, { "kick", "bd" }))                                      return ChannelPart::kick;
    if (hasWord (words, { "bass", "sub", "808", "reese" }))                     return ChannelPart::bass;
    if (hasWord (words, { "hat", "hihat", "hh", "cymbal", "ride", "crash" }))   return ChannelPart::hats;
    if (hasWord (words, { "perc", "clap", "snare", "rim", "tom", "conga", "bongo", "shaker", "tamb", "cowbell", "top" }))
        return ChannelPart::percussion;
    if (hasWord (words, { "vocal", "vox", "voice", "chant" }))                  return ChannelPart::vocal;
    if (hasWord (words, { "fx", "riser", "sweep", "impact", "noise", "atmo", "texture" }))
        return ChannelPart::fx;
    if (hasWord (words, { "synth", "pad", "chord", "stab", "lead", "arp", "pluck", "keys", "string", "bell", "organ" }))
        return ChannelPart::synth;

    if (category == "kick")                                                     return ChannelPart::kick;
    if (category == "hat" || category == "cymbal")                              return ChannelPart::hats;
    if (category == "snare" || category == "clap" || category == "tom" || category == "perc")
        return ChannelPart::percussion;
    if (category == "drums")                                                    return loop && lowShare > 0.35f ? ChannelPart::drums : ChannelPart::percussion;
    if (category == "bass")                                                     return ChannelPart::bass;
    if (category == "vocal")                                                    return ChannelPart::vocal;
    if (category == "fx")                                                       return ChannelPart::fx;

    return ChannelPart::synth;
}

BuiltinEffect slotEffect (const Slot& slot)
{
    using Type = effects::EqPlugin::BandType;
    const auto& defaults = effects::EqPlugin::bandDefaults;

    BuiltinEffect e;
    e.id = slotId;
    e.kind = BuiltinEffectKind::eq;
    auto& p = e.params;

    p["band1Type"] = typeIndex (slot.lowCutHz > 0 ? Type::lowCut : Type::off);
    p["band1Freq"] = slot.lowCutHz > 0 ? slot.lowCutHz : defaults[0].freq;
    p["band1Q"] = 0.71;

    // Band 6 doubles a steep high cut, or tames the top; band 7 is the high cut.
    if (slot.steep && slot.highCutHz > 0)
    {
        p["band6Type"] = typeIndex (Type::highCut);
        p["band6Freq"] = slot.highCutHz;
        p["band6GainDb"] = 0;
        p["band6Q"] = 0.71;
    }
    else if (slot.topShelfDb != 0)
    {
        p["band6Type"] = typeIndex (Type::highShelf);
        p["band6Freq"] = slot.topShelfHz;
        p["band6GainDb"] = slot.topShelfDb;
        p["band6Q"] = 0.71;
    }
    else
    {
        p["band6Type"] = typeIndex (defaults[5].type);
        p["band6Freq"] = defaults[5].freq;
        p["band6GainDb"] = 0;
        p["band6Q"] = defaults[5].q;
    }

    p["band7Type"] = typeIndex (slot.highCutHz > 0 ? Type::highCut : Type::off);
    p["band7Freq"] = slot.highCutHz > 0 ? slot.highCutHz : defaults[6].freq;
    p["band7Q"] = 0.71;
    return e;
}

std::optional<Slot> readSlot (const Project& project, const Channel& channel)
{
    using Type = effects::EqPlugin::BandType;
    const auto* insert = findInsert (project, channel.insert);

    if (insert == nullptr)
        return {};

    for (auto& effect : insert->effects)
    {
        auto* e = std::get_if<BuiltinEffect> (&effect);

        if (e == nullptr || e->id != slotId || e->kind != BuiltinEffectKind::eq)
            continue;

        auto param = [&] (const std::string& id, double fallback)
        {
            auto it = e->params.find (id);
            return it != e->params.end() ? it->second : fallback;
        };

        const auto& defaults = effects::EqPlugin::bandDefaults;
        auto type = [&] (int band) { return static_cast<Type> (static_cast<int> (param ("band" + std::to_string (band) + "Type", typeIndex (defaults[band - 1].type)))); };

        Slot slot;
        slot.from = "song";

        if (type (1) == Type::lowCut)
            slot.lowCutHz = param ("band1Freq", defaults[0].freq);

        if (type (7) == Type::highCut)
            slot.highCutHz = param ("band7Freq", defaults[6].freq);

        if (type (6) == Type::highCut)
            slot.steep = true;
        else if (type (6) == Type::highShelf && param ("band6GainDb", 0) != 0)
        {
            slot.topShelfDb = param ("band6GainDb", 0);
            slot.topShelfHz = param ("band6Freq", defaults[5].freq);
        }

        return slot;
    }

    return {};
}

std::vector<Command> slotCommands (const Project& project, const Id& id, ChannelPart part, const Slot& slot)
{
    const auto* channel = findChannel (project, id);

    if (channel == nullptr)
        throw CommandError ("No channel \"" + id + "\"");

    std::vector<Command> commands;
    auto insertId = channel->insert;

    const auto shared = insertId == masterInsertId
        || std::any_of (project.channels.begin(), project.channels.end(), [&] (auto& c) { return c.id != id && c.insert == insertId; })
        || std::any_of (project.mixer.inserts.begin(), project.mixer.inserts.end(), [&] (auto& i) { return i.id != masterInsertId && i.output == insertId; });

    const Insert* insert = shared ? nullptr : findInsert (project, insertId);

    if (shared)
    {
        // Its own insert, named after it, feeding where the channel played.
        auto newId = id;

        for (int n = 2; findInsert (project, newId) != nullptr; ++n)
            newId = id + "-mix" + (n > 2 ? std::to_string (n - 1) : "");

        Insert own;
        own.id = newId;
        own.name = channel->name.empty() ? id : channel->name;
        own.color = channel->color;
        own.output = insertId;
        commands.push_back (AddInsert { own, std::nullopt });
        commands.push_back (UpdateChannel { id, Json { { "insert", newId } } });
        insertId = newId;
    }

    const auto effect = slotEffect (slot);
    const auto hasSlot = insert != nullptr && std::any_of (insert->effects.begin(), insert->effects.end(), [] (auto& e)
    {
        auto* b = std::get_if<BuiltinEffect> (&e);
        return b != nullptr && b->id == slotId && b->kind == BuiltinEffectKind::eq;
    });

    if (hasSlot)
    {
        Json params = Json::object();

        for (auto& [k, v] : effect.params)
            params[k] = v;

        commands.push_back (UpdateEffect { insertId, slotId, Json { { "params", params }, { "bypass", false } } });
    }
    else
    {
        if (insert != nullptr && std::any_of (insert->effects.begin(), insert->effects.end(), [] (auto& e) { return std::visit ([] (auto& x) { return x.id == slotId; }, e); }))
            throw CommandError ("Insert \"" + insertId + "\" has an effect \"slot\" that isn't an EQ: rename or remove it first");

        commands.push_back (AddEffect { insertId, Effect (effect), 0 });
    }

    commands.push_back (UpdateChannel { id, Json { { "part", toString (part) } } });
    return commands;
}

std::optional<ChannelPart> partFromName (const std::string& name)
{
    for (auto p : allParts)
        if (name == toString (p))
            return p;

    return {};
}

bool isLowPart (ChannelPart p)
{
    return p == ChannelPart::kick || p == ChannelPart::rumble || p == ChannelPart::drums || p == ChannelPart::bass;
}

} // namespace gk::mixer
