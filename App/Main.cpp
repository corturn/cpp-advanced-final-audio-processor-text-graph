#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>
#include <algorithm>
#include <string>
#include <cctype>
#include <fstream>
#include <filesystem> 

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "oscillators.h"
#include "user_input.h"
#include "effects.h"
#include "midi_pulse.h"
#include "letter_binds.h"
#include "parse_line.h"

static std::atomic_bool keepRunning { true };

static void signalHandler (int)
{
    keepRunning = false;
}

static void printGraphStructure (std::shared_ptr<juce::AudioProcessorGraph> graph)
{
    std::cout << "=== Nodes ===\n";
    for (auto* node : graph->getNodes())
    {
        std::cout << "Node ID: " << static_cast<int> (node->nodeID.uid)
                  << ", Processor: " << node->getProcessor()->getName()
                                         .toStdString()               << '\n';
    }

    std::cout << "=== Connections ===\n";
    for (const auto& c : graph->getConnections())
    {
        std::cout << "From Node " << static_cast<int> (c.source.nodeID.uid)
                  << " [ch "      << c.source.channelIndex      << "]  →  "
                  << "Node "      << static_cast<int> (c.destination.nodeID.uid)
                  << " [ch "      << c.destination.channelIndex << "]\n";
    }
}

struct InputProcessor {
    InputProcessor(LetterRegistry &reg_in, Parser &parse_in, std::shared_ptr<juce::AudioProcessorGraph> graph_in) :
        reg(reg_in), parse(parse_in), graph(graph_in) {}

    void process_line(std::string line) {
        bool set_command = line.starts_with("SET");
        bool play_command = line.starts_with("PLAY");
        bool pause_command = line.starts_with("PAUSE");
        bool print_command = line.starts_with("PRINT");

        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return std::tolower(c); });
        if (set_command) {
            execute_bind_command(reg, line);
        } else if (play_command) {
            parse.clear_graph();
            parse.parse_and_initialize(saved_graph);
            printGraphStructure(graph);
        } else if (pause_command) {
            parse.clear_graph();
        } else if (print_command) {
            if (line.find("v") != std::string::npos) {
                reg.printBindingsDetailed();
            } else {
                reg.printBindings();
            }
        } else {
            // This regex is not necessary and is totally overkill, I just
            // wrote this class when first starting the project and thought
            // it was cool. Rule 1 of optimization... don't?
            RegexFunctor<"\"([^\"]*)\""> graph_str_match(line);
            if (auto m = graph_str_match()) {
                auto s = (*m)[1].str();
                saved_graph = s;
            }
        }
    }

    LetterRegistry &reg;
    Parser &parse;
    std::shared_ptr<juce::AudioProcessorGraph> graph;
    std::string saved_graph;
};

static void file_mode(std::string const &filename, LetterRegistry &reg, Parser &parse, std::shared_ptr<juce::AudioProcessorGraph> graph) {
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Cannot open command file '" << filename << "'\n";
        return;
    }

    std::cout << "Running commands from '" << filename << "' …\n";

    InputProcessor ip(reg, parse, graph);

    std::string line;
    while (keepRunning.load(std::memory_order_relaxed) &&
        std::getline(file, line))
    {
        // “EXIT” inside file quits run early
        if (line == "EXIT" || line == "exit")
        {
            std::cout << "'EXIT' directive found in file - stopping.\n";
            keepRunning.store(false, std::memory_order_relaxed);
            break;
        }

        ip.process_line(line);
    }

    std::cout << "[File mode] Finished processing '" << filename << "'. Type EXIT to stop.\n";

    while (keepRunning) {
        std::getline(std::cin, line);
        if (line == "EXIT") {
            std::cout << "'EXIT' command received. Signaling stop." << std::endl;
            keepRunning.store(false, std::memory_order_relaxed); // Signal to stop
            break;
        }
    }
}

static void interactive_mode(LetterRegistry &reg, Parser &parse, std::shared_ptr<juce::AudioProcessorGraph> graph) {
    std::cout << "| Hello! This is interactive mode. Commands:" << std::endl;
    std::cout << "|   Bind a letter:" << std::endl;
    std::cout << "|       SET <letter> <type> <parameter> <value>...  <- specify types and specific parameters" << std::endl;
    std::cout << "|           e.g.: SET a sin note 66" << std::endl;
    std::cout << "|           e.g.: SET a delay time 0.5 feedback 0.4" << std::endl;
    std::cout << "|       SET <letter> <type>                         <- specifies just type, default parameters are selected" << std::endl;
    std::cout << "|           e.g.: SET a delay" << std::endl;
    std::cout << "|   Generate a graph:" << std::endl;
    std::cout << "|       \"h (el lo)\"                               <- generates a graph with your specified letter bindings." << std::endl;
    std::cout << "|                                                      Parenthesis have to do with rhythm, so try binding a letter like:" << std::endl;
    std::cout << "|                                                      SET x midi and then generate something like x (a b)" << std::endl;
    std::cout << "|   Play/Pause your graph:" << std::endl;
    std::cout << "|       PLAY" << std::endl;
    std::cout << "|       PAUSE" << std::endl;
    std::cout << "|   Print your current letter : type bindings:" << std::endl;
    std::cout << "|       PRINT" << std::endl;
    std::cout << "|       PRINT v                                     <- verbose print includes all parameters and their defaults" << std::endl;

    std::string line;

    auto ip = InputProcessor(reg, parse, graph);

    while (keepRunning.load(std::memory_order_relaxed)) {
        std::cout << "cmd> " << std::flush;
        if (std::getline(std::cin, line)) {
            if (line == "EXIT") {
                std::cout << "'EXIT' command received. Signaling stop." << std::endl;
                keepRunning.store(false, std::memory_order_relaxed); // Signal to stop
                break;
            }

            ip.process_line(line);

        } else {
            if (std::cin.eof()) {
                std::cout << "[Input Thread] EOF detected on console input. Exiting input loop." << std::endl;
            } else if (std::cin.bad()) {
                std::cerr << "[Input Thread] Fatal error on std::cin. Exiting input loop." << std::endl;
            } else if (std::cin.fail()) {
                std::cerr << "[Input Thread] Non-fatal error on std::cin. Attempting to clear flags." << std::endl;
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
            keepRunning.store(false, std::memory_order_relaxed); // Signal to stop
            break;
        }
    }
    std::cout << "[Input Thread] Loop finished. Thread is now terminating." << std::endl;
}

int main(int argc, char* argv[])
{
    std::signal (SIGINT, signalHandler);

    juce::AudioDeviceManager deviceManager;
    if (auto err = deviceManager.initialise (0, 2, nullptr, true); err.isNotEmpty())
    {
        std::cerr << "Audio error: " << err << std::endl;
        return 1;
    }
    juce::AudioProcessorPlayer player;
    player.setDoublePrecisionProcessing(true);

    auto graph = std::make_shared<juce::AudioProcessorGraph>();

    auto inputDevice  = juce::MidiInput::getDefaultDevice();
    auto outputDevice = juce::MidiOutput::getDefaultDevice();

    graph->enableAllBuses();

    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&player);
    deviceManager.setMidiInputDeviceEnabled (inputDevice.identifier, true);
    deviceManager.addMidiInputDeviceCallback (inputDevice.identifier, &player);
    deviceManager.setDefaultMidiOutputDevice (outputDevice.identifier);

    graph->setPlayConfigDetails (graph->getMainBusNumInputChannels(), 
                                 graph->getMainBusNumOutputChannels(),
                                 graph->getSampleRate(),
                                 graph->getBlockSize());

    LetterRegistry reg;
    Parser parse(graph, reg);

    bind_all_letters_and_params_random(reg);

    reg.printBindingsDetailed();
    

    player.setProcessor (graph.get());
    
    if (argc <= 1) {
        interactive_mode(reg, parse, graph);
    } else {
        file_mode(std::string(argv[1]), reg, parse, graph);
    }

    std::cout << "Stopping …\n";
    deviceManager.removeAudioCallback (&player);
    player.setProcessor (nullptr);
    deviceManager.closeAudioDevice();
    return 0;
}
