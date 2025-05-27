#ifndef OSCILLATOR_BASE_H
#define OSCILLATOR_BASE_H

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <span>
#include <stdio.h>

using WaveformFunction = std::function<double(double)>;

class OscillatorBase : public juce::AudioProcessor
{
public:
    OscillatorBase(WaveformFunction waveformGenerator)
        : AudioProcessor (BusesProperties()
                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                           )
    {
        commonInitialization(waveformGenerator);
        setFixedMidiNote(fixedMidiNote);
    }

    OscillatorBase(WaveformFunction waveformGenerator, int initialMidiNote)
        : AudioProcessor (BusesProperties()
                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                           )
    {
        commonInitialization(waveformGenerator);
        setFixedMidiNote(initialMidiNote);
    }

    bool supportsDoublePrecisionProcessing() const override { return true; }

    void setMidiTriggered(bool shouldBeTriggered)
    {
        if (midiTriggered == shouldBeTriggered)
            return;

        midiTriggered = shouldBeTriggered;
        oscillator.setFrequency(fixedFrequency, true); 

        if (!midiTriggered)
        {
            isPlaying = true; 
            gain.setGainLinear(gain_val); 
        }
        else
        {
            gain.setGainLinear(0.0); 
            isPlaying = false;
        }
    }

    bool isMidiTriggered() const
    {
        return midiTriggered;
    }

    void setFixedMidiNote(int newMidiNote)
    {
        // Clamp MIDI note to valid range
        fixedMidiNote = juce::jlimit(0, 127, newMidiNote); 
        fixedFrequency = midiNoteToHz(fixedMidiNote);
        // Only update the dsp::Oscillator if sampleRate is valid (i.e., after prepareToPlay)
        if (sampleRate > 0.0) 
            oscillator.setFrequency(fixedFrequency, true); 
    }

    void set_velocity(int new_velocity) {
        velocity = static_cast<juce::uint8>(new_velocity);
    }

    void set_open_on_all_channels(bool is_open) {
        open_on_all_channels = is_open;
    }
    
    int getFixedMidiNote() const
    {
        return fixedMidiNote;
    }

    double getFixedFrequency() const
    {
        return fixedFrequency;
    }

    void prepareToPlay (double newSampleRate, int samplesPerBlock) override
    {
        sampleRate = newSampleRate;

        juce::dsp::ProcessSpec spec { sampleRate,
                                      static_cast<juce::uint32> (samplesPerBlock),
                                      static_cast<juce::uint32> (getTotalNumOutputChannels()) };

        oscillator.prepare (spec);
        gain.prepare(spec);
        gain.setRampDurationSeconds(0.005); 

        // Always set the oscillator to its fixed frequency (derived from fixedMidiNote)
        // This call is important here as setFixedMidiNote might have been called before prepareToPlay
        oscillator.setFrequency(fixedFrequency, true); 

        if (!midiTriggered) { // Drone mode
            gain.setGainLinear(gain_val); 
            isPlaying = true; 
        } else { // MIDI triggered mode
            gain.setGainLinear(0.0); 
            isPlaying = false;
        }
        oscillator.reset(); 
        gain.reset();
    }

    void processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages) override
    {
        buffer.clear();
        if (sampleRate <= 0.0) { // Should not happen if prepareToPlay was called
            buffer.clear();
            return;
        }

        const int numSamples = buffer.getNumSamples();
        juce::dsp::AudioBlock<double> processingBlock (buffer);
        
        int currentSample = 0; 
        for (const auto meta : midiMessages)
        {
            const juce::MidiMessage& msg = meta.getMessage();
            const int msgSample  = juce::jlimit (0, numSamples - 1 , meta.samplePosition);

            if (msgSample > currentSample) {
                 render(processingBlock, currentSample, msgSample);
            }
            handleMidi (msg); 
            currentSample = msgSample; 
        }
        
        if (currentSample < numSamples) {
            render (processingBlock, currentSample, numSamples);
        }
    }

    // JUCE boilerplate AudioProcessor methods
    const juce::String getName() const override                  { return "Oscillator Base"; }
    juce::AudioProcessorEditor* createEditor() override          { return nullptr; }
    bool hasEditor() const override                              { return false; }
    bool acceptsMidi() const override                            { return true; } 
    bool producesMidi() const override                           { return false; }
    double getTailLengthSeconds() const override                 { return 0.01; } 
    int getNumPrograms() override                                { return 1; }
    int getCurrentProgram() override                             { return 0; }
    void setCurrentProgram (int) override                        {}
    const juce::String getProgramName (int) override             { return juce::String(); }
    void changeProgramName (int, const juce::String&) override   {}
    void getStateInformation (juce::MemoryBlock&) override       {}
    void setStateInformation (const void*, int) override         {}
    
    // Dummy implementation for float processBlock is necessary for ABC
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    void releaseResources() override                             {}

protected:
    // Converts a MIDI note number to frequency in Hz.
    static double midiNoteToHz (int midiNote) {
        return 440.0 * std::pow (2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
    }

    void handleMidi (const juce::MidiMessage& m) {
        if (!midiTriggered || (velocity != m.getVelocity() && !open_on_all_channels)) 
        {
            return; 
        }

        if (m.isNoteOn()) {
            gain.setGainLinear (gain_val); 
            isPlaying = true; 
        } else if (m.isNoteOff()) {
            gain.setGainLinear (0.0); 
            isPlaying = false; 
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            gain.setGainLinear (0.0); 
            isPlaying = false;
        }
    }

    virtual void render (juce::dsp::AudioBlock<double>& block, int startSample, int endSample) {
        if (startSample >= endSample) return; 

        auto subBlock = block.getSubBlock (static_cast<size_t>(startSample),
                                           static_cast<size_t>(endSample - startSample));
        juce::dsp::ProcessContextReplacing<double> ctx (subBlock); 

        bool shouldProcessAudio;
        if (!midiTriggered) { 
            shouldProcessAudio = true; 
        } else { 
            shouldProcessAudio = (isPlaying || gain.isSmoothing()); 
        }

        if (shouldProcessAudio) {
            oscillator.process (ctx); 
            gain.process (ctx); 
        } else {
            subBlock.clear(); 
        }
    }

    juce::dsp::Oscillator<double> oscillator;
    juce::dsp::Gain<double>       gain;
    
    double                       sampleRate = 0.0; 
    bool                         isPlaying  = false; 

    bool                         midiTriggered = false; 
    int                          fixedMidiNote = 69; // Default MIDI note (A4)
    double                       fixedFrequency;
    double                       gain_val = 0.5;

    juce::uint8 velocity = 1;
    bool open_on_all_channels = false;

private:
    void commonInitialization(WaveformFunction waveformGenerator)
    {
        this->oscillator.initialise (waveformGenerator, 128);
    }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscillatorBase)
};

class SinOsc : public OscillatorBase
{
public:
    SinOsc() 
      : OscillatorBase([](double phase) { return std::sin(phase); })
    {}

    SinOsc(int initialMidiNote)
      : OscillatorBase([](double phase) { return std::sin(phase); }, initialMidiNote)
    {}

    const juce::String getName() const override { return "Sine Oscillator"; }
};

class SquareOsc : public OscillatorBase
{
public:
    SquareOsc()
        : OscillatorBase([](double phase) { return (phase < 0) ? 1.0 : -1.0; })
    {gain_val = 0.05;}

    SquareOsc(int initialMidiNote)
        : OscillatorBase([](double phase) { return (phase < 0) ? 1.0 : -1.0; }, initialMidiNote)
    {gain_val = 0.05;}

    const juce::String getName() const override { return "Square Oscillator"; }

};

class SawOsc : public OscillatorBase
{
public:
    SawOsc()
        : OscillatorBase([](double phase) { return phase / juce::MathConstants<double>::pi; })
    {gain_val = 0.15;}

    SawOsc(int initialMidiNote)
        : OscillatorBase([](double phase) { return phase / juce::MathConstants<double>::pi; }, initialMidiNote)
    {gain_val = 0.15;}


    const juce::String getName() const override { return "Sawtooth Oscillator"; }
};

// Band-limited triangle
inline double triangleBL (double x) noexcept
{
    constexpr double norm = 8.0 / (juce::MathConstants<double>::pi
                                   * juce::MathConstants<double>::pi);

    constexpr int numHarmonics = 64;

    double sum = 0.0;
    for (int k = 0; k < numHarmonics; ++k)
    {
        const int    n = 2 * k + 1;
        const double s = std::sin (n * x);
        sum += (std::pow (-1.0, k) / (n * n)) * s;
    }
    return norm * sum;
}


class TriangleOsc : public OscillatorBase
{
public:
    TriangleOsc()
        : OscillatorBase([](double phase) 
          {
            return triangleBL(phase); 
          }
        )
    {}

    TriangleOsc(int initialMidiNote)
        : OscillatorBase([](double phase) 
          {
            return triangleBL(phase); 
          },
          initialMidiNote
        )
    {}

    const juce::String getName() const override { return "Triangle Oscillator"; }
};

class NoiseOsc : public OscillatorBase
{
public:
    NoiseOsc()
        : OscillatorBase([](double) { return 0.0; }) 
    {gain_val = 0.02;}

    NoiseOsc(int initialMidiNote)
        : OscillatorBase([](double) { return 0.0; }, initialMidiNote)
    {gain_val = 0.02;}


    const juce::String getName() const override { return "Noise Oscillator"; }

protected:
    void render (juce::dsp::AudioBlock<double>& block, int startSample, int endSample) override
    {
        if (startSample >= endSample) return;

        auto subBlock = block.getSubBlock (static_cast<size_t>(startSample),
                                           static_cast<size_t>(endSample - startSample));
        
        juce::dsp::ProcessContextReplacing<double> gainContext (subBlock); 

        bool shouldProcessAudio;
        if (!isMidiTriggered()) { 
            shouldProcessAudio = true; 
        } else {
            shouldProcessAudio = (this->isPlaying || this->gain.isSmoothing()); 
        }

        if (shouldProcessAudio)
        {
            for (size_t channel = 0; channel < subBlock.getNumChannels(); ++channel)
            {
                for (size_t i = 0; i < subBlock.getNumSamples(); ++i)
                {
                    subBlock.setSample(static_cast<int>(channel), static_cast<int>(i), random.nextDouble() * 2.0 - 1.0);
                }
            }
            gain.process (gainContext); 
        }
        else
        {
            subBlock.clear();
        }
    }

private:
    juce::Random random;
};

#endif
