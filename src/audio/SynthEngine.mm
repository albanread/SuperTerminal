//
//  SynthEngine.mm
//  SuperTerminal Framework - Sound Synthesis Engine Implementation
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "SynthEngine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <chrono>

// Global instance
std::unique_ptr<SynthEngine> g_synthEngine = nullptr;

// Global sound effects registry
static std::unordered_map<uint32_t, EffectsParams> g_soundEffects;
static std::mutex g_soundEffectsMutex;

// EnvelopeADSR Implementation

float EnvelopeADSR::getValue(float time, float noteDuration) const {
    if (time < 0.0f) return 0.0f;

    float totalTime = attackTime + decayTime + releaseTime;
    float sustainTime = std::max(0.0f, noteDuration - totalTime);

    if (time <= attackTime) {
        // Attack phase - rise from 0 to 1
        return time / attackTime;
    }

    time -= attackTime;
    if (time <= decayTime) {
        // Decay phase - fall from 1 to sustain level
        float t = time / decayTime;
        return 1.0f - t * (1.0f - sustainLevel);
    }

    time -= decayTime;
    if (time <= sustainTime) {
        // Sustain phase - hold at sustain level
        return sustainLevel;
    }

    time -= sustainTime;
    if (time <= releaseTime) {
        // Release phase - fall from sustain level to 0
        float t = time / releaseTime;
        return sustainLevel * (1.0f - t);
    }

    return 0.0f; // Past note end
}

// SynthAudioBuffer Implementation

void SynthAudioBuffer::resize(float durationSeconds) {
    duration = durationSeconds;
    size_t frameCount = static_cast<size_t>(durationSeconds * sampleRate);
    samples.resize(frameCount * channels);
}

void SynthAudioBuffer::clear() {
    std::fill(samples.begin(), samples.end(), 0.0f);
}

// SynthEngine Implementation

SynthEngine::SynthEngine()
    : initialized(false)
    , lastGenerationTime(0.0f)
    , generatedSoundCount(0)
    , randomSeed(12345)
{
    std::cout << "SynthEngine: Constructor called" << std::endl;
}

SynthEngine::~SynthEngine() {
    if (initialized.load()) {
        shutdown();
    }
    std::cout << "SynthEngine: Destructor called" << std::endl;
}

bool SynthEngine::initialize(const SynthConfig& synthConfig) {
    std::lock_guard<std::mutex> lock(synthMutex);

    if (initialized.load()) {
        std::cout << "SynthEngine: Already initialized" << std::endl;
        return true;
    }

    std::cout << "SynthEngine: Initializing..." << std::endl;

    config = synthConfig;
    generatedSoundCount.store(0);

    initialized.store(true);
    std::cout << "SynthEngine: Initialization complete" << std::endl;
    std::cout << "  Sample Rate: " << config.sampleRate << " Hz" << std::endl;
    std::cout << "  Channels: " << config.channels << std::endl;
    std::cout << "  Bit Depth: " << config.bitDepth << " bits" << std::endl;

    return true;
}

void SynthEngine::shutdown() {
    std::lock_guard<std::mutex> lock(synthMutex);

    if (!initialized.load()) {
        return;
    }

    std::cout << "SynthEngine: Shutting down..." << std::endl;
    std::cout << "  Generated " << generatedSoundCount.load() << " sounds" << std::endl;

    initialized.store(false);
    std::cout << "SynthEngine: Shutdown complete" << std::endl;
}

// Sound generation methods

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateBeep(float frequency, float duration) {
    SynthSoundEffect effect = createBeepEffect(frequency, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateBang(float intensity, float duration) {
    SynthSoundEffect effect = createBangEffect(intensity, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateExplode(float size, float duration) {
    SynthSoundEffect effect = createExplodeEffect(size, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateBigExplosion(float size, float duration) {
    SynthSoundEffect effect = createBigExplosionEffect(size, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateSmallExplosion(float intensity, float duration) {
    SynthSoundEffect effect = createSmallExplosionEffect(intensity, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateDistantExplosion(float distance, float duration) {
    SynthSoundEffect effect = createDistantExplosionEffect(distance, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateMetalExplosion(float shrapnel, float duration) {
    SynthSoundEffect effect = createMetalExplosionEffect(shrapnel, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateZap(float frequency, float duration) {
    SynthSoundEffect effect = createZapEffect(frequency, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateCoin(float pitch, float duration) {
    SynthSoundEffect effect = createCoinEffect(pitch, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateJump(float height, float duration) {
    SynthSoundEffect effect = createJumpEffect(height, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generatePowerUp(float intensity, float duration) {
    SynthSoundEffect effect = createPowerUpEffect(intensity, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateHurt(float severity, float duration) {
    SynthSoundEffect effect = createHurtEffect(severity, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateShoot(float power, float duration) {
    SynthSoundEffect effect = createShootEffect(power, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateClick(float sharpness, float duration) {
    SynthSoundEffect effect = createClickEffect(sharpness, duration);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateSweepUp(float startFreq, float endFreq, float duration) {
    SynthSoundEffect effect = createSweepEffect(startFreq, endFreq, duration, 1.0f);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateSweepDown(float startFreq, float endFreq, float duration) {
    SynthSoundEffect effect = createSweepEffect(startFreq, endFreq, duration, 1.0f);
    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateRandomBeep(uint32_t seed, float duration) {
    if (seed != 0) {
        randomSeed = seed;
    }

    float frequency = randomRange(200.0f, 2000.0f);
    SynthSoundEffect effect = createBeepEffect(frequency, duration);

    // Add some randomness to the envelope
    effect.envelope.attackTime = randomRange(0.001f, 0.05f);
    effect.envelope.decayTime = randomRange(0.05f, duration * 0.5f);
    effect.envelope.sustainLevel = randomRange(0.3f, 0.7f);
    effect.envelope.releaseTime = randomRange(0.05f, duration * 0.3f);

    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generatePickup(float brightness, float duration) {
    SynthSoundEffect effect;
    effect.name = "pickup";
    effect.duration = duration;

    // Arpeggiated chord effect
    Oscillator osc1, osc2, osc3;
    float baseFreq = 440.0f * brightness;

    osc1.waveform = WAVE_SINE;
    osc1.frequency = baseFreq;
    osc1.amplitude = 0.4f;

    osc2.waveform = WAVE_SINE;
    osc2.frequency = baseFreq * 1.25f; // Major third
    osc2.amplitude = 0.3f;

    osc3.waveform = WAVE_SINE;
    osc3.frequency = baseFreq * 1.5f; // Perfect fifth
    osc3.amplitude = 0.2f;

    effect.oscillators = {osc1, osc2, osc3};
    effect.envelope.attackTime = 0.01f;
    effect.envelope.decayTime = duration * 0.8f;
    effect.envelope.sustainLevel = 0.2f;
    effect.envelope.releaseTime = duration * 0.2f;

    return generateSound(effect);
}

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateBlip(float pitch, float duration) {
    SynthSoundEffect effect = createBeepEffect(800.0f * pitch, duration);
    effect.envelope.attackTime = 0.001f;
    effect.envelope.decayTime = duration * 0.5f;
    effect.envelope.sustainLevel = 0.0f;
    effect.envelope.releaseTime = duration * 0.5f;

    return generateSound(effect);
}

// Core sound generation

std::unique_ptr<SynthAudioBuffer> SynthEngine::generateSound(const SynthSoundEffect& effect) {
    if (!initialized.load()) {
        std::cerr << "SynthEngine: Cannot generate sound - not initialized" << std::endl;
        return nullptr;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    auto buffer = std::make_unique<SynthAudioBuffer>(config.sampleRate, config.channels);
    buffer->resize(effect.duration);

    applySynthesis(*buffer, effect);

    auto endTime = std::chrono::high_resolution_clock::now();
    float generationTime = std::chrono::duration<float>(endTime - startTime).count();
    lastGenerationTime.store(generationTime);
    generatedSoundCount.fetch_add(1);

    std::cout << "SynthEngine: Generated '" << effect.name << "' ("
              << effect.duration << "s) in " << generationTime * 1000.0f << "ms" << std::endl;

    return buffer;
}

void SynthEngine::applySynthesis(SynthAudioBuffer& buffer, const SynthSoundEffect& effect) {
    size_t frameCount = buffer.getFrameCount();
    float dt = 1.0f / buffer.sampleRate;

    // Clear buffer
    buffer.clear();

    // Route to appropriate synthesis engine based on type
    switch (effect.synthesisType) {
        case SynthesisType::ADDITIVE:
            synthesizeAdditive(buffer, effect);
            return;
        case SynthesisType::FM:
            synthesizeFM(buffer, effect);
            return;
        case SynthesisType::GRANULAR:
            synthesizeGranular(buffer, effect);
            return;
        case SynthesisType::PHYSICAL:
            synthesizePhysical(buffer, effect);
            return;
        case SynthesisType::SUBTRACTIVE:
        default:
            break;  // Fall through to traditional oscillator synthesis
    }

    // Traditional subtractive synthesis (oscillators + filters + envelopes)
    for (const auto& osc : effect.oscillators) {
        std::vector<float> oscSamples(buffer.getSampleCount(), 0.0f);

        float phase = osc.phase;
        float currentFreq = osc.frequency;

        for (size_t frame = 0; frame < frameCount; ++frame) {
            float time = frame * dt;

            // Apply pitch sweep if specified
            if (effect.pitchSweepStart != 0.0f || effect.pitchSweepEnd != 0.0f) {
                float t = time / effect.duration;
                if (effect.pitchSweepCurve != 1.0f) {
                    t = std::pow(t, effect.pitchSweepCurve);
                }
                float sweepSemitones = effect.pitchSweepStart + t * (effect.pitchSweepEnd - effect.pitchSweepStart);
                currentFreq = osc.frequency * semitonesToRatio(sweepSemitones);
            }

            // Apply frequency modulation
            if (osc.fmAmount > 0.0f && osc.fmFreq > 0.0f) {
                float fmValue = std::sin(2.0f * M_PI * osc.fmFreq * time);
                currentFreq += osc.frequency * osc.fmAmount * fmValue;
            }

            // Generate waveform sample
            float sample = generateWaveform(osc.waveform, phase, osc.pulseWidth) * osc.amplitude;

            // Apply amplitude modulation
            if (osc.amAmount > 0.0f && osc.amFreq > 0.0f) {
                float amValue = 0.5f + 0.5f * std::sin(2.0f * M_PI * osc.amFreq * time);
                sample *= (1.0f - osc.amAmount + osc.amAmount * amValue);
            }

            // Apply envelope
            sample *= effect.envelope.getValue(time, effect.duration);

            // Write to both channels (stereo)
            for (uint32_t ch = 0; ch < buffer.channels; ++ch) {
                oscSamples[frame * buffer.channels + ch] += sample;
            }

            // Update phase
            phase += 2.0f * M_PI * currentFreq * dt;
            while (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
        }

        // Add oscillator to main buffer
        for (size_t i = 0; i < buffer.getSampleCount(); ++i) {
            buffer.samples[i] += oscSamples[i];
        }
    }

    // Mix in noise if specified
    if (effect.noiseMix > 0.0f) {
        for (size_t frame = 0; frame < frameCount; ++frame) {
            float time = frame * dt;
            float envelope = effect.envelope.getValue(time, effect.duration);
            float noiseSample = generateNoise() * effect.noiseMix * envelope;

            for (uint32_t ch = 0; ch < buffer.channels; ++ch) {
                buffer.samples[frame * buffer.channels + ch] += noiseSample;
            }
        }
    }

    // Apply effects
    if (effect.filter.type != FilterType::NONE) {
        applyFilter(buffer, effect.filter);
    }

    if (effect.distortion > 0.0f) {
        applyDistortion(buffer, effect.distortion);
    }

    if (effect.echoCount > 0 && effect.echoDelay > 0.0f) {
        applyEcho(buffer, effect.echoDelay, effect.echoDecay, effect.echoCount);
    }

    // Normalize to prevent clipping
    normalizeBuffer(buffer, 0.8f);
}

// Waveform generation

float SynthEngine::generateWaveform(WaveformType type, float phase, float pulseWidth) {
    switch (type) {
        case WAVE_SINE:
            return std::sin(phase);

        case WAVE_SQUARE:
            return (std::sin(phase) >= 0.0f) ? 1.0f : -1.0f;

        case WAVE_SAWTOOTH:
            return 2.0f * (phase / (2.0f * M_PI)) - 1.0f;

        case WAVE_TRIANGLE: {
            float t = phase / (2.0f * M_PI);
            if (t < 0.5f) {
                return 4.0f * t - 1.0f;
            } else {
                return 3.0f - 4.0f * t;
            }
        }

        case WAVE_NOISE:
            return generateNoise();

        default:
            return 0.0f;
    }
}

float SynthEngine::generateNoise() {
    // Simple linear congruential generator
    randomSeed = (randomSeed * 1103515245 + 12345) & 0x7fffffff;
    return 2.0f * (randomSeed / float(0x7fffffff)) - 1.0f;
}

// Effect processing

void SynthEngine::applyFilter(SynthAudioBuffer& buffer, const FilterParams& filter) {
    if (filter.type == FilterType::NONE) return;

    // Simple single-pole filter implementation
    float fc = filter.cutoffFreq / buffer.sampleRate;
    float alpha = 2.0f * M_PI * fc;

    std::vector<float> y1(buffer.channels, 0.0f);  // Previous output
    std::vector<float> x1(buffer.channels, 0.0f);  // Previous input

    size_t frameCount = buffer.getFrameCount();

    for (size_t frame = 0; frame < frameCount; ++frame) {
        for (uint32_t ch = 0; ch < buffer.channels; ++ch) {
            size_t idx = frame * buffer.channels + ch;
            float x = buffer.samples[idx];
            float y = 0.0f;

            switch (filter.type) {
                case FilterType::LOW_PASS:
                    y = alpha * x + (1.0f - alpha) * y1[ch];
                    break;

                case FilterType::HIGH_PASS:
                    y = alpha * (y1[ch] + x - x1[ch]);
                    break;

                case FilterType::BAND_PASS:
                    // Simple implementation
                    y = alpha * x + (1.0f - alpha) * y1[ch];
                    y = alpha * (y - y1[ch]);
                    break;

                default:
                    y = x;
                    break;
            }

            buffer.samples[idx] = y;
            x1[ch] = x;
            y1[ch] = y;
        }
    }
}

void SynthEngine::applyDistortion(SynthAudioBuffer& buffer, float amount) {
    if (amount <= 0.0f) return;

    float drive = 1.0f + amount * 10.0f;  // Drive amount

    for (float& sample : buffer.samples) {
        sample *= drive;
        // Soft clipping
        if (sample > 1.0f) {
            sample = 1.0f - std::exp(-(sample - 1.0f));
        } else if (sample < -1.0f) {
            sample = -1.0f + std::exp(sample + 1.0f);
        }
        sample /= drive * 0.5f;  // Compensate for level increase
    }
}

void SynthEngine::applyEcho(SynthAudioBuffer& buffer, float delay, float decay, int count) {
    if (count <= 0 || delay <= 0.0f || decay <= 0.0f) return;

    size_t delaySamples = static_cast<size_t>(delay * buffer.sampleRate) * buffer.channels;
    std::vector<float> originalSamples = buffer.samples;

    for (int echo = 1; echo <= count; ++echo) {
        size_t offset = echo * delaySamples;
        float amplitude = std::pow(decay, echo);

        for (size_t i = 0; i + offset < buffer.samples.size(); ++i) {
            buffer.samples[i + offset] += originalSamples[i] * amplitude;
        }
    }
}

void SynthEngine::normalizeBuffer(SynthAudioBuffer& buffer, float targetLevel) {
    if (buffer.samples.empty()) return;

    // Find peak amplitude
    float peak = 0.0f;
    for (float sample : buffer.samples) {
        peak = std::max(peak, std::abs(sample));
    }

    if (peak > 0.0f && peak != targetLevel) {
        float scale = targetLevel / peak;
        for (float& sample : buffer.samples) {
            sample *= scale;
        }
    }
}

// WAV export

bool SynthEngine::exportToWAV(const SynthAudioBuffer& buffer, const std::string& filename,
                             const WAVExportParams& params) {
    if (buffer.samples.empty()) {
        std::cerr << "SynthEngine: Cannot export empty buffer to WAV" << std::endl;
        return false;
    }

    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) {
        std::cerr << "SynthEngine: Failed to create WAV file: " << filename << std::endl;
        return false;
    }

    // Calculate data size
    size_t samplesPerChannel = buffer.getFrameCount();
    size_t bytesPerSample = params.bitDepth / 8;
    uint32_t dataSize = samplesPerChannel * params.channels * bytesPerSample;

    // Write WAV header
    if (!writeWAVHeader(file, params, dataSize)) {
        fclose(file);
        return false;
    }

    // Write audio data
    if (!writeWAVData(file, buffer, params)) {
        fclose(file);
        return false;
    }

    fclose(file);

    std::cout << "SynthEngine: Exported WAV file: " << filename
              << " (" << samplesPerChannel << " frames, "
              << params.channels << " channels, "
              << params.bitDepth << " bit)" << std::endl;

    return true;
}

bool SynthEngine::writeWAVHeader(FILE* file, const WAVExportParams& params, uint32_t dataSize) {
    WAVHeader header = {};

    // RIFF header
    memcpy(header.riffId, "RIFF", 4);
    header.riffSize = 36 + dataSize;
    memcpy(header.waveId, "WAVE", 4);

    // Format chunk
    memcpy(header.fmtId, "fmt ", 4);
    header.fmtSize = 16;
    header.format = 1;  // PCM
    header.channels = params.channels;
    header.sampleRate = params.sampleRate;
    header.bitsPerSample = params.bitDepth;
    header.blockAlign = header.channels * (header.bitsPerSample / 8);
    header.byteRate = header.sampleRate * header.blockAlign;

    // Data chunk header
    memcpy(header.dataId, "data", 4);
    header.dataSize = dataSize;

    return fwrite(&header, sizeof(header), 1, file) == 1;
}

bool SynthEngine::writeWAVData(FILE* file, const SynthAudioBuffer& buffer, const WAVExportParams& params) {
    if (params.bitDepth == 16) {
        std::vector<int16_t> intSamples;
        convertFloatToInt16(buffer.samples, intSamples, params.volume);
        return fwrite(intSamples.data(), sizeof(int16_t), intSamples.size(), file) == intSamples.size();
    } else if (params.bitDepth == 32) {
        std::vector<int32_t> intSamples;
        convertFloatToInt32(buffer.samples, intSamples, params.volume);
        return fwrite(intSamples.data(), sizeof(int32_t), intSamples.size(), file) == intSamples.size();
    }

    std::cerr << "SynthEngine: Unsupported bit depth: " << params.bitDepth << std::endl;
    return false;
}

void SynthEngine::convertFloatToInt16(const std::vector<float>& input, std::vector<int16_t>& output, float volume) {
    output.resize(input.size());
    const float scale = 32767.0f * volume;

    for (size_t i = 0; i < input.size(); ++i) {
        float sample = std::clamp(input[i] * scale, -32767.0f, 32767.0f);
        output[i] = static_cast<int16_t>(sample);
    }
}

void SynthEngine::convertFloatToInt32(const std::vector<float>& input, std::vector<int32_t>& output, float volume) {
    output.resize(input.size());
    const float scale = 2147483647.0f * volume;

    for (size_t i = 0; i < input.size(); ++i) {
        float sample = std::clamp(input[i] * scale, -2147483647.0f, 2147483647.0f);
        output[i] = static_cast<int32_t>(sample);
    }
}

// Utility functions

float SynthEngine::noteToFrequency(int midiNote) {
    return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
}

int SynthEngine::frequencyToNote(float frequency) {
    return static_cast<int>(69 + 12 * std::log2(frequency / 440.0f));
}

float SynthEngine::semitonesToRatio(float semitones) {
    return std::pow(2.0f, semitones / 12.0f);
}

float SynthEngine::dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float SynthEngine::linearToDb(float linear) {
    return 20.0f * std::log10(std::max(linear, 1e-10f));
}

float SynthEngine::random01() {
    randomSeed = (randomSeed * 1103515245 + 12345) & 0x7fffffff;
    return randomSeed / float(0x7fffffff);
}

float SynthEngine::randomRange(float min, float max) {
    return min + random01() * (max - min);
}

// Predefined sound effect creators

SynthSoundEffect SynthEngine::createBeepEffect(float frequency, float duration) {
    SynthSoundEffect effect;
    effect.name = "beep";
    effect.duration = duration;

    Oscillator osc;
    osc.waveform = WAVE_SINE;
    osc.frequency = frequency;
    osc.amplitude = 0.8f;

    effect.oscillators = {osc};
    effect.envelope.attackTime = 0.01f;
    effect.envelope.decayTime = 0.05f;
    effect.envelope.sustainLevel = 0.8f;
    effect.envelope.releaseTime = 0.1f;

    return effect;
}

SynthSoundEffect SynthEngine::createBangEffect(float intensity, float duration) {
    SynthSoundEffect effect;
    effect.name = "bang";
    effect.duration = duration;

    // Low frequency thump
    Oscillator thump;
    thump.waveform = WAVE_SINE;
    thump.frequency = 60.0f * intensity;
    thump.amplitude = 0.8f;

    // Mid frequency punch
    Oscillator punch;
    punch.waveform = WAVE_SQUARE;
    punch.frequency = 200.0f * intensity;
    punch.amplitude = 0.6f;

    // High frequency crack
    Oscillator crack;
    crack.waveform = WAVE_NOISE;
    crack.frequency = 1500.0f * intensity;
    crack.amplitude = 1.0f;

    effect.oscillators = {thump, punch, crack};
    effect.envelope.attackTime = 0.0005f;  // Very sharp attack
    effect.envelope.decayTime = duration * 0.2f;  // Quick decay
    effect.envelope.sustainLevel = 0.3f;   // Some sustain for body
    effect.envelope.releaseTime = duration * 0.8f;  // Long tail

    // Pitch sweep for impact
    effect.pitchSweepStart = 5.0f;
    effect.pitchSweepEnd = -15.0f;
    effect.pitchSweepCurve = 0.4f;

    effect.filter.type = FilterType::HIGH_PASS;
    effect.filter.cutoffFreq = 150.0f / intensity;
    effect.distortion = 0.5f * intensity;

    return effect;
}

SynthSoundEffect SynthEngine::createExplodeEffect(float size, float duration) {
    SynthSoundEffect effect;
    effect.name = "explode";
    effect.duration = duration;

    // Deep bass rumble (sub-bass)
    Oscillator subBass;
    subBass.waveform = WAVE_SINE;
    subBass.frequency = 25.0f * size;  // Much lower frequency
    subBass.amplitude = 1.0f;  // Max amplitude for impact

    // Mid-bass thump
    Oscillator midBass;
    midBass.waveform = WAVE_SQUARE;
    midBass.frequency = 80.0f * size;
    midBass.amplitude = 0.9f;

    // Low-mid punch
    Oscillator punch;
    punch.waveform = WAVE_SAWTOOTH;
    punch.frequency = 150.0f * size;
    punch.amplitude = 0.7f;

    // High frequency crackle (more aggressive)
    Oscillator crackle;
    crackle.waveform = WAVE_NOISE;
    crackle.frequency = 2000.0f;
    crackle.amplitude = 1.0f;

    // Sharp attack noise
    Oscillator attack;
    attack.waveform = WAVE_NOISE;
    attack.frequency = 8000.0f;
    attack.amplitude = 0.8f;

    effect.oscillators = {subBass, midBass, punch, crackle, attack};

    // Much more aggressive envelope for impact
    effect.envelope.attackTime = 0.0005f;  // Instant attack
    effect.envelope.decayTime = duration * 0.15f;  // Faster initial decay
    effect.envelope.sustainLevel = 0.6f;   // Higher sustain for longer rumble
    effect.envelope.releaseTime = duration * 0.85f;  // Long rumbling tail

    // More dramatic pitch sweep
    effect.pitchSweepStart = 12.0f;   // Start higher for impact
    effect.pitchSweepEnd = -36.0f;    // Drop 3 octaves for deep rumble
    effect.pitchSweepCurve = 0.3f;    // Very fast exponential decay

    // Heavy distortion for aggressive sound
    effect.distortion = 0.8f * size;

    // Add some noise mixing for chaos
    effect.noiseMix = 0.3f * size;

    return effect;
}

SynthSoundEffect SynthEngine::createBigExplosionEffect(float size, float duration) {
    SynthSoundEffect effect;
    effect.name = "big_explosion";
    effect.duration = duration;

    // Massive sub-bass for earth-shaking impact
    Oscillator ultraBass;
    ultraBass.waveform = WAVE_SINE;
    ultraBass.frequency = 15.0f * size;  // Ultra-low frequency
    ultraBass.amplitude = 1.2f;

    // Deep bass rumble
    Oscillator deepBass;
    deepBass.waveform = WAVE_SQUARE;
    deepBass.frequency = 35.0f * size;
    deepBass.amplitude = 1.0f;

    // Mid-bass punch
    Oscillator midBass;
    midBass.waveform = WAVE_SAWTOOTH;
    midBass.frequency = 75.0f * size;
    midBass.amplitude = 0.9f;

    // Upper-mid aggression
    Oscillator upperMid;
    upperMid.waveform = WAVE_SQUARE;
    upperMid.frequency = 200.0f * size;
    upperMid.amplitude = 0.8f;

    // Massive noise component
    Oscillator chaos;
    chaos.waveform = WAVE_NOISE;
    chaos.frequency = 3000.0f;
    chaos.amplitude = 1.2f;

    effect.oscillators = {ultraBass, deepBass, midBass, upperMid, chaos};

    // Long, devastating envelope
    effect.envelope.attackTime = 0.0003f;  // Instant devastation
    effect.envelope.decayTime = duration * 0.1f;
    effect.envelope.sustainLevel = 0.8f;   // Long rumble
    effect.envelope.releaseTime = duration * 0.9f;

    // Dramatic pitch drop
    effect.pitchSweepStart = 24.0f;   // Start very high
    effect.pitchSweepEnd = -48.0f;    // Drop 4 octaves
    effect.pitchSweepCurve = 0.25f;   // Very aggressive curve

    // Maximum distortion and chaos
    effect.distortion = 1.0f;
    effect.noiseMix = 0.5f * size;

    return effect;
}

SynthSoundEffect SynthEngine::createSmallExplosionEffect(float intensity, float duration) {
    SynthSoundEffect effect;
    effect.name = "small_explosion";
    effect.duration = duration;

    // Punchy mid-bass
    Oscillator punch;
    punch.waveform = WAVE_SQUARE;
    punch.frequency = 120.0f * intensity;
    punch.amplitude = 0.9f;

    // Sharp crack
    Oscillator crack;
    crack.waveform = WAVE_SAWTOOTH;
    crack.frequency = 400.0f * intensity;
    crack.amplitude = 0.7f;

    // High frequency snap
    Oscillator snap;
    snap.waveform = WAVE_NOISE;
    snap.frequency = 4000.0f;
    snap.amplitude = 0.8f;

    effect.oscillators = {punch, crack, snap};

    // Quick, sharp envelope
    effect.envelope.attackTime = 0.001f;
    effect.envelope.decayTime = duration * 0.4f;
    effect.envelope.sustainLevel = 0.2f;
    effect.envelope.releaseTime = duration * 0.6f;

    // Moderate pitch sweep
    effect.pitchSweepStart = 8.0f;
    effect.pitchSweepEnd = -16.0f;
    effect.pitchSweepCurve = 0.6f;

    effect.distortion = 0.6f * intensity;
    effect.noiseMix = 0.2f;

    return effect;
}

SynthSoundEffect SynthEngine::createDistantExplosionEffect(float distance, float duration) {
    SynthSoundEffect effect;
    effect.name = "distant_explosion";
    effect.duration = duration;

    // Muffled low frequency (distance effect)
    Oscillator rumble;
    rumble.waveform = WAVE_SINE;
    rumble.frequency = 60.0f / (1.0f + distance * 0.5f);
    rumble.amplitude = 0.8f / (1.0f + distance * 0.3f);

    // Filtered mid frequencies
    Oscillator mid;
    mid.waveform = WAVE_TRIANGLE;
    mid.frequency = 200.0f / (1.0f + distance * 0.3f);
    mid.amplitude = 0.5f / (1.0f + distance * 0.5f);

    effect.oscillators = {rumble, mid};

    // Slower envelope for distance
    effect.envelope.attackTime = 0.05f + distance * 0.02f;  // Delayed attack
    effect.envelope.decayTime = duration * 0.3f;
    effect.envelope.sustainLevel = 0.6f;
    effect.envelope.releaseTime = duration * 0.7f;

    // Gentle pitch sweep
    effect.pitchSweepStart = 0.0f;
    effect.pitchSweepEnd = -12.0f;
    effect.pitchSweepCurve = 1.0f;

    // Low-pass filtering for distance
    effect.filter.type = FilterType::LOW_PASS;
    effect.filter.cutoffFreq = 1000.0f / (1.0f + distance * 0.8f);

    effect.distortion = 0.2f;

    return effect;
}

SynthSoundEffect SynthEngine::createMetalExplosionEffect(float shrapnel, float duration) {
    SynthSoundEffect effect;
    effect.name = "metal_explosion";
    effect.duration = duration;

    // Heavy bass thump
    Oscillator thump;
    thump.waveform = WAVE_SQUARE;
    thump.frequency = 80.0f * shrapnel;
    thump.amplitude = 0.9f;

    // Metallic ring
    Oscillator ring1;
    ring1.waveform = WAVE_SINE;
    ring1.frequency = 1200.0f * shrapnel;
    ring1.amplitude = 0.6f;

    // Higher metallic overtones
    Oscillator ring2;
    ring2.waveform = WAVE_SINE;
    ring2.frequency = 2400.0f * shrapnel;
    ring2.amplitude = 0.4f;

    // Harsh metallic noise
    Oscillator metalNoise;
    metalNoise.waveform = WAVE_NOISE;
    metalNoise.frequency = 6000.0f;
    metalNoise.amplitude = 0.8f * shrapnel;

    effect.oscillators = {thump, ring1, ring2, metalNoise};

    // Sharp attack with ringing tail
    effect.envelope.attackTime = 0.0005f;
    effect.envelope.decayTime = duration * 0.2f;
    effect.envelope.sustainLevel = 0.4f;
    effect.envelope.releaseTime = duration * 0.8f;

    // Sharp pitch drop for impact
    effect.pitchSweepStart = 12.0f;
    effect.pitchSweepEnd = -18.0f;
    effect.pitchSweepCurve = 0.4f;

    // High-pass filter for metallic character
    effect.filter.type = FilterType::HIGH_PASS;
    effect.filter.cutoffFreq = 400.0f;

    effect.distortion = 0.7f * shrapnel;
    effect.noiseMix = 0.4f * shrapnel;

    return effect;
}

SynthSoundEffect SynthEngine::createZapEffect(float frequency, float duration) {
    SynthSoundEffect effect;
    effect.name = "zap";
    effect.duration = duration;

    Oscillator osc;
    osc.waveform = WAVE_SAWTOOTH;
    osc.frequency = frequency;
    osc.amplitude = 0.9f;
    osc.fmAmount = 0.3f;
    osc.fmFreq = 50.0f;

    effect.oscillators = {osc};
    effect.envelope.attackTime = 0.001f;
    effect.envelope.decayTime = duration * 0.8f;
    effect.envelope.sustainLevel = 0.0f;
    effect.envelope.releaseTime = duration * 0.2f;

    effect.pitchSweepStart = 12.0f;  // Start 1 octave higher
    effect.pitchSweepEnd = -12.0f;   // End 1 octave lower
    effect.pitchSweepCurve = 2.0f;   // Fast initial drop

    effect.filter.type = FilterType::HIGH_PASS;
    effect.filter.cutoffFreq = 500.0f;

    return effect;
}

SynthSoundEffect SynthEngine::createCoinEffect(float pitch, float duration) {
    SynthSoundEffect effect;
    effect.name = "coin";
    effect.duration = duration;

    // Multiple harmonics for a bell-like sound
    Oscillator fund, harm2, harm3;
    float baseFreq = 800.0f * pitch;

    fund.waveform = WAVE_SINE;
    fund.frequency = baseFreq;
    fund.amplitude = 0.6f;

    harm2.waveform = WAVE_SINE;
    harm2.frequency = baseFreq * 2.0f;
    harm2.amplitude = 0.3f;

    harm3.waveform = WAVE_SINE;
    harm3.frequency = baseFreq * 3.0f;
    harm3.amplitude = 0.2f;

    effect.oscillators = {fund, harm2, harm3};
    effect.envelope.attackTime = 0.01f;
    effect.envelope.decayTime = duration * 0.7f;
    effect.envelope.sustainLevel = 0.2f;
    effect.envelope.releaseTime = duration * 0.3f;

    return effect;
}

SynthSoundEffect SynthEngine::createJumpEffect(float height, float duration) {
    SynthSoundEffect effect;
    effect.name = "jump";
    effect.duration = duration;

    Oscillator osc;
    osc.waveform = WAVE_SQUARE;
    osc.frequency = 200.0f + 300.0f * height;
    osc.amplitude = 0.7f;

    effect.oscillators = {osc};
    effect.envelope.attackTime = 0.01f;
    effect.envelope.decayTime = duration * 0.6f;
    effect.envelope.sustainLevel = 0.3f;
    effect.envelope.releaseTime = duration * 0.4f;

    effect.pitchSweepStart = 0.0f;
    effect.pitchSweepEnd = 12.0f * height; // Rise in pitch
    effect.pitchSweepCurve = 0.7f;

    return effect;
}

SynthSoundEffect SynthEngine::createPowerUpEffect(float intensity, float duration) {
    SynthSoundEffect effect;
    effect.name = "powerup";
    effect.duration = duration;

    Oscillator osc1, osc2;
    osc1.waveform = WAVE_SAWTOOTH;
    osc1.frequency = 100.0f;
    osc1.amplitude = 0.5f;

    osc2.waveform = WAVE_SINE;
    osc2.frequency = 200.0f;
    osc2.amplitude = 0.4f;

    effect.oscillators = {osc1, osc2};
    effect.envelope.attackTime = duration * 0.1f;
    effect.envelope.decayTime = duration * 0.2f;
    effect.envelope.sustainLevel = 0.8f;
    effect.envelope.releaseTime = duration * 0.7f;

    effect.pitchSweepStart = -12.0f;
    effect.pitchSweepEnd = 24.0f * intensity;
    effect.pitchSweepCurve = 1.5f;

    return effect;
}

SynthSoundEffect SynthEngine::createHurtEffect(float severity, float duration) {
    SynthSoundEffect effect;
    effect.name = "hurt";
    effect.duration = duration;

    Oscillator osc;
    osc.waveform = WAVE_SAWTOOTH;
    osc.frequency = 300.0f + 200.0f * severity;
    osc.amplitude = 0.8f;
    osc.fmAmount = 0.5f * severity;
    osc.fmFreq = 20.0f;

    effect.oscillators = {osc};
    effect.envelope.attackTime = 0.001f;
    effect.envelope.decayTime = duration * 0.3f;
    effect.envelope.sustainLevel = 0.4f;
    effect.envelope.releaseTime = duration * 0.7f;

    effect.pitchSweepStart = 5.0f;
    effect.pitchSweepEnd = -10.0f * severity;
    effect.distortion = 0.2f * severity;

    return effect;
}

SynthSoundEffect SynthEngine::createShootEffect(float power, float duration) {
    SynthSoundEffect effect;
    effect.name = "shoot";
    effect.duration = duration;

    Oscillator osc;
    osc.waveform = WAVE_NOISE;
    osc.frequency = 2000.0f * power;
    osc.amplitude = 0.9f;

    effect.oscillators = {osc};
    effect.envelope.attackTime = 0.001f;
    effect.envelope.decayTime = duration * 0.5f;
    effect.envelope.sustainLevel = 0.2f;
    effect.envelope.releaseTime = duration * 0.5f;

    effect.filter.type = FilterType::BAND_PASS;
    effect.filter.cutoffFreq = 1500.0f * power;
    effect.filter.resonance = 2.0f;

    return effect;
}

SynthSoundEffect SynthEngine::createClickEffect(float sharpness, float duration) {
    SynthSoundEffect effect;
    effect.name = "click";
    effect.duration = duration;

    Oscillator osc;
    osc.waveform = WAVE_SQUARE;
    osc.frequency = 1000.0f + 2000.0f * sharpness;
    osc.amplitude = 0.8f;

    effect.oscillators = {osc};
    effect.envelope.attackTime = 0.001f;
    effect.envelope.decayTime = duration * 0.8f;
    effect.envelope.sustainLevel = 0.0f;
    effect.envelope.releaseTime = duration * 0.2f;

    effect.filter.type = FilterType::HIGH_PASS;
    effect.filter.cutoffFreq = 500.0f * sharpness;

    return effect;
}

SynthSoundEffect SynthEngine::createSweepEffect(float startFreq, float endFreq, float duration, float intensity) {
    SynthSoundEffect effect;
    effect.name = "sweep";
    effect.duration = duration;

    Oscillator osc;
    osc.waveform = WAVE_SINE;
    osc.frequency = startFreq;
    osc.amplitude = 0.7f;

    effect.oscillators = {osc};
    effect.envelope.attackTime = 0.01f;
    effect.envelope.decayTime = 0.1f;
    effect.envelope.sustainLevel = 0.8f;
    effect.envelope.releaseTime = 0.1f;

    // Calculate pitch sweep in semitones
    effect.pitchSweepStart = 0.0f;
    effect.pitchSweepEnd = 12.0f * std::log2(endFreq / startFreq);
    effect.pitchSweepCurve = intensity;

    return effect;
}

// C interface implementation

extern "C" {

bool synth_initialize() {
    if (!g_synthEngine) {
        g_synthEngine = std::make_unique<SynthEngine>();
    }
    return g_synthEngine->initialize();
}

void synth_shutdown() {
    if (g_synthEngine) {
        g_synthEngine->shutdown();
        g_synthEngine.reset();
    }
}

bool synth_is_initialized() {
    return g_synthEngine && g_synthEngine->isInitialized();
}

bool synth_generate_beep(const char* filename, float frequency, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateBeep(frequency, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_big_explosion(const char* filename, float size, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateBigExplosion(size, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_small_explosion(const char* filename, float intensity, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateSmallExplosion(intensity, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_distant_explosion(const char* filename, float distance, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateDistantExplosion(distance, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_metal_explosion(const char* filename, float shrapnel, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateMetalExplosion(shrapnel, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_bang(const char* filename, float intensity, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateBang(intensity, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_explode(const char* filename, float size, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateExplode(size, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_zap(const char* filename, float frequency, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateZap(frequency, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_coin(const char* filename, float pitch, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateCoin(pitch, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_jump(const char* filename, float height, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateJump(height, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_powerup(const char* filename, float intensity, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generatePowerUp(intensity, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_hurt(const char* filename, float severity, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateHurt(severity, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_shoot(const char* filename, float power, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateShoot(power, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_click(const char* filename, float sharpness, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateClick(sharpness, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_sweep_up(const char* filename, float startFreq, float endFreq, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateSweepUp(startFreq, endFreq, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_sweep_down(const char* filename, float startFreq, float endFreq, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateSweepDown(startFreq, endFreq, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_random_beep(const char* filename, uint32_t seed, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateRandomBeep(seed, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_pickup(const char* filename, float brightness, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generatePickup(brightness, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_blip(const char* filename, float pitch, float duration) {
    if (!g_synthEngine || !filename) return false;

    auto buffer = g_synthEngine->generateBlip(pitch, duration);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_oscillator(const char* filename, int waveform, float frequency,
                              float duration, float attack, float decay,
                              float sustain, float release) {
    if (!g_synthEngine || !filename) return false;

    SynthSoundEffect effect;
    effect.name = "custom_oscillator";
    effect.duration = duration;

    Oscillator osc;
    osc.waveform = (WaveformType)waveform;
    osc.frequency = frequency;
    osc.amplitude = 0.8f;

    effect.oscillators = {osc};
    effect.envelope.attackTime = attack;
    effect.envelope.decayTime = decay;
    effect.envelope.sustainLevel = sustain;
    effect.envelope.releaseTime = release;

    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer) return false;

    return g_synthEngine->exportToWAV(*buffer, filename);
}

bool synth_generate_sound_pack(const char* directory) {
    if (!g_synthEngine || !directory) return false;

    std::string dir(directory);
    if (dir.back() != '/') dir += '/';

    bool success = true;

    // Generate common game sounds
    success &= synth_generate_beep((dir + "beep.wav").c_str(), 800.0f, 0.2f);
    success &= synth_generate_bang((dir + "bang.wav").c_str(), 1.0f, 0.3f);
    success &= synth_generate_explode((dir + "explode.wav").c_str(), 1.0f, 1.0f);
    success &= synth_generate_big_explosion((dir + "big_explosion.wav").c_str(), 1.5f, 2.5f);
    success &= synth_generate_small_explosion((dir + "small_explosion.wav").c_str(), 1.0f, 0.4f);
    success &= synth_generate_distant_explosion((dir + "distant_explosion.wav").c_str(), 2.0f, 1.8f);
    success &= synth_generate_metal_explosion((dir + "metal_explosion.wav").c_str(), 1.2f, 1.5f);
    success &= synth_generate_zap((dir + "zap.wav").c_str(), 2000.0f, 0.15f);
    success &= synth_generate_coin((dir + "coin.wav").c_str(), 1.0f, 0.4f);
    success &= synth_generate_jump((dir + "jump.wav").c_str(), 1.0f, 0.3f);
    success &= synth_generate_powerup((dir + "powerup.wav").c_str(), 1.0f, 0.8f);
    success &= synth_generate_hurt((dir + "hurt.wav").c_str(), 1.0f, 0.4f);
    success &= synth_generate_shoot((dir + "shoot.wav").c_str(), 1.0f, 0.2f);
    success &= synth_generate_click((dir + "click.wav").c_str(), 1.0f, 0.05f);
    success &= synth_generate_pickup((dir + "pickup.wav").c_str(), 1.0f, 0.25f);
    success &= synth_generate_blip((dir + "blip.wav").c_str(), 1.0f, 0.1f);

    return success;
}

float synth_note_to_frequency(int midiNote) {
    return SynthEngine::noteToFrequency(midiNote);
}

int synth_frequency_to_note(float frequency) {
    return SynthEngine::frequencyToNote(frequency);
}

float synth_get_last_generation_time() {
    return g_synthEngine ? g_synthEngine->getLastGenerationTime() : 0.0f;
}

size_t synth_get_generated_count() {
    return g_synthEngine ? g_synthEngine->getGeneratedSoundCount() : 0;
}

// C function wrappers for memory-based sound generation
extern "C" {
    uint32_t synth_create_beep(float frequency, float duration);
    uint32_t synth_create_explode(float size, float duration);
    uint32_t synth_create_coin(float pitch, float duration);
    uint32_t synth_create_shoot(float intensity, float duration);
    uint32_t synth_create_click(float intensity, float duration);
    uint32_t synth_create_jump(float power, float duration);
    uint32_t synth_create_powerup(float intensity, float duration);
    uint32_t synth_create_hurt(float intensity, float duration);
    uint32_t synth_create_pickup(float pitch, float duration);
    uint32_t synth_create_blip(float pitch, float duration);
    uint32_t synth_create_zap(float frequency, float duration);
    uint32_t synth_create_big_explosion(float size, float duration);
    uint32_t synth_create_small_explosion(float intensity, float duration);
    uint32_t synth_create_distant_explosion(float distance, float duration);
    uint32_t synth_create_metal_explosion(float shrapnel, float duration);
    uint32_t synth_create_sweep_up(float startFreq, float endFreq, float duration);
    uint32_t synth_create_sweep_down(float startFreq, float endFreq, float duration);
    uint32_t synth_create_oscillator(int waveform, float frequency, float duration,
                                    float attack, float decay, float sustain, float release);
    uint32_t synth_create_random_beep(uint32_t seed, float duration);
}

uint32_t synth_create_beep(float frequency, float duration) {
    return g_synthEngine ? g_synthEngine->generateBeepToMemory(frequency, duration) : 0;
}

uint32_t synth_create_explode(float size, float duration) {
    return g_synthEngine ? g_synthEngine->generateExplodeToMemory(size, duration) : 0;
}

uint32_t synth_create_coin(float pitch, float duration) {
    return g_synthEngine ? g_synthEngine->generateCoinToMemory(pitch, duration) : 0;
}

uint32_t synth_create_shoot(float intensity, float duration) {
    return g_synthEngine ? g_synthEngine->generateShootToMemory(intensity, duration) : 0;
}

uint32_t synth_create_click(float intensity, float duration) {
    return g_synthEngine ? g_synthEngine->generateClickToMemory(intensity, duration) : 0;
}

uint32_t synth_create_jump(float power, float duration) {
    return g_synthEngine ? g_synthEngine->generateJumpToMemory(power, duration) : 0;
}

uint32_t synth_create_powerup(float intensity, float duration) {
    return g_synthEngine ? g_synthEngine->generatePowerupToMemory(intensity, duration) : 0;
}

uint32_t synth_create_hurt(float intensity, float duration) {
    return g_synthEngine ? g_synthEngine->generateHurtToMemory(intensity, duration) : 0;
}

uint32_t synth_create_pickup(float pitch, float duration) {
    return g_synthEngine ? g_synthEngine->generatePickupToMemory(pitch, duration) : 0;
}

uint32_t synth_create_blip(float pitch, float duration) {
    return g_synthEngine ? g_synthEngine->generateBlipToMemory(pitch, duration) : 0;
}

uint32_t synth_create_zap(float frequency, float duration) {
    return g_synthEngine ? g_synthEngine->generateZapToMemory(frequency, duration) : 0;
}

uint32_t synth_create_big_explosion(float size, float duration) {
    return g_synthEngine ? g_synthEngine->generateBigExplosionToMemory(size, duration) : 0;
}

uint32_t synth_create_small_explosion(float intensity, float duration) {
    return g_synthEngine ? g_synthEngine->generateSmallExplosionToMemory(intensity, duration) : 0;
}

uint32_t synth_create_distant_explosion(float distance, float duration) {
    return g_synthEngine ? g_synthEngine->generateDistantExplosionToMemory(distance, duration) : 0;
}

uint32_t synth_create_metal_explosion(float shrapnel, float duration) {
    return g_synthEngine ? g_synthEngine->generateMetalExplosionToMemory(shrapnel, duration) : 0;
}

uint32_t synth_create_sweep_up(float startFreq, float endFreq, float duration) {
    return g_synthEngine ? g_synthEngine->generateSweepUpToMemory(startFreq, endFreq, duration) : 0;
}

uint32_t synth_create_sweep_down(float startFreq, float endFreq, float duration) {
    return g_synthEngine ? g_synthEngine->generateSweepDownToMemory(startFreq, endFreq, duration) : 0;
}

uint32_t synth_create_oscillator(int waveform, float frequency, float duration,
                                float attack, float decay, float sustain, float release) {
    return g_synthEngine ? g_synthEngine->generateOscillatorToMemory(
        static_cast<WaveformType>(waveform), frequency, duration,
        attack, decay, sustain, release) : 0;
}

uint32_t synth_create_random_beep(uint32_t seed, float duration) {
    return g_synthEngine ? g_synthEngine->generateRandomBeepToMemory(seed, duration) : 0;
}

// Memory-based sound generation functions
uint32_t SynthEngine::generateBeepToMemory(float frequency, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateBeep(frequency, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    // Load directly into audio system from buffer
    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateExplodeToMemory(float size, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateExplode(size, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateCoinToMemory(float pitch, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateCoin(pitch, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateShootToMemory(float intensity, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateShoot(intensity, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateClickToMemory(float intensity, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateClick(intensity, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateJumpToMemory(float power, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateJump(power, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generatePowerupToMemory(float intensity, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generatePowerUp(intensity, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateHurtToMemory(float intensity, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateHurt(intensity, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generatePickupToMemory(float pitch, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generatePickup(pitch, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateBlipToMemory(float pitch, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateBlip(pitch, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateZapToMemory(float frequency, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateZap(frequency, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateBigExplosionToMemory(float size, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateBigExplosion(size, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateSmallExplosionToMemory(float intensity, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateSmallExplosion(intensity, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateDistantExplosionToMemory(float distance, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateDistantExplosion(distance, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateMetalExplosionToMemory(float shrapnel, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateMetalExplosion(shrapnel, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateSweepUpToMemory(float startFreq, float endFreq, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateSweepUp(startFreq, endFreq, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateSweepDownToMemory(float startFreq, float endFreq, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateSweepDown(startFreq, endFreq, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateOscillatorToMemory(WaveformType waveform, float frequency, float duration,
                                                 float attack, float decay, float sustain, float release) {
    if (!initialized.load()) {
        return 0;
    }

    // Create SoundEffect with custom oscillator
    SynthSoundEffect effect;
    effect.name = "custom_oscillator";
    effect.duration = duration;

    // Set up oscillator
    Oscillator osc;
    osc.waveform = waveform;
    osc.frequency = frequency;
    osc.amplitude = 1.0f;
    effect.oscillators.push_back(osc);

    // Set up envelope
    effect.envelope.attackTime = attack;
    effect.envelope.decayTime = decay;
    effect.envelope.sustainLevel = sustain;
    effect.envelope.releaseTime = release;

    auto buffer = generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t SynthEngine::generateRandomBeepToMemory(uint32_t seed, float duration) {
    if (!initialized.load()) {
        return 0;
    }

    auto buffer = generateRandomBeep(seed, duration);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

// Advanced synthesis implementations

bool synth_generate_additive(const char* filename, float fundamental,
                            const float* harmonics, int numHarmonics, float duration) {
    if (!g_synthEngine || !filename || numHarmonics <= 0 || numHarmonics > 32) {
        return false;
    }

    SynthSoundEffect effect;
    effect.name = "additive";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::ADDITIVE;

    // Set up additive parameters
    effect.additive.fundamental = fundamental;
    effect.additive.numHarmonics = std::min(numHarmonics, 32);
    for (int i = 0; i < effect.additive.numHarmonics; i++) {
        effect.additive.harmonics[i] = harmonics[i];
    }

    // Generate and export
    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return false;
    }

    WAVExportParams params;
    return g_synthEngine->exportToWAV(*buffer, filename, params);
}

bool synth_generate_fm(const char* filename, float carrierFreq, float modulatorFreq,
                      float modIndex, float duration) {
    if (!g_synthEngine || !filename) {
        return false;
    }

    SynthSoundEffect effect;
    effect.name = "fm";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::FM;

    // Set up FM parameters
    effect.fm.carrierFreq = carrierFreq;
    effect.fm.modulatorFreq = modulatorFreq;
    effect.fm.modIndex = modIndex;

    // Generate and export
    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return false;
    }

    WAVExportParams params;
    return g_synthEngine->exportToWAV(*buffer, filename, params);
}

bool synth_generate_granular(const char* filename, float baseFreq, float grainSize,
                            float overlap, float duration) {
    if (!g_synthEngine || !filename) {
        return false;
    }

    SynthSoundEffect effect;
    effect.name = "granular";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::GRANULAR;

    // Set up granular parameters
    effect.granular.grainSize = grainSize;
    effect.granular.overlap = overlap;
    effect.granular.pitch = baseFreq / 440.0f; // Convert to pitch ratio

    // Generate and export
    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return false;
    }

    WAVExportParams params;
    return g_synthEngine->exportToWAV(*buffer, filename, params);
}

bool synth_generate_physical_string(const char* filename, float frequency, float damping,
                                   float brightness, float duration) {
    if (!g_synthEngine || !filename) {
        return false;
    }

    SynthSoundEffect effect;
    effect.name = "physical_string";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::PHYSICAL;

    // Set up physical modeling parameters
    effect.physical.modelType = PhysicalParams::PLUCKED_STRING;
    effect.physical.frequency = frequency;
    effect.physical.damping = damping;
    effect.physical.brightness = brightness;

    // Generate and export
    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return false;
    }

    WAVExportParams params;
    return g_synthEngine->exportToWAV(*buffer, filename, params);
}

bool synth_generate_physical_bar(const char* filename, float frequency, float damping,
                                float brightness, float duration) {
    if (!g_synthEngine || !filename) {
        return false;
    }

    SynthSoundEffect effect;
    effect.name = "physical_bar";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::PHYSICAL;

    // Set up physical modeling parameters
    effect.physical.modelType = PhysicalParams::STRUCK_BAR;
    effect.physical.frequency = frequency;
    effect.physical.damping = damping;
    effect.physical.brightness = brightness;

    // Generate and export
    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return false;
    }

    WAVExportParams params;
    return g_synthEngine->exportToWAV(*buffer, filename, params);
}

bool synth_generate_physical_tube(const char* filename, float frequency, float airPressure,
                                 float brightness, float duration) {
    if (!g_synthEngine || !filename) {
        return false;
    }

    SynthSoundEffect effect;
    effect.name = "physical_tube";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::PHYSICAL;

    // Set up physical modeling parameters
    effect.physical.modelType = PhysicalParams::BLOWN_TUBE;
    effect.physical.frequency = frequency;
    effect.physical.airPressure = airPressure;
    effect.physical.brightness = brightness;

    // Generate and export
    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return false;
    }

    WAVExportParams params;
    return g_synthEngine->exportToWAV(*buffer, filename, params);
}

bool synth_generate_physical_drum(const char* filename, float frequency, float damping,
                                 float excitation, float duration) {
    if (!g_synthEngine || !filename) {
        return false;
    }

    SynthSoundEffect effect;
    effect.name = "physical_drum";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::PHYSICAL;

    // Set up physical modeling parameters
    effect.physical.modelType = PhysicalParams::DRUMHEAD;
    effect.physical.frequency = frequency;
    effect.physical.damping = damping;
    effect.physical.excitation = excitation;

    // Generate and export
    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return false;
    }

    WAVExportParams params;
    return g_synthEngine->exportToWAV(*buffer, filename, params);
}

// Memory-based advanced synthesis

uint32_t synth_create_additive(float fundamental, const float* harmonics, int numHarmonics, float duration) {
    if (!g_synthEngine || numHarmonics <= 0 || numHarmonics > 32) {
        return 0;
    }

    SynthSoundEffect effect;
    effect.name = "additive";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::ADDITIVE;

    // Set up additive parameters
    effect.additive.fundamental = fundamental;
    effect.additive.numHarmonics = std::min(numHarmonics, 32);
    for (int i = 0; i < effect.additive.numHarmonics; i++) {
        effect.additive.harmonics[i] = harmonics[i];
    }

    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t synth_create_fm(float carrierFreq, float modulatorFreq, float modIndex, float duration) {
    if (!g_synthEngine) {
        return 0;
    }

    SynthSoundEffect effect;
    effect.name = "fm";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::FM;

    // Set up FM parameters
    effect.fm.carrierFreq = carrierFreq;
    effect.fm.modulatorFreq = modulatorFreq;
    effect.fm.modIndex = modIndex;

    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t synth_create_granular(float baseFreq, float grainSize, float overlap, float duration) {
    if (!g_synthEngine) {
        return 0;
    }

    SynthSoundEffect effect;
    effect.name = "granular";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::GRANULAR;

    // Set up granular parameters
    effect.granular.grainSize = grainSize;
    effect.granular.overlap = overlap;
    effect.granular.pitch = baseFreq / 440.0f; // Convert to pitch ratio

    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t synth_create_physical_string(float frequency, float damping, float brightness, float duration) {
    if (!g_synthEngine) {
        return 0;
    }

    SynthSoundEffect effect;
    effect.name = "physical_string";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::PHYSICAL;

    // Set up physical modeling parameters
    effect.physical.modelType = PhysicalParams::PLUCKED_STRING;
    effect.physical.frequency = frequency;
    effect.physical.damping = damping;
    effect.physical.brightness = brightness;

    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t synth_create_physical_bar(float frequency, float damping, float brightness, float duration) {
    if (!g_synthEngine) {
        return 0;
    }

    SynthSoundEffect effect;
    effect.name = "physical_bar";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::PHYSICAL;

    // Set up physical modeling parameters
    effect.physical.modelType = PhysicalParams::STRUCK_BAR;
    effect.physical.frequency = frequency;
    effect.physical.damping = damping;
    effect.physical.brightness = brightness;

    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t synth_create_physical_tube(float frequency, float airPressure, float brightness, float duration) {
    if (!g_synthEngine) {
        return 0;
    }

    SynthSoundEffect effect;
    effect.name = "physical_tube";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::PHYSICAL;

    // Set up physical modeling parameters
    effect.physical.modelType = PhysicalParams::BLOWN_TUBE;
    effect.physical.frequency = frequency;
    effect.physical.airPressure = airPressure;
    effect.physical.brightness = brightness;

    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

uint32_t synth_create_physical_drum(float frequency, float damping, float excitation, float duration) {
    if (!g_synthEngine) {
        return 0;
    }

    SynthSoundEffect effect;
    effect.name = "physical_drum";
    effect.duration = duration;
    effect.synthesisType = SynthesisType::PHYSICAL;

    // Set up physical modeling parameters
    effect.physical.modelType = PhysicalParams::DRUMHEAD;
    effect.physical.frequency = frequency;
    effect.physical.damping = damping;
    effect.physical.excitation = excitation;

    auto buffer = g_synthEngine->generateSound(effect);
    if (!buffer || buffer->samples.empty()) {
        return 0;
    }

    return audio_load_sound_from_buffer(buffer->samples.data(), buffer->samples.size(),
                                       buffer->sampleRate, buffer->channels);
}

// Effects control and preset management implementations

bool synth_add_effect(uint32_t soundId, const char* effectType) {
    if (!g_synthEngine || !effectType || soundId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_soundEffectsMutex);

    // Get or create effects for this sound
    EffectsParams& effects = g_soundEffects[soundId];

    // Enable the specified effect
    std::string effect = effectType;
    if (effect == "reverb") {
        effects.reverb.enabled = true;
    } else if (effect == "distortion") {
        effects.distortion.enabled = true;
    } else if (effect == "chorus") {
        effects.chorus.enabled = true;
    } else if (effect == "delay") {
        effects.delay.enabled = true;
    } else {
        return false; // Unknown effect type
    }

    return true;
}

bool synth_remove_effect(uint32_t soundId, const char* effectType) {
    if (!g_synthEngine || !effectType || soundId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_soundEffectsMutex);

    auto it = g_soundEffects.find(soundId);
    if (it == g_soundEffects.end()) {
        return false; // Sound not found
    }

    // Disable the specified effect
    std::string effect = effectType;
    if (effect == "reverb") {
        it->second.reverb.enabled = false;
    } else if (effect == "distortion") {
        it->second.distortion.enabled = false;
    } else if (effect == "chorus") {
        it->second.chorus.enabled = false;
    } else if (effect == "delay") {
        it->second.delay.enabled = false;
    } else {
        return false; // Unknown effect type
    }

    return true;
}

bool synth_set_effect_param(uint32_t soundId, const char* effectType, const char* paramName, float value) {
    if (!g_synthEngine || !effectType || !paramName || soundId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_soundEffectsMutex);

    auto it = g_soundEffects.find(soundId);
    if (it == g_soundEffects.end()) {
        return false; // Sound not found
    }

    std::string effect = effectType;
    std::string param = paramName;

    if (effect == "reverb") {
        if (param == "roomSize") it->second.reverb.roomSize = std::clamp(value, 0.0f, 1.0f);
        else if (param == "damping") it->second.reverb.damping = std::clamp(value, 0.0f, 1.0f);
        else if (param == "width") it->second.reverb.width = std::clamp(value, 0.0f, 1.0f);
        else if (param == "wet") it->second.reverb.wet = std::clamp(value, 0.0f, 1.0f);
        else if (param == "dry") it->second.reverb.dry = std::clamp(value, 0.0f, 1.0f);
        else return false;
    } else if (effect == "distortion") {
        if (param == "drive") it->second.distortion.drive = std::clamp(value, 0.0f, 2.0f);
        else if (param == "tone") it->second.distortion.tone = std::clamp(value, 0.0f, 1.0f);
        else if (param == "level") it->second.distortion.level = std::clamp(value, 0.0f, 1.0f);
        else return false;
    } else if (effect == "chorus") {
        if (param == "rate") it->second.chorus.rate = std::clamp(value, 0.1f, 10.0f);
        else if (param == "depth") it->second.chorus.depth = std::clamp(value, 0.0f, 1.0f);
        else if (param == "delay") it->second.chorus.delay = std::clamp(value, 0.001f, 0.1f);
        else if (param == "feedback") it->second.chorus.feedback = std::clamp(value, 0.0f, 0.9f);
        else if (param == "mix") it->second.chorus.mix = std::clamp(value, 0.0f, 1.0f);
        else return false;
    } else if (effect == "delay") {
        if (param == "delayTime") it->second.delay.delayTime = std::clamp(value, 0.001f, 2.0f);
        else if (param == "feedback") it->second.delay.feedback = std::clamp(value, 0.0f, 0.95f);
        else if (param == "mix") it->second.delay.mix = std::clamp(value, 0.0f, 1.0f);
        else return false;
    } else {
        return false; // Unknown effect type
    }

    return true;
}

bool synth_save_preset(const char* presetName, const char* presetData) {
    if (!presetName || !presetData) {
        return false;
    }

    try {
        // Use current directory instead of creating subdirectory to avoid permission issues
        std::string filename = std::string(presetName) + "_preset.txt";
        std::cout << "SynthEngine: Saving preset '" << presetName << "' to '" << filename << "'" << std::endl;

        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cout << "SynthEngine: Failed to open file for writing: " << filename << std::endl;
            return false;
        }

        file << presetData;
        file.close();

        std::cout << "SynthEngine: Preset saved successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cout << "SynthEngine: Exception saving preset: " << e.what() << std::endl;
        return false;
    }
}

const char* synth_load_preset(const char* presetName) {
    if (!presetName) {
        return nullptr;
    }

    try {
        std::string filename = std::string(presetName) + "_preset.txt";
        std::cout << "SynthEngine: Loading preset '" << presetName << "' from '" << filename << "'" << std::endl;

        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cout << "SynthEngine: Failed to open file for reading: " << filename << std::endl;
            return nullptr;
        }

        // Read the entire file into a static string buffer
        static std::string presetData;
        std::stringstream buffer;
        buffer << file.rdbuf();
        presetData = buffer.str();
        file.close();

        std::cout << "SynthEngine: Preset loaded successfully, size: " << presetData.length() << " bytes" << std::endl;
        return presetData.c_str();
    } catch (const std::exception& e) {
        std::cout << "SynthEngine: Exception loading preset: " << e.what() << std::endl;
        return nullptr;
    }
}

bool synth_apply_preset(uint32_t soundId, const char* presetName) {
    if (!presetName || soundId == 0) {
        return false;
    }

    // Load the preset data
    const char* presetData = synth_load_preset(presetName);
    if (!presetData) {
        return false;
    }

    // For now, implement basic preset parsing
    // A full implementation would parse JSON and apply parameters
    std::lock_guard<std::mutex> lock(g_soundEffectsMutex);

    // Create default effects for this sound
    EffectsParams& effects = g_soundEffects[soundId];

    // Simple preset system - just enable common effects with default settings
    std::string preset = presetData;
    if (preset.find("reverb") != std::string::npos) {
        effects.reverb.enabled = true;
        effects.reverb.roomSize = 0.5f;
        effects.reverb.wet = 0.3f;
    }
    if (preset.find("chorus") != std::string::npos) {
        effects.chorus.enabled = true;
        effects.chorus.rate = 1.0f;
        effects.chorus.depth = 0.3f;
    }

    return true;
}

} // extern "C"

// Advanced synthesis algorithm implementations

void SynthEngine::synthesizeAdditive(SynthAudioBuffer& buffer, const SynthSoundEffect& effect) {
    size_t frameCount = buffer.getFrameCount();
    float dt = 1.0f / buffer.sampleRate;

    for (size_t frame = 0; frame < frameCount; ++frame) {
        float time = frame * dt;
        float sample = 0.0f;

        // Generate each harmonic
        for (int i = 0; i < effect.additive.numHarmonics; ++i) {
            if (effect.additive.harmonics[i] > 0.0f) {
                float harmonic_freq = effect.additive.fundamental * (i + 1);
                float phase = 2.0f * M_PI * harmonic_freq * time + effect.additive.harmonicPhases[i];
                sample += effect.additive.harmonics[i] * std::sin(phase);
            }
        }

        // Apply envelope
        sample *= effect.envelope.getValue(time, effect.duration);

        // Write to both channels (stereo)
        for (uint32_t ch = 0; ch < buffer.channels; ++ch) {
            buffer.samples[frame * buffer.channels + ch] = sample;
        }
    }
}

void SynthEngine::synthesizeFM(SynthAudioBuffer& buffer, const SynthSoundEffect& effect) {
    size_t frameCount = buffer.getFrameCount();
    float dt = 1.0f / buffer.sampleRate;

    for (size_t frame = 0; frame < frameCount; ++frame) {
        float time = frame * dt;

        // Generate modulator signal
        float modulator = std::sin(2.0f * M_PI * effect.fm.modulatorFreq * time);

        // Apply frequency modulation to carrier
        float modulated_freq = effect.fm.carrierFreq + (effect.fm.modIndex * effect.fm.modulatorFreq * modulator);
        float carrier_phase = 2.0f * M_PI * modulated_freq * time;

        float sample = std::sin(carrier_phase);

        // Apply envelope
        sample *= effect.envelope.getValue(time, effect.duration);

        // Write to both channels (stereo)
        for (uint32_t ch = 0; ch < buffer.channels; ++ch) {
            buffer.samples[frame * buffer.channels + ch] = sample;
        }
    }
}

void SynthEngine::synthesizeGranular(SynthAudioBuffer& buffer, const SynthSoundEffect& effect) {
    size_t frameCount = buffer.getFrameCount();
    float dt = 1.0f / buffer.sampleRate;

    float grain_samples = effect.granular.grainSize * buffer.sampleRate;
    float grain_spacing = grain_samples * (1.0f - effect.granular.overlap);

    for (size_t frame = 0; frame < frameCount; ++frame) {
        float time = frame * dt;
        float sample = 0.0f;

        // Calculate which grains are active at this time
        int grain_start = static_cast<int>((frame - grain_samples) / grain_spacing);
        int grain_end = static_cast<int>(frame / grain_spacing) + 1;

        for (int g = std::max(0, grain_start); g <= grain_end; ++g) {
            float grain_start_time = g * grain_spacing * dt;
            float grain_time = time - grain_start_time;

            if (grain_time >= 0.0f && grain_time <= effect.granular.grainSize) {
                // Generate grain envelope (Hann window)
                float grain_phase = grain_time / effect.granular.grainSize;
                float grain_env = 0.5f * (1.0f - std::cos(2.0f * M_PI * grain_phase));

                // Generate grain content
                float base_freq = 440.0f * effect.granular.pitch;
                float grain_freq = base_freq * (1.0f + effect.granular.randomness * (random01() - 0.5f));
                float grain_sample = generateWaveform(effect.granular.grainWave,
                                                    2.0f * M_PI * grain_freq * grain_time, 0.5f);

                sample += grain_sample * grain_env / effect.granular.density;
            }
        }

        // Apply envelope
        sample *= effect.envelope.getValue(time, effect.duration);

        // Write to both channels (stereo)
        for (uint32_t ch = 0; ch < buffer.channels; ++ch) {
            buffer.samples[frame * buffer.channels + ch] = sample;
        }
    }
}

void SynthEngine::synthesizePhysical(SynthAudioBuffer& buffer, const SynthSoundEffect& effect) {
    size_t frameCount = buffer.getFrameCount();
    float dt = 1.0f / buffer.sampleRate;

    // Simple Karplus-Strong inspired physical modeling
    size_t delay_samples = static_cast<size_t>(buffer.sampleRate / effect.physical.frequency);
    std::vector<float> delay_line(delay_samples, 0.0f);
    size_t delay_index = 0;

    // Initial excitation
    for (size_t i = 0; i < delay_samples; ++i) {
        switch (effect.physical.modelType) {
            case PhysicalParams::PLUCKED_STRING:
                delay_line[i] = (random01() - 0.5f) * effect.physical.excitation;
                break;
            case PhysicalParams::STRUCK_BAR:
                delay_line[i] = std::sin(M_PI * i / delay_samples) * effect.physical.excitation;
                break;
            case PhysicalParams::BLOWN_TUBE:
                delay_line[i] = (random01() - 0.5f) * effect.physical.airPressure * 0.1f;
                break;
            case PhysicalParams::DRUMHEAD:
                delay_line[i] = (i < delay_samples/4) ? effect.physical.excitation : 0.0f;
                break;
        }
    }

    for (size_t frame = 0; frame < frameCount; ++frame) {
        float time = frame * dt;

        // Get delayed sample
        float delayed_sample = delay_line[delay_index];

        // Apply physical modeling filter (simple lowpass + damping)
        float filtered_sample = delayed_sample * (1.0f - effect.physical.damping);

        // Apply brightness filter
        if (effect.physical.brightness < 1.0f) {
            static float prev_sample = 0.0f;
            filtered_sample = filtered_sample * effect.physical.brightness +
                            prev_sample * (1.0f - effect.physical.brightness);
            prev_sample = filtered_sample;
        }

        // Feed back into delay line
        delay_line[delay_index] = filtered_sample;
        delay_index = (delay_index + 1) % delay_samples;

        // Apply envelope
        float sample = delayed_sample * effect.envelope.getValue(time, effect.duration);

        // Write to both channels (stereo)
        for (uint32_t ch = 0; ch < buffer.channels; ++ch) {
            buffer.samples[frame * buffer.channels + ch] = sample;
        }
    }
}
