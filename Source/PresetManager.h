#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/** Factory + user presets, favourites and the current-preset name. Message thread only. */
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    struct Entry { juce::String name; bool factory = true; juce::File file; };

    void refresh();                                       // rescan the user preset folder
    const std::vector<Entry>& getAll() const noexcept     { return entries; }
    int indexOf (const juce::String& name) const;

    bool load (int index);
    bool load (const juce::String& name)                  { return load (indexOf (name)); }
    void loadNext (int delta);
    bool saveUser (const juce::String& name);
    bool deleteUser (const juce::String& name);

    juce::String getCurrentName() const;
    void setCurrentName (const juce::String&);

    bool isFavourite (const juce::String& name) const     { return favourites.contains (name); }
    void toggleFavourite (const juce::String& name);

    static juce::File getUserFolder();
    static juce::StringArray getFactoryNames();

private:
    void loadFavourites();
    void saveFavourites() const;
    void applyFactory (const juce::String& name);

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Entry> entries;
    juce::StringArray favourites;
};
