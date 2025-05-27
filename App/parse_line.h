#ifndef PARSE_LINE_H
#define PARSE_LINE_H

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <stdio.h>
#include <vector>

#include "letter_binds.h"
#include "oscillators.h"
#include "effects.h"
#include "midi_pulse.h"

static auto is_effect(juce::AudioProcessorGraph::Node::Ptr node) {
    return dynamic_cast<EffectsBase*>(node->getProcessor()) != nullptr;
}

static auto is_osc(juce::AudioProcessorGraph::Node::Ptr node) {
    return dynamic_cast<OscillatorBase*>(node->getProcessor());
}

static auto is_midi(juce::AudioProcessorGraph::Node::Ptr node) {
    return dynamic_cast<MidiBeatPulseProcessor*>(node->getProcessor());
}

class Parser {
public:

    Parser(std::shared_ptr<juce::AudioProcessorGraph> graph_in, LetterRegistry &reg_in) : graph(graph_in), reg(reg_in) {
        audioOut = graph->addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>
        (juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    }

    void clear_graph() {
        graph->clear();
        graph->rebuild();
        audioOut = graph->addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>
                                (juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    }

    void connect(juce::AudioProcessorGraph::Node::Ptr n1, juce::AudioProcessorGraph::Node::Ptr n2) {
        graph->addConnection ({ {n1->nodeID, 0}, {n2->nodeID, 0} }); // left
        graph->addConnection ({ {n1->nodeID, 1}, {n2->nodeID, 1} }); // right
    }
    void connect_midi_direct(juce::AudioProcessorGraph::Node::Ptr n1, juce::AudioProcessorGraph::Node::Ptr n2) {
        graph->addConnection({ {n1->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                               {n2->nodeID, juce::AudioProcessorGraph::midiChannelIndex} });
        if (auto* osc = is_osc(n2)) {
            osc->setMidiTriggered(true);
            osc->set_open_on_all_channels(true);
        }
        else if (auto* midi = is_midi(n2)) {
            midi->setMidiInputGatingEnabled(true);
            midi->set_is_listening_velocity(false);
        }
    }

    void connect_midi(juce::AudioProcessorGraph::Node::Ptr n1, juce::AudioProcessorGraph::Node::Ptr n2, bool need_to_inc) {
        graph->addConnection({ {n1->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                               {n2->nodeID, juce::AudioProcessorGraph::midiChannelIndex} });
        if (auto* osc = is_osc(n2)) {
            osc->setMidiTriggered(true);
            auto *m = is_midi(n1);
            if (need_to_inc)
                m->inc_connections();
            auto connection = m->get_connections();
            osc->set_open_on_all_channels(false);
            osc->set_velocity(connection);
        }
        else if (auto* midi = is_midi(n2)) {
            midi->setMidiInputGatingEnabled(true);
            auto *m = is_midi(n1);
            if (need_to_inc)
                m->inc_connections();
            auto connection = m->get_connections();
            midi->set_listening_velocity(connection);
            midi->set_is_listening_velocity(true);
        }

    }

    void initialize_word(std::string const &s) {

        bool need_to_inc = true;

        std::vector<juce::AudioProcessorGraph::Node::Ptr> orphans;

        juce::AudioProcessorGraph::Node::Ptr current_node;
        juce::AudioProcessorGraph::Node::Ptr effects_tail = nullptr;

        bool prev_was_midi = false;

        for (std::string::const_iterator it = s.begin(); it != s.end(); ++it) {
            if (*it == '(') {
                paren_depth++;
                prev_was_midi = false;
                need_to_inc = true;

                continue;
            }

            if (*it == ')') {
                paren_depth--;
                prev_was_midi = false;
                midi_pulsers.pop_back();
                continue;
            }

            current_node = graph->addNode (reg.initialize(*it));

            if (prev_was_midi) {
                connect_midi_direct(midi_pulsers.back(), current_node);
            }

            if (paren_depth > 0) {
                connect_midi(midi_pulsers[paren_depth - 1], current_node, need_to_inc);
                need_to_inc = false;
            }

            if (is_osc(current_node)) {
                orphans.push_back(current_node);
                prev_was_midi = false;
            }
            else if (is_effect(current_node)) {
                for (auto orphan : orphans) {
                    connect(orphan, current_node);
                    prev_was_midi = false;
                }
                orphans.clear();
                if (effects_tail) {
                    connect(effects_tail, current_node);
                }
                effects_tail = current_node;
            }
            else if (is_midi(current_node)) {
                prev_was_midi = true;
                midi_pulsers.push_back(current_node);
            }
        }
        if (effects_tail)
            connect(effects_tail, audioOut);
        for (auto orphan : orphans) {
            connect(orphan, audioOut);
        }
    }

    void parse_and_initialize(const std::string& line) {
        std::istringstream stream(line);
        std::string word;

        while (stream >> word) {
            initialize_word(word);
        }
    }

    std::shared_ptr<juce::AudioProcessorGraph> graph;
    LetterRegistry &reg;
    juce::AudioProcessorGraph::Node::Ptr audioOut;
    std::vector<juce::AudioProcessorGraph::Node::Ptr> midi_pulsers;
    size_t paren_depth = 0;
};


#endif