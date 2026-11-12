#include "PresetManager.h"
#include "Params.h"

namespace
{
    const juce::String presetExt { ".mcdpreset" };
    const juce::Identifier presetNameId { "presetName" };

    // Values per knob in on-screen order, then RELEASE AUTO (1/0).
    //                       IN  SUBMONO TIGHT K/B PUNCH SNAP MUD REESE HARSH AIR  WIDTH GLUE ATT  REL  SAT  CLIP LOUD  AGAIN CEIL  FIX  MIX OUT  AUTO
    struct Factory { const char* name; std::array<float, 23> v; };
    const std::array<Factory, 8> factoryPresets {{
        { "Default",              {{ 0, 100, 20, 20, 20, 15, 25, 20, 25, 20, 100,  25, 10, 150, 15, 30,  -7.0f, 100, -1.0f, 100, 100, 0, 1 }} },
        { "Liquid Polish",        {{ 0,  90, 25, 20, 15, 15, 30, 20, 35, 40, 115,  30, 20, 200, 20, 15,  -8.0f, 100, -1.0f,  85, 100, 0, 1  }} },
        { "Neuro Smash",          {{ 0, 120, 45, 40, 45, 35, 40, 55, 45, 25, 110,  45,  5, 100, 40, 60,  -4.5f, 100, -0.3f, 100, 100, 0, 1  }} },
        { "Jump Up Bounce",       {{ 0, 110, 35, 55, 50, 30, 35, 40, 35, 25, 105,  40,  8, 120, 30, 50,  -5.0f, 100, -0.3f, 100, 100, 0, 0  }} },
        { "Rollers Sub Fix",      {{ 0, 140, 50, 45, 25, 20, 40, 25, 25, 20, 100,  30, 15, 160, 15, 35,  -6.0f, 100, -0.5f, 100, 100, 0, 1  }} },
        { "Jungle Breaks",        {{ 0, 100, 20, 25, 55, 50, 30, 20, 30, 30, 110,  35, 25, 180, 35, 40,  -6.5f, 100, -0.5f,  90, 100, 0, 0  }} },
        { "Club Loud",            {{ 0, 120, 40, 35, 40, 30, 35, 40, 40, 30, 105,  50, 10, 120, 35, 70,  -3.5f, 100, -0.3f, 100, 100, 0, 1  }} },
        { "Gentle Master Assist", {{ 0,  80, 10, 10, 10, 10, 15, 10, 15, 10, 100,  15, 30, 250,  5,  0, -10.0f, 100, -1.0f,  60, 100, 0, 1  }} },
    }};

    void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    loadFavourites();
    refresh();
}

juce::File PresetManager::getUserFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Mod Con DSP").getChildFile ("Dr Mix").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::StringArray PresetManager::getFactoryNames()
{
    juce::StringArray names;
    for (auto& f : factoryPresets) names.add (f.name);
    return names;
}

void PresetManager::refresh()
{
    entries.clear();
    for (auto& f : factoryPresets) entries.push_back ({ f.name, true, {} });

    auto files = getUserFolder().findChildFiles (juce::File::findFiles, false, "*" + presetExt);
    files.sort();
    for (auto& f : files) entries.push_back ({ f.getFileNameWithoutExtension(), false, f });
}

int PresetManager::indexOf (const juce::String& name) const
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == name) return (int) i;
    return -1;
}

void PresetManager::applyFactory (const juce::String& name)
{
    for (auto& f : factoryPresets)
    {
        if (name != f.name) continue;
        for (int k = 0; k < params::knobOrder.size(); ++k)
            setParam (apvts, params::knobOrder[k], f.v[(size_t) k]);
        setParam (apvts, params::releaseAuto, f.v[(size_t) params::knobOrder.size()]);
        return;
    }
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= (int) entries.size()) return false;
    const auto& e = entries[(size_t) index];

    // every preset starts from a flat ADVANCED EQ (factory presets and older user presets don't store one)
    for (int i = 0; i < params::numEqBands; ++i) setParam (apvts, params::eqBand (i), 0.0f);
    setParam (apvts, params::eqMix, 100.0f);
    setParam (apvts, params::eqOn, 1.0f);

    if (e.factory)
        applyFactory (e.name);
    else
    {
        auto xml = juce::XmlDocument::parse (e.file);
        if (xml == nullptr) return false;
        for (auto* child : xml->getChildWithTagNameIterator ("PARAM"))
        {
            const auto id = child->getStringAttribute ("id");
            if (id == params::bypass || id == params::oversample) continue;   // not part of a preset
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
                setParam (apvts, id, (float) child->getDoubleAttribute ("value", p->convertFrom0to1 (p->getDefaultValue())));
        }
    }
    setCurrentName (e.name);
    return true;
}

void PresetManager::loadNext (int delta)
{
    if (entries.empty()) return;
    const int n = (int) entries.size();
    int idx = indexOf (getCurrentName());
    idx = idx < 0 ? 0 : ((idx + delta) % n + n) % n;
    load (idx);
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty() || getFactoryNames().contains (name)) return false;

    juce::XmlElement xml ("DrMixPreset");
    xml.setAttribute ("version", params::version);
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (rp->getParameterID() == params::bypass || rp->getParameterID() == params::oversample) continue;
            auto* c = xml.createNewChildElement ("PARAM");
            c->setAttribute ("id", rp->getParameterID());
            c->setAttribute ("value", rp->convertFrom0to1 (rp->getValue()));
        }

    const bool ok = xml.writeTo (getUserFolder().getChildFile (name + presetExt));
    refresh();
    if (ok) setCurrentName (name);
    return ok;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    const int i = indexOf (name);
    if (i < 0 || entries[(size_t) i].factory) return false;
    const bool ok = entries[(size_t) i].file.moveToTrash();
    refresh();
    return ok;
}

juce::String PresetManager::getCurrentName() const
{
    return apvts.state.getProperty (presetNameId, "Default").toString();
}

void PresetManager::setCurrentName (const juce::String& n)
{
    apvts.state.setProperty (presetNameId, n, nullptr);
}

void PresetManager::toggleFavourite (const juce::String& name)
{
    if (favourites.contains (name)) favourites.removeString (name);
    else favourites.add (name);
    saveFavourites();
}

void PresetManager::loadFavourites()
{
    favourites.clear();
    auto f = getUserFolder().getParentDirectory().getChildFile ("favourites.txt");
    if (f.existsAsFile())
    {
        favourites.addLines (f.loadFileAsString());
        favourites.removeEmptyStrings();
    }
}

void PresetManager::saveFavourites() const
{
    getUserFolder().getParentDirectory().getChildFile ("favourites.txt").replaceWithText (favourites.joinIntoString ("\n"));
}
