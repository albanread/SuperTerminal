//
//  SynthInstrumentMap.h
//  SuperTerminal - Synthesized Instrument Mapping
//
//  Maps General MIDI instrument numbers 200+ to synthesized instruments
//  for use with ABC notation without protocol changes.
//
//  Created by Assistant on 2024-11-19.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace SuperTerminal {

// Synthesized instrument types
enum class SynthType {
    ADDITIVE,
    FM,
    GRANULAR,
    PHYSICAL_STRING,
    PHYSICAL_BAR,
    PHYSICAL_TUBE,
    PHYSICAL_DRUM
};

// Parameters for different synthesis types
struct SynthParams {
    SynthType type;
    std::string name;
    std::string description;
    
    // Common parameters
    float attack = 0.01f;
    float decay = 0.1f;
    float sustain = 0.7f;
    float release = 0.3f;
    
    // Additive synthesis parameters
    std::vector<float> harmonics;
    
    // FM synthesis parameters
    float carrierRatio = 1.0f;
    float modulatorRatio = 2.0f;
    float modIndex = 1.0f;
    
    // Granular synthesis parameters
    float grainSize = 0.05f;
    float overlap = 0.5f;
    float density = 1.0f;
    
    // Physical modeling parameters
    float damping = 0.1f;
    float brightness = 0.5f;
    float stiffness = 0.5f;
    float airPressure = 0.8f;  // For tube instruments
    float excitation = 1.0f;   // For drums
    
    SynthParams(SynthType t = SynthType::ADDITIVE, const std::string& n = "Synth", const std::string& desc = "")
        : type(t), name(n), description(desc) {
        // Default harmonic series for additive synthesis
        harmonics = {1.0f, 0.5f, 0.33f, 0.25f, 0.2f, 0.17f, 0.14f, 0.125f};
    }
};

// Synthesized instrument registry
class SynthInstrumentMap {
public:
    static SynthInstrumentMap& getInstance() {
        static SynthInstrumentMap instance;
        return instance;
    }
    
    // Check if instrument number is synthesized (200+)
    static bool isSynthInstrument(int instrumentNumber) {
        return instrumentNumber >= 200;
    }
    
    // Get synth parameters for instrument number
    const SynthParams* getSynthParams(int instrumentNumber) const {
        auto it = instrumentMap.find(instrumentNumber);
        return (it != instrumentMap.end()) ? &it->second : nullptr;
    }
    
    // Get all available synth instruments
    std::vector<std::pair<int, std::string>> getAvailableInstruments() const {
        std::vector<std::pair<int, std::string>> result;
        for (const auto& pair : instrumentMap) {
            result.emplace_back(pair.first, pair.second.name);
        }
        return result;
    }
    
    // Register custom synth instrument
    void registerInstrument(int instrumentNumber, const SynthParams& params) {
        if (instrumentNumber >= 200) {
            instrumentMap[instrumentNumber] = params;
        }
    }
    
private:
    std::unordered_map<int, SynthParams> instrumentMap;
    
    SynthInstrumentMap() {
        initializeDefaultInstruments();
    }
    
    void initializeDefaultInstruments() {
        // ADDITIVE SYNTHESIS INSTRUMENTS (200-219)
        
        // 200: Pure Sine
        SynthParams pureSine(SynthType::ADDITIVE, "Pure Sine", "Clean sine wave tone");
        pureSine.harmonics = {1.0f, 0.0f}; // Add second harmonic as 0 to ensure array size > 1
        pureSine.attack = 0.01f;
        pureSine.decay = 0.1f;
        pureSine.sustain = 0.9f;
        pureSine.release = 0.2f;
        instrumentMap[200] = pureSine;
        
        // 201: Harmonic Series
        SynthParams harmonicSeries(SynthType::ADDITIVE, "Harmonic Series", "Natural harmonic overtones");
        harmonicSeries.harmonics = {1.0f, 0.5f, 0.33f, 0.25f, 0.2f, 0.17f, 0.14f, 0.125f};
        instrumentMap[201] = harmonicSeries;
        
        // 202: Bright Organ
        SynthParams brightOrgan(SynthType::ADDITIVE, "Bright Organ", "Organ-like with odd harmonics");
        brightOrgan.harmonics = {1.0f, 0.0f, 0.6f, 0.0f, 0.4f, 0.0f, 0.3f, 0.0f, 0.2f};
        brightOrgan.attack = 0.02f;
        brightOrgan.sustain = 0.9f;
        brightOrgan.release = 0.2f;
        instrumentMap[202] = brightOrgan;
        
        // 203: Soft Pad
        SynthParams softPad(SynthType::ADDITIVE, "Soft Pad", "Gentle pad with slow attack");
        softPad.harmonics = {1.0f, 0.3f, 0.1f, 0.05f};
        softPad.attack = 0.5f;
        softPad.decay = 0.2f;
        softPad.sustain = 0.8f;
        softPad.release = 1.0f;
        instrumentMap[203] = softPad;
        
        // 204: Bell-like
        SynthParams bellLike(SynthType::ADDITIVE, "Bell-like", "Metallic bell harmonics");
        bellLike.harmonics = {1.0f, 0.67f, 0.4f, 0.18f, 0.12f, 0.09f};
        bellLike.attack = 0.01f;
        bellLike.decay = 0.3f;
        bellLike.sustain = 0.3f;
        bellLike.release = 2.0f;
        instrumentMap[204] = bellLike;
        
        // FM SYNTHESIS INSTRUMENTS (220-239)
        
        // 220: FM Bass
        SynthParams fmBass(SynthType::FM, "FM Bass", "Deep FM bass");
        fmBass.carrierRatio = 1.0f;
        fmBass.modulatorRatio = 1.0f;
        fmBass.modIndex = 0.3f;
        fmBass.attack = 0.01f;
        fmBass.decay = 0.1f;
        fmBass.sustain = 0.7f;
        fmBass.release = 0.2f;
        instrumentMap[220] = fmBass;
        
        // 221: FM Lead
        SynthParams fmLead(SynthType::FM, "FM Lead", "Bright FM lead");
        fmLead.carrierRatio = 1.0f;
        fmLead.modulatorRatio = 1.5f;
        fmLead.modIndex = 0.3f;
        fmLead.attack = 0.02f;
        fmLead.decay = 0.05f;
        fmLead.sustain = 0.8f;
        fmLead.release = 0.1f;
        instrumentMap[221] = fmLead;
        
        // 222: FM Bell
        SynthParams fmBell(SynthType::FM, "FM Bell", "Bell-like FM tone");
        fmBell.carrierRatio = 1.0f;
        fmBell.modulatorRatio = 1.2f;
        fmBell.modIndex = 0.4f;
        fmBell.attack = 0.01f;
        fmBell.decay = 0.2f;
        fmBell.sustain = 0.4f;
        fmBell.release = 1.5f;
        instrumentMap[222] = fmBell;
        
        // 223: FM Brass
        SynthParams fmBrass(SynthType::FM, "FM Brass", "Brass-like FM");
        fmBrass.carrierRatio = 1.0f;
        fmBrass.modulatorRatio = 1.3f;
        fmBrass.modIndex = 0.2f;
        fmBrass.attack = 0.05f;
        fmBrass.decay = 0.1f;
        fmBrass.sustain = 0.9f;
        fmBrass.release = 0.3f;
        instrumentMap[223] = fmBrass;
        
        // GRANULAR SYNTHESIS INSTRUMENTS (240-259)
        
        // 240: Granular Pad
        SynthParams granularPad(SynthType::GRANULAR, "Granular Pad", "Textured granular pad");
        granularPad.grainSize = 0.1f;
        granularPad.overlap = 0.7f;
        granularPad.density = 0.8f;
        granularPad.attack = 0.3f;
        granularPad.sustain = 0.8f;
        granularPad.release = 0.8f;
        instrumentMap[240] = granularPad;
        
        // 241: Granular Texture
        SynthParams granularTexture(SynthType::GRANULAR, "Granular Texture", "Dense granular texture");
        granularTexture.grainSize = 0.02f;
        granularTexture.overlap = 0.5f;
        granularTexture.density = 1.5f;
        granularTexture.attack = 0.1f;
        granularTexture.sustain = 0.7f;
        granularTexture.release = 0.5f;
        instrumentMap[241] = granularTexture;
        
        // PHYSICAL MODELING INSTRUMENTS (260-299)
        
        // 260: Physical String (Guitar-like)
        SynthParams physicalGuitar(SynthType::PHYSICAL_STRING, "Physical Guitar", "Guitar string model");
        physicalGuitar.damping = 0.05f;
        physicalGuitar.brightness = 0.7f;
        physicalGuitar.stiffness = 0.3f;
        physicalGuitar.attack = 0.01f;
        physicalGuitar.decay = 0.2f;
        physicalGuitar.sustain = 0.4f;
        physicalGuitar.release = 0.8f;
        instrumentMap[260] = physicalGuitar;
        
        // 261: Physical String (Violin-like)
        SynthParams physicalViolin(SynthType::PHYSICAL_STRING, "Physical Violin", "Violin string model");
        physicalViolin.damping = 0.02f;
        physicalViolin.brightness = 0.8f;
        physicalViolin.stiffness = 0.2f;
        physicalViolin.attack = 0.05f;
        physicalViolin.decay = 0.1f;
        physicalViolin.sustain = 0.8f;
        physicalViolin.release = 0.3f;
        instrumentMap[261] = physicalViolin;
        
        // 270: Physical Bar (Bell)
        SynthParams physicalBell(SynthType::PHYSICAL_BAR, "Physical Bell", "Metal bar bell model");
        physicalBell.damping = 0.01f;
        physicalBell.brightness = 0.9f;
        physicalBell.stiffness = 0.8f;
        physicalBell.attack = 0.01f;
        physicalBell.decay = 0.3f;
        physicalBell.sustain = 0.2f;
        physicalBell.release = 3.0f;
        instrumentMap[270] = physicalBell;
        
        // 271: Physical Bar (Marimba)
        SynthParams physicalMarimba(SynthType::PHYSICAL_BAR, "Physical Marimba", "Wooden bar marimba model");
        physicalMarimba.damping = 0.08f;
        physicalMarimba.brightness = 0.6f;
        physicalMarimba.stiffness = 0.4f;
        physicalMarimba.attack = 0.01f;
        physicalMarimba.decay = 0.2f;
        physicalMarimba.sustain = 0.1f;
        physicalMarimba.release = 1.0f;
        instrumentMap[271] = physicalMarimba;
        
        // 280: Physical Tube (Flute)
        SynthParams physicalFlute(SynthType::PHYSICAL_TUBE, "Physical Flute", "Flute tube model");
        physicalFlute.airPressure = 0.6f;
        physicalFlute.brightness = 0.7f;
        physicalFlute.damping = 0.05f;
        physicalFlute.attack = 0.1f;
        physicalFlute.decay = 0.05f;
        physicalFlute.sustain = 0.8f;
        physicalFlute.release = 0.2f;
        instrumentMap[280] = physicalFlute;
        
        // 281: Physical Tube (Brass)
        SynthParams physicalBrass(SynthType::PHYSICAL_TUBE, "Physical Brass", "Brass tube model");
        physicalBrass.airPressure = 0.9f;
        physicalBrass.brightness = 0.8f;
        physicalBrass.damping = 0.03f;
        physicalBrass.attack = 0.08f;
        physicalBrass.decay = 0.1f;
        physicalBrass.sustain = 0.9f;
        physicalBrass.release = 0.4f;
        instrumentMap[281] = physicalBrass;
        
        // 290: Physical Drum (Kick)
        SynthParams physicalKick(SynthType::PHYSICAL_DRUM, "Physical Kick", "Kick drum model");
        physicalKick.excitation = 1.2f;
        physicalKick.damping = 0.3f;
        physicalKick.brightness = 0.3f;
        physicalKick.attack = 0.01f;
        physicalKick.decay = 0.15f;
        physicalKick.sustain = 0.1f;
        physicalKick.release = 0.3f;
        instrumentMap[290] = physicalKick;
        
        // 291: Physical Drum (Snare)
        SynthParams physicalSnare(SynthType::PHYSICAL_DRUM, "Physical Snare", "Snare drum model");
        physicalSnare.excitation = 1.0f;
        physicalSnare.damping = 0.2f;
        physicalSnare.brightness = 0.8f;
        physicalSnare.attack = 0.01f;
        physicalSnare.decay = 0.08f;
        physicalSnare.sustain = 0.1f;
        physicalSnare.release = 0.15f;
        instrumentMap[291] = physicalSnare;
        
        // 292: Physical Drum (Tom)
        SynthParams physicalTom(SynthType::PHYSICAL_DRUM, "Physical Tom", "Tom drum model");
        physicalTom.excitation = 0.9f;
        physicalTom.damping = 0.15f;
        physicalTom.brightness = 0.5f;
        physicalTom.attack = 0.01f;
        physicalTom.decay = 0.12f;
        physicalTom.sustain = 0.2f;
        physicalTom.release = 0.4f;
        instrumentMap[292] = physicalTom;
    }
};

// Utility functions
inline float midiNoteToFrequency(int midiNote) {
    return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
}

inline std::string synthTypeToString(SynthType type) {
    switch (type) {
        case SynthType::ADDITIVE: return "Additive";
        case SynthType::FM: return "FM";
        case SynthType::GRANULAR: return "Granular";
        case SynthType::PHYSICAL_STRING: return "Physical String";
        case SynthType::PHYSICAL_BAR: return "Physical Bar";
        case SynthType::PHYSICAL_TUBE: return "Physical Tube";
        case SynthType::PHYSICAL_DRUM: return "Physical Drum";
        default: return "Unknown";
    }
}

} // namespace SuperTerminal