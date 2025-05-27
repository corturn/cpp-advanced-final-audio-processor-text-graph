#ifndef MIDI_PULSE_H
#define MIDI_PULSE_H

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_utils/juce_audio_utils.h>

/* This class enables rhythms, melody patterns, and everything to do with
   parenthesis notation in my CL audio processor.

   I didn't think this feature would end up being so difficult/convoluted,
   there is just a lot of logic that needs to be tracked at runtime. With more
   time, I'd like to optimize this class. */

enum class CycleState
{
    AWAITING_NOTE_ON,
    NOTE_IS_ON
};

class MidiBeatPulseProcessor : public juce::AudioProcessor
{
public:
    MidiBeatPulseProcessor (double bpm_in = 120.0, 
                            int    beats_on_in = 1,
                            int    beats_off_in = 1)
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                           .withOutput ("Output", juce::AudioChannelSet::stereo())),
          bpm (bpm_in),
          noteNumber (60), // This note doesn't matter, but I might make it do something in the future
          beatsOn (beats_on_in > 0 ? beats_on_in : 0), 
          beatsOff (beats_off_in > 0 ? beats_off_in : 0)
    {}

    // Again, JUCE boilerplate...
    bool acceptsMidi()  const override   { return true; }
    bool producesMidi() const override   { return true; }
    void releaseResources() override {}
    juce::AudioProcessorEditor* createEditor() override          { return nullptr; }
    bool hasEditor() const override                              { return false; }
    double getTailLengthSeconds() const override                 { return 0; }
    int getNumPrograms() override                                { return 0; }
    int getCurrentProgram() override                             { return 0; }
    void setCurrentProgram (int) override                        {}
    const juce::String getProgramName (int) override             { return {}; }
    void changeProgramName (int, const juce::String&) override   {}
    void getStateInformation (juce::MemoryBlock&) override       {}
    void setStateInformation (const void*, int) override         {}

    // Getters and setters for melody tracking...
    void inc_connections () {
        num_connections++;
    }

    void inc_connections (int i) {
        num_connections += i;
    }

    int get_connections () {
        return num_connections;
    }

    void set_is_listening_velocity (bool is_listening) {
        is_listening_velocity = is_listening;
    }

    void set_listening_velocity (int velo) {
        listening_velocity = static_cast<juce::uint8>(velo);
    }

    int get_listening_velocity () {
        return static_cast<int>(listening_velocity);
    }

    void prepareToPlay (double newSampleRate, int /*samplesPerBlock*/) override
    {
        sampleRate = newSampleRate;
        if (bpm > 0.0 && newSampleRate > 0.0) 
            samplesPerBeat = static_cast<long long>((newSampleRate * 60.0) / bpm);
        else
            samplesPerBeat = 0; 

        samplesForOnDuration = beatsOn * samplesPerBeat;
        samplesForOffDuration = beatsOff * samplesPerBeat;

        globalSampleCount = 0;
        currentCycleState = CycleState::AWAITING_NOTE_ON;
        nextStateChangeGlobalSample = 0;
        ourGeneratedNoteIsOn = false;
        loopCount = 0;
        isInitialCycle = true;
        externalGatingNoteActive = false;
    }

    void processBlock (juce::AudioSampleBuffer& audio, 
                   juce::MidiBuffer&        midiMessages) override
    {
        const int blockSize = audio.getNumSamples();

        // If processor is configured to produce no rhythm, just jump to next
        if (samplesForOnDuration == 0 && samplesForOffDuration == 0) {
            globalSampleCount += blockSize;
            return;
        }

        juce::MidiBuffer processedMidi; // To build the output MIDI buffer
        auto incomingMidiIter = midiMessages.cbegin();
        const auto incomingMidiEnd = midiMessages.cend();

        for (int currentSampleInBlock = 0; currentSampleInBlock < blockSize; ++currentSampleInBlock)
        {
            // Process incoming MIDI messages to update the parent gate state (externalGatingNoteActive)
            while (incomingMidiIter != incomingMidiEnd && (*incomingMidiIter).samplePosition == currentSampleInBlock)
            {
                const auto& msg = (*incomingMidiIter).getMessage();

                if (isMidiInputGatingActive) // Only care about incoming MIDI for gating if the feature is on
                {
                    if (msg.isNoteOn()) {
                        externalGatingNoteActive = true;  // Any Note On opens the parent gate
                    }
                    else if (msg.isNoteOff()) {
                        externalGatingNoteActive = false; // Any Note Off closes the parent gate
                    }
                    else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { // I don't think this matters but just in case
                        externalGatingNoteActive = false;
                    }
                    if (is_listening_velocity && listening_velocity != msg.getVelocity()) {
                        externalGatingNoteActive = false; // For midi melody tracking
                    }
                }
                
                ++incomingMidiIter;
            }

            // Check for forced OFF due to parent gate closing
            if (isMidiInputGatingActive && ourGeneratedNoteIsOn && !externalGatingNoteActive)
            {
                processedMidi.addEvent(juce::MidiMessage::noteOff(1, this->noteNumber, velocity), currentSampleInBlock);
                ourGeneratedNoteIsOn = false;
            }

            // Process this processor's own rhythmic state changes
            while ((globalSampleCount + currentSampleInBlock) == nextStateChangeGlobalSample)
            {
                if (currentCycleState == CycleState::AWAITING_NOTE_ON) 
                {
                    if (!isInitialCycle) {
                        loopCount++;
                    }
                    isInitialCycle = false;
                    velocity = static_cast<juce::uint8>(num_connections > 0 ? loopCount % static_cast<unsigned long long>(num_connections) + 1 : 1);

                    bool permittedByParentGate = !isMidiInputGatingActive || externalGatingNoteActive;

                    if (beatsOn > 0 && permittedByParentGate)
                    {
                        if (!ourGeneratedNoteIsOn) {
                            processedMidi.addEvent(juce::MidiMessage::noteOn(1, this->noteNumber, velocity), currentSampleInBlock);
                            ourGeneratedNoteIsOn = true;
                        }
                    }
                    currentCycleState = CycleState::NOTE_IS_ON;
                    nextStateChangeGlobalSample += samplesForOnDuration;
                }
                else // currentCycleState == CycleState::NOTE_IS_ON
                {
                    if (ourGeneratedNoteIsOn)
                    {
                        processedMidi.addEvent(juce::MidiMessage::noteOff(1, this->noteNumber, velocity), currentSampleInBlock);
                        ourGeneratedNoteIsOn = false;
                    }
                    currentCycleState = CycleState::AWAITING_NOTE_ON;
                    nextStateChangeGlobalSample += samplesForOffDuration;
                }
            } 
        } 
        
        while(incomingMidiIter != incomingMidiEnd)
        {
            processedMidi.addEvent((*incomingMidiIter).getMessage(), (*incomingMidiIter).samplePosition);
            ++incomingMidiIter;
        }

        midiMessages.swapWith(processedMidi); 
        globalSampleCount += blockSize;
    }

    const juce::String getName() const override { return "Midi Pulse"; }

    void setMidiInputGatingEnabled(bool activate)
    {
        isMidiInputGatingActive = activate;
    }

    bool isMidiInputGatingCurrentlyEnabled() const
    {
        return isMidiInputGatingActive;
    }

    unsigned long long getLoopCount() const { return loopCount; }

private:
    // Settings
    double bpm;
    int    noteNumber; 
    int    beatsOn;
    int    beatsOff;

    // Internal timing state
    double sampleRate = 44100.0;
    long long samplesPerBeat = 0;
    long long samplesForOnDuration = 0;
    long long samplesForOffDuration = 0;
    long long globalSampleCount = 0;
    long long nextStateChangeGlobalSample = 0;

    // Cycle and output state
    CycleState currentCycleState = CycleState::AWAITING_NOTE_ON;
    bool ourGeneratedNoteIsOn = false; 

    // Loop counting
    unsigned long long loopCount = 0;
    bool isInitialCycle = true;

    // External midi gating state & control
    bool externalGatingNoteActive = false; 
    bool isMidiInputGatingActive = false; 

    int num_connections = 0;
    juce::uint8 velocity = 1;
    bool is_listening_velocity = false;
    juce::uint8 listening_velocity = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiBeatPulseProcessor)
};

#endif