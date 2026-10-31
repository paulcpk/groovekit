// Draws each plugin's window, after a chord (or a burst) has gone through it so its screens show
// something, as a PNG at twice the size: PluginPictures <folder>.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <filesystem>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE gkCreate_triad();
juce::AudioProcessor* JUCE_CALLTYPE gkCreate_quartet();
juce::AudioProcessor* JUCE_CALLTYPE gkCreate_sextet();
juce::AudioProcessor* JUCE_CALLTYPE gkCreate_kessel();
juce::AudioProcessor* JUCE_CALLTYPE gkCreate_chamber();
juce::AudioProcessor* JUCE_CALLTYPE gkCreate_echo();

int main (int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: PluginPictures <folder>\n";
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juce;
    const std::filesystem::path folder = argv[1];
    std::filesystem::create_directories (folder);
    // Each with what it plays for its picture: a chord for the synths, a kick for Kessel.
    const std::vector<int> chord { 48, 55, 60, 63 }, kick { 60 };
    const std::tuple<const char*, juce::AudioProcessor* (*)(), std::vector<int>> plugins[] {
        { "Triad", gkCreate_triad, chord }, { "Quartet", gkCreate_quartet, chord }, { "Sextet", gkCreate_sextet, chord }, { "Kessel", gkCreate_kessel, kick },
        { "Chamber", gkCreate_chamber, {} }, { "Echo", gkCreate_echo, {} },
    };

    for (auto& [name, create, notes] : plugins)
    {
        std::unique_ptr<juce::AudioProcessor> processor (create());
        const bool synth = processor->acceptsMidi();
        processor->setPlayConfigDetails (synth ? 0 : 2, 2, 48000, 512);
        processor->prepareToPlay (48000, 512);

        if (processor->getNumPrograms() > 1)
            processor->setCurrentProgram (1);

        juce::AudioBuffer<float> buffer (2, 512);

        for (int block = 0; block < 16; ++block)
        {
            juce::MidiBuffer midi;
            buffer.clear();

            if (block == 0)
            {
                if (synth)
                    for (auto note : notes)
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
                else
                    for (int i = 0; i < 400; ++i)
                        for (int ch = 0; ch < 2; ++ch)
                            buffer.setSample (ch, i, 0.5f * std::sin (i * 0.25f) * (1 - i / 400.0f));
            }

            processor->processBlock (buffer, midi);
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());

        if (editor == nullptr)
            continue;

        // A frame of the editor's timer, so scopes and lights show what played.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 2.0f);
        juce::File file ((folder / (std::string (name) + ".png")).string());
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;

        if (out.openedOk() && png.writeImageToStream (image, out))
            std::cout << file.getFullPathName() << " " << image.getWidth() << "x" << image.getHeight() << "\n";

        editor.reset();
        processor->releaseResources();
    }

    return 0;
}
