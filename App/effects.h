#ifndef EFFECTS_PROCESSOR_H
#define EFFECTS_PROCESSOR_H

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <stdio.h>

class EffectsBase  : public juce::AudioProcessor
{
public:
    //==============================================================================
    EffectsBase()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                           .withOutput ("Output", juce::AudioChannelSet::stereo()))
    {}

    void processBlock (juce::AudioBuffer<float>& /*buffer*/, juce::MidiBuffer& /*midiMessages*/) override
    {
    }

    // Unlike oscillators, effects demand more custom processing
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override = 0;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override = 0;
    void releaseResources() override {}

    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override          { return nullptr; }
    bool hasEditor() const override                              { return false; }

    const juce::String getName() const override                  { return {}; }
    bool acceptsMidi() const override                            { return false; }
    bool producesMidi() const override                           { return false; }
    double getTailLengthSeconds() const override                 { return 0; }

    int getNumPrograms() override                                { return 1; }
    int getCurrentProgram() override                             { return 0; }
    void setCurrentProgram (int) override                        {}
    const juce::String getProgramName (int) override             { return {}; }
    void changeProgramName (int, const juce::String&) override   {}

    void getStateInformation (juce::MemoryBlock&) override       {}
    void setStateInformation (const void*, int) override         {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsBase) // Renamed
};

class FilterProcessor  : public EffectsBase
{
public:
    FilterProcessor() : initialCutoffFreq (2000.0)
    {
    }

    FilterProcessor(double cutoffFrequency) : initialCutoffFreq (cutoffFrequency)
    {}

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        *filter.state = *juce::dsp::IIR::Coefficients<double>::makeLowPass (sampleRate, initialCutoffFreq);

        auto numChannels = getTotalNumOutputChannels();
        if (numChannels == 0) numChannels = 2; // Default to stereo

        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (samplesPerBlock), static_cast<juce::uint32>(numChannels) }; 
        filter.prepare (spec);
    }

    void processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& /*midiMessages*/) override
    {
        juce::dsp::AudioBlock<double> block (buffer);
        juce::dsp::ProcessContextReplacing<double> context (block);
        filter.process (context);
    }

    void reset() override
    {
        filter.reset();
    }

    const juce::String getName() const override { return "Filter (Double)"; }

private:
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<double>, juce::dsp::IIR::Coefficients<double>> filter;
    double initialCutoffFreq = 2000.0;
};

 // These do, unfortunately, have to be floats, unless
 // I wanted to create my own reverb algorithm which
 // is very complicated and out of the project scope
 // at the moment. I have my own API use doubles, but
 // translate internally

class ReverbProcessor : public EffectsBase
{
public:
    ReverbProcessor(double size, double damp, double wet,
                    double dry, double width)
    {
        params.roomSize   = juce::jlimit(0.0f, 1.0f, static_cast<float>(size));
        params.damping    = juce::jlimit(0.0f, 1.0f, static_cast<float>(damp));
        params.wetLevel   = juce::jlimit(0.0f, 1.0f, static_cast<float>(wet));
        params.dryLevel   = juce::jlimit(0.0f, 1.0f, static_cast<float>(dry));
        params.width      = juce::jlimit(0.0f, 1.0f, static_cast<float>(width));
        params.freezeMode = 0.0f;
    }

    void prepareToPlay (double sampleRate, int blockSize) override
    {
        // Set parameters on the internal reverb unit.
        reverb.setParameters (params);

        auto numCh = juce::jmax (2, getTotalNumOutputChannels());
        if (numCh == 0) numCh = 2;

        juce::dsp::ProcessSpec spec { sampleRate,
                                      static_cast<juce::uint32> (blockSize),
                                      static_cast<juce::uint32> (numCh) };
        reverb.prepare (spec);

        tempFloat.setSize (numCh, blockSize, false, false, true);
    }

    void processBlock (juce::AudioBuffer<double>& buffer,
                       juce::MidiBuffer& /*midiMessages*/) override
    {
        const int numCh      = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (tempFloat.getNumChannels() != numCh ||
            tempFloat.getNumSamples()  != numSamples)
            tempFloat.setSize (numCh, numSamples, false, false, true);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const double* src = buffer.getReadPointer (ch);
            float* dst = tempFloat.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i) {
                dst[i] = static_cast<float> (src[i]);
            }
        }

        // Process the audio using juce::dsp::Reverb (operates on floats).
        // Create a juce::dsp::AudioBlock referencing the temporary float buffer.
        juce::dsp::AudioBlock<float> floatBlock (tempFloat);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (floatBlock));

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = tempFloat.getReadPointer (ch);
            double* dst = buffer.getWritePointer(ch);

            for (int i = 0; i < numSamples; ++i)
                dst[i] = static_cast<double> (src[i]);
        }
    }

    void reset() override
    {
        reverb.reset();
    }

    const juce::String getName() const override { return "Reverb"; }

    void setReverbParameters (const juce::dsp::Reverb::Parameters& newParams)
    {
        params.roomSize   = juce::jlimit(0.0f, 1.0f, newParams.roomSize);
        params.damping    = juce::jlimit(0.0f, 1.0f, newParams.damping);
        params.wetLevel   = juce::jlimit(0.0f, 1.0f, newParams.wetLevel);
        params.dryLevel   = juce::jlimit(0.0f, 1.0f, newParams.dryLevel);
        params.width      = juce::jlimit(0.0f, 1.0f, newParams.width);
        params.freezeMode = juce::jlimit(0.0f, 1.0f, newParams.freezeMode);

        reverb.setParameters (params);
    }

private:
    juce::dsp::Reverb             reverb;    // JUCE stock reverb, operates on floats.
    juce::dsp::Reverb::Parameters params;    // Stores the current reverb parameters.
    juce::AudioBuffer<float>      tempFloat; // Temporary buf for double-to-float and float-to-double conversion.
};


class DelayProcessor : public EffectsBase
{
public:
    DelayProcessor(double delay, double fb, double wet, double dry)
        : delayTimeSeconds(delay), feedback(fb), wetLevel(wet), dryLevel(dry), currentSampleRate(48000.0)
    {
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;
        
        auto numChannels = getTotalNumOutputChannels();
        if (numChannels == 0) numChannels = 2;

        delayLines.clear();
        for (int i = 0; i < numChannels; ++i)
        {
            // Max delay of 2 seconds.
            delayLines.emplace_back(static_cast<int>(sampleRate * 2.0)); 
        }

        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(samplesPerBlock), static_cast<juce::uint32>(numChannels) };

        for (auto& dl : delayLines)
        {
            dl.prepare(spec);
            dl.setDelay(sampleRate * delayTimeSeconds); 
        }
    }

    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& /*midiMessages*/) override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        // Ensure we have a delay line for each channel.
        if (delayLines.size() != static_cast<size_t>(numChannels))
        {
            // If the number of channels changed, prepareToPlay should have been called.
            // If it wasn't, this is an unexpected state.
        }


        for (int channel = 0; channel < numChannels; ++channel)
        {
            // Ensure we only access valid delay lines
            if (static_cast<size_t>(channel) < delayLines.size()) 
            {
                auto* channelData = buffer.getWritePointer(channel);
                auto& delayLine = delayLines[static_cast<size_t>(channel)];

                for (int sample = 0; sample < numSamples; ++sample)
                {
                    const double drySample = channelData[sample];
                    const double delayedSample = delayLine.popSample(0); 

                    const double outputSample = (drySample * dryLevel) + (delayedSample * wetLevel);
                    
                    // Calculate sample to push back into delay line (input + feedback from delayed signal)
                    const double sampleToPush = drySample + (delayedSample * feedback);
                    
                    delayLine.pushSample(0, sampleToPush); 
                    channelData[sample] = outputSample;
                }
            }
        }
    }

    void reset() override
    {
        for (auto& dl : delayLines)
        {
            dl.reset();
        }
    }

    const juce::String getName() const override { return "Delay"; }

    void setDelayTimeSeconds(double newDelayTime)
    {
        delayTimeSeconds = juce::jmax(0.0, newDelayTime); // Ensure positive delay time
        if (currentSampleRate > 0)
        {
            for (auto& dl : delayLines)
            {
                dl.setDelay(currentSampleRate * delayTimeSeconds);
            }
        }
    }

    void setFeedback(double newFeedback)
    {
        // Clamp feedback to 0.99 to avoid pain and destruction
        feedback = juce::jlimit(0.0, 0.99, newFeedback);
    }

    void setWetLevel(double newWetLevel)
    {
        wetLevel = juce::jlimit(0.0, 1.0, newWetLevel);
    }

    void setDryLevel(double newDryLevel)
    {
        dryLevel = juce::jlimit(0.0, 1.0, newDryLevel);
    }


private:
    std::vector<juce::dsp::DelayLine<double, juce::dsp::DelayLineInterpolationTypes::Linear>> delayLines; 

    double delayTimeSeconds;
    double feedback;
    double wetLevel;
    double dryLevel;
    double currentSampleRate;
};


#endif
