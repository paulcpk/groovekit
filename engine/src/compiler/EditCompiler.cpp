#include "EditCompiler.h"

#include "effects/BuiltinEffects.h"
#include "mixer/FaderPlugin.h"
#include "render/Stretch.h"
#include "sampler/SamplerPlugin.h"

namespace gk
{

namespace te = tracktion;
using namespace model;

namespace
{
    /** A parameter an automation clip can drive, with the conversion from model units. */
    struct AutomationTargetParam
    {
        te::AutomatableParameter::Ptr param;
        std::function<float (double)> toNative;
        double min, max;
    };

    /** Point times of different clips that land on the same instant are separated by this much, to make steps. */
    constexpr double stepSeconds = 1.0e-6;

    class Compiler
    {
    public:
        Compiler (te::Engine& e, const Project& p, const CompileOptions& o)
            : engine (e), project (p), options (o) {}

        CompiledEdit run()
        {
            validateOrThrow (project);
            createEdit();
            createInserts();
            createChannels();
            createPatternClips();
            createAudioClips();
            createAutomation();
            return std::move (result);
        }

    private:
        te::Engine& engine;
        const Project& project;
        const CompileOptions& options;
        CompiledEdit result;
        te::Edit* edit = nullptr;

        std::map<Id, mixer::FaderPlugin*> channelFaders, insertFaders;
        std::map<Id, std::map<Id, te::AuxSendPlugin*>> sendPlugins;
        std::map<Id, std::map<Id, std::pair<te::Plugin*, BuiltinEffectKind>>> effectPlugins;
        std::map<Id, te::AudioTrack*> audioClipTracks;

        //==============================================================================
        te::TimePosition toTime (Ticks ticks) const
        {
            return edit->tempoSequence.toTime (te::BeatPosition::fromBeats (static_cast<double> (ticks) / ticksPerQuarter));
        }

        static te::BeatPosition toBeats (Ticks ticks)
        {
            return te::BeatPosition::fromBeats (static_cast<double> (ticks) / ticksPerQuarter);
        }

        std::filesystem::path resolve (const std::string& path) const
        {
            auto p = std::filesystem::path (path);
            return p.is_relative() ? options.projectDir / p : p;
        }

        bool isTrackMuted (const Id& trackId) const
        {
            auto track = findTrack (project, trackId);
            return track != nullptr && track->mute;
        }

        te::AudioTrack& addTrack (const juce::String& name)
        {
            auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr, false);

            if (track == nullptr)
                throw std::runtime_error ("Couldn't create a track");

            track->setName (name);
            return *track;
        }

        template <typename PluginType>
        PluginType& addPlugin (te::AudioTrack& track)
        {
            auto plugin = edit->getPluginCache().createNewPlugin (PluginType::xmlTypeName, {});
            auto typed = dynamic_cast<PluginType*> (plugin.get());

            if (typed == nullptr)
                throw std::logic_error (std::string ("Couldn't create plugin ") + PluginType::xmlTypeName);

            track.pluginList.insertPlugin (plugin, -1, nullptr);
            return *typed;
        }

        mixer::FaderPlugin& addFader (te::AudioTrack& track, double volumeDb, double pan)
        {
            auto& fader = addPlugin<mixer::FaderPlugin> (track);
            fader.setVolumeDb (static_cast<float> (volumeDb));
            fader.setPan (static_cast<float> (pan));
            return fader;
        }

        //==============================================================================
        void createEdit()
        {
            te::Edit::Options editOptions { engine, te::createEmptyEdit (engine), te::ProjectItemID::createNewID (0) };
            editOptions.role = options.role;
            editOptions.numAudioTracks = 0;
            editOptions.defaultMasterVolumedB = 0.0f;

            result.edit = te::Edit::createEdit (std::move (editOptions));
            edit = result.edit.get();

            edit->tempoSequence.getTempo (0)->setBpm (project.tempo);

            auto timeSig = edit->tempoSequence.getTimeSig (0);
            timeSig->numerator = project.timeSignature.numerator;
            timeSig->denominator = project.timeSignature.denominator;
        }

        /** Insert tracks: [aux return] effects, pre-fader sends, fader, post-fader sends. */
        void createInserts()
        {
            std::map<Id, int> returnBus;

            for (auto& insert : project.mixer.inserts)
                for (auto& send : insert.sends)
                    if (! returnBus.contains (send.target))
                        returnBus.emplace (send.target, static_cast<int> (returnBus.size()));

            for (auto& insert : project.mixer.inserts)
            {
                auto& track = addTrack ("insert:" + insert.id);
                result.insertTracks[insert.id] = &track;

                if (auto bus = returnBus.find (insert.id); bus != returnBus.end())
                    addPlugin<te::AuxReturnPlugin> (track).busNumber = bus->second;

                for (auto& effect : insert.effects)
                {
                    if (auto builtin = std::get_if<BuiltinEffect> (&effect))
                    {
                        auto plugin = effects::createEffect (*edit, *builtin);
                        track.pluginList.insertPlugin (plugin, -1, nullptr);
                        effectPlugins[insert.id][builtin->id] = { plugin.get(), builtin->kind };
                    }
                    else
                    {
                        throw std::runtime_error ("Insert \"" + insert.id + "\": plugin effects aren't supported yet");
                    }
                }

                auto addSends = [&] (bool preFader)
                {
                    for (auto& send : insert.sends)
                    {
                        if (send.preFader != preFader)
                            continue;

                        auto& aux = addPlugin<te::AuxSendPlugin> (track);
                        aux.busNumber = returnBus.at (send.target);
                        aux.setGainDb (static_cast<float> (send.levelDb));

                        // Only mute: setMute (false) would reset the level to 0 dB.
                        if (insert.mute)
                            aux.setMute (true);

                        sendPlugins[insert.id][send.id] = &aux;
                    }
                };

                addSends (true);
                insertFaders[insert.id] = &addFader (track, insert.volumeDb, insert.pan);
                addSends (false);

                track.setMute (insert.mute);
            }

            for (auto& insert : project.mixer.inserts)
                if (insert.id != masterInsertId)
                    result.insertTracks.at (insert.id)->getOutput().setOutputToTrack (result.insertTracks.at (insert.output));
        }

        void createChannels()
        {
            for (auto& channel : project.channels)
            {
                auto& track = addTrack ("channel:" + channel.id);
                result.channelTracks[channel.id] = &track;

                if (auto sampler = std::get_if<SamplerInstrument> (&channel.instrument))
                {
                    try
                    {
                        addPlugin<sampler::SamplerPlugin> (track).setInstrument (*sampler, options.projectDir);
                    }
                    catch (const std::exception& e)
                    {
                        throw std::runtime_error ("Channel \"" + channel.id + "\": " + e.what());
                    }
                }
                else
                {
                    throw std::runtime_error ("Channel \"" + channel.id + "\": plugin instruments aren't supported yet");
                }

                channelFaders[channel.id] = &addFader (track, channel.volumeDb, channel.pan);
                track.setMute (channel.mute);
                track.getOutput().setOutputToTrack (result.insertTracks.at (channel.insert));
            }
        }

        //==============================================================================
        /** One MIDI clip per channel with notes in the pattern; the pattern repeats to fill the clip. */
        void createPatternClips()
        {
            for (auto& clip : project.playlist.clips)
            {
                auto pc = std::get_if<PatternClip> (&clip);

                if (pc == nullptr)
                    continue;

                auto& pattern = *findPattern (project, pc->pattern);
                const auto loopLength = getPatternLength (pattern, project.timeSignature);
                const bool muted = pc->mute || isTrackMuted (pc->track);

                std::map<Id, std::vector<const Note*>> notesByChannel;

                for (auto& note : pattern.notes)
                    notesByChannel[note.channel].push_back (&note);

                for (auto& [channelId, notes] : notesByChannel)
                {
                    auto& track = *result.channelTracks.at (channelId);
                    auto midiClip = track.insertMIDIClip (pc->id + ":" + channelId,
                                                          { toTime (pc->start), toTime (pc->start + pc->length) },
                                                          nullptr);

                    if (midiClip == nullptr)
                        throw std::runtime_error ("Couldn't create a MIDI clip for \"" + pc->id + "\"");

                    auto& sequence = midiClip->getSequence();

                    for (Ticks repeat = 0; repeat * loopLength - pc->offset < pc->length; ++repeat)
                    {
                        const auto base = repeat * loopLength - pc->offset;

                        for (auto note : notes)
                        {
                            const auto start = base + note->start;

                            if (start < 0 || start >= pc->length)
                                continue;

                            const auto length = std::min (note->length, pc->length - start);
                            sequence.addNote (note->key, toBeats (start),
                                              te::BeatDuration::fromBeats (static_cast<double> (length) / ticksPerQuarter),
                                              note->velocity, 0, nullptr);
                        }
                    }

                    midiClip->setMuted (muted);
                }
            }
        }

        te::AudioTrack& getAudioClipTrack (const Id& insertId)
        {
            if (auto it = audioClipTracks.find (insertId); it != audioClipTracks.end())
                return *it->second;

            auto& track = addTrack ("audio:" + insertId);
            track.getOutput().setOutputToTrack (result.insertTracks.at (insertId));
            audioClipTracks[insertId] = &track;
            return track;
        }

        void createAudioClips()
        {
            for (auto& clip : project.playlist.clips)
            {
                auto ac = std::get_if<AudioClip> (&clip);

                if (ac == nullptr)
                    continue;

                auto file = juce::File (resolve (ac->sample).string());

                if (! file.existsAsFile())
                    throw std::runtime_error ("Audio clip \"" + ac->id + "\": file not found: " + file.getFullPathName().toStdString());

                auto& track = getAudioClipTrack (ac->insert);

                // A clip recorded at another tempo plays a stretched copy, pitch kept.
                const auto speed = ac->sourceTempo ? project.tempo / *ac->sourceTempo : 1.0;

                if (std::abs (speed - 1.0) > 1.0e-6)
                    file = juce::File (getStretchedFile (file.getFullPathName().toStdString(), speed, options.cacheDir).string());

                te::ClipPosition position { { toTime (ac->start), toTime (ac->start + ac->length) },
                                            te::TimeDuration::fromSeconds (ac->offset / speed) };

                auto waveClip = track.insertWaveClip (ac->id, file, position, false);

                if (waveClip == nullptr)
                    throw std::runtime_error ("Couldn't create audio clip \"" + ac->id + "\"");

                // Tracktion guesses a tempo from names like "Loop 120bpm" and then syncs and resizes the
                // clip on its own (and offline renders wait forever for the proxy). The model decides.
                waveClip->setAutoTempo (false);
                waveClip->setAutoPitch (false);
                waveClip->setTimeStretchMode (te::TimeStretcher::disabled);
                waveClip->setPosition (position);

                waveClip->setGainDB (static_cast<float> (ac->gainDb));
                waveClip->setPan (static_cast<float> (ac->pan));
                waveClip->setFadeIn (te::TimeDuration::fromSeconds (ac->fadeIn));
                waveClip->setFadeOut (te::TimeDuration::fromSeconds (ac->fadeOut));
                waveClip->setMuted (ac->mute || isTrackMuted (ac->track));
            }
        }

        //==============================================================================
        AutomationTargetParam findTarget (const AutomationTarget& target)
        {
            auto faderParam = [&] (mixer::FaderPlugin& fader) -> AutomationTargetParam
            {
                if (target.param == "volume")
                    return { fader.volumeParam, [] (double v) { return static_cast<float> (v); }, mixer::FaderPlugin::minDb, mixer::FaderPlugin::maxDb };

                return { fader.panParam, [] (double v) { return static_cast<float> (v); }, -1, 1 };
            };

            switch (target.scope)
            {
                case AutomationTargetScope::channel:
                    return faderParam (*channelFaders.at (*target.channel));

                case AutomationTargetScope::insert:
                    return faderParam (*insertFaders.at (*target.insert));

                case AutomationTargetScope::send:
                {
                    auto send = sendPlugins.at (*target.insert).at (*target.send);
                    return { send->gain,
                             [] (double db) { return te::decibelsToVolumeFaderPosition (static_cast<float> (db)); },
                             -100, 6 };
                }

                case AutomationTargetScope::effect:
                {
                    auto [plugin, kind] = effectPlugins.at (*target.insert).at (*target.effect);
                    auto [param, info] = effects::findAutomatableParam (*plugin, kind, target.param);
                    return { param, [convert = info->toNative] (double v) { auto f = static_cast<float> (v); return convert != nullptr ? convert (f) : f; },
                             info->min, info->max };
                }
            }

            throw std::logic_error ("Unknown automation scope");
        }

        void createAutomation()
        {
            struct Point { double seconds; float value; float curve; };
            std::map<std::string, std::pair<AutomationTargetParam, std::vector<std::vector<Point>>>> curves;

            for (auto& clip : project.playlist.clips)
            {
                auto ac = std::get_if<AutomationClip> (&clip);

                if (ac == nullptr || ac->mute || isTrackMuted (ac->track))
                    continue;

                const auto key = toJson (ac->target).dump();

                if (! curves.contains (key))
                {
                    try
                    {
                        curves.emplace (key, std::make_pair (findTarget (ac->target), std::vector<std::vector<Point>>()));
                    }
                    catch (const std::exception& e)
                    {
                        throw std::runtime_error ("Automation clip \"" + ac->id + "\": " + e.what());
                    }
                }

                auto& [target, clips] = curves.at (key);
                auto& points = clips.emplace_back();

                for (auto& p : ac->points)
                {
                    if (p.time >= ac->length)
                        continue;

                    if (p.value < target.min || p.value > target.max)
                        throw std::runtime_error ("Automation clip \"" + ac->id + "\": value " + std::to_string (p.value)
                                                  + " is outside " + std::to_string (target.min) + ".." + std::to_string (target.max));

                    points.push_back ({ toTime (ac->start + p.time).inSeconds(), target.toNative (p.value), static_cast<float> (p.curve) });
                }
            }

            for (auto& [key, entry] : curves)
            {
                auto& [target, clips] = entry;

                std::stable_sort (clips.begin(), clips.end(), [] (auto& a, auto& b)
                {
                    return ! b.empty() && (a.empty() || a.front().seconds < b.front().seconds);
                });

                auto& curve = target.param->getCurve();
                std::optional<Point> previous;

                for (auto& points : clips)
                {
                    if (points.empty())
                        continue;

                    // Hold the previous clip's last value until this clip starts.
                    if (previous && points.front().seconds > previous->seconds + stepSeconds)
                        curve.addPoint (te::TimePosition::fromSeconds (points.front().seconds - stepSeconds), previous->value, 0.0f);

                    for (auto& p : points)
                        curve.addPoint (te::TimePosition::fromSeconds (p.seconds), p.value, p.curve);

                    previous = points.back();
                }

                // Tracktion normally rebuilds the playback stream on a timer; offline renders don't run the message loop.
                target.param->updateStream();
            }
        }
    };
}

CompiledEdit compileProject (te::Engine& engine, const Project& project, const CompileOptions& options)
{
    auto withDefaults = options;

    if (withDefaults.cacheDir.empty())
        withDefaults.cacheDir = getDefaultCacheDir();

    return Compiler (engine, project, withDefaults).run();
}

} // namespace gk
