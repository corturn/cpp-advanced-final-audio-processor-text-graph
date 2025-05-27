# Text Graph Synth & Sequencer

Overview:

My program is a highly customizable text to musical sequence parser, that takes
compile-time randomized or runtime user-specified binds between letters and
audio processors with customizable (and also compile-time randomized) parameters,
and a user-specified string of these letters, parsing their order, how they are
divided into words, as well as a parenthetical syntax that allows for generating
"pulses" as an alternative to holding out notes indefinitely. From this sequence
of letters and parenthesis, the program builds a graph of audio processor nodes
which is ultimately connected out to your speakers.

The audio processor nodes are fairly rudimentary at the moment, but the LetterRegistry
and TypeTable data structures makes adding new nodes very straightforward and minimizes
code duplication.


Types Overview:

This is a lot to remember, which is why the example files are provided. Those files are probably a more fun way to digest this.

    - Oscillator types - defined in oscillators.h:
        - sin
            - note
        - triangle
            - note
        - saw
            - note
        - square
            - note
        - noise
            - note
    - Effects types - defined in effects.h:
        - filter
            - cuttoff
        - reverb
            - size
            - damp
            - wet
            - dry
            - width
        - delay
            - time
            - feedback
            - wet
            - dry
    - Midi Pulse type - defined in midi_pulse.h
        - midi
            - bpm
            - on
            - off


Text parsing:

In /examples/example2.txt I PLAY the below string:
```
"y(i(love)s x(chord)s) ke"
```
'y' is a midi pulse type that alternates between triggering its two sub-elements
'i' and 'j'. 'i' and 'j' are also midi pulse types that trigger their respective
combinations of oscillators and effects 'l' 'o' 'v' 'e' and 'c' 'h' 'o' 'r' 'd'.
's' is a reverb effect applied to the entirety of both words. Only when BOTH
'y' is ON for one of its sub-elements and that sub-element is ON does the sound
get triggered. This enables complex rhythms from simple building blocks.

'k' is a bass note fed into a filter type 'e' to soften its harsh higher register tones.

Each letter was SET in the previous instructions to establish the binds between
letter and type. Note how I can generate any number of a certain letter's type.
A letter is not bound to a certain node, and can be initialized and behave
independently with any number of instances.


Files:

    effects.h       - classes for audio processors that do not produce sound on their
                    own, but ingest and manipulate sound
    letter_binds.h  - letter : type binding, mapping names to types and to parameters
                    and their types, compile-time randomization logic, PRINT logic
                    to display binds
    Main.cpp        - input processing for interactive and file modes, performs basic
                    parsing to directs commands to proper handlers, initializes graph
    midi_pulse.h    - processor that sends midi signals to trigger sounds on/off
                    in rhythmic loops
    oscillators.h   - classes for audio processors that do produce sound
    parse_line.h    - logic for runtime parsing and converting input string to graph,
                    leverages convenient syntax provided by LetterRegistry to
                    initialize a node given its bound character (reg.initialize(*it))
    user_input.h    - RegexFunctor class that is used briefly, more for fun than practicality


In the root directory, build with:
```
cmake <-G generator - if necessary/desired for your system> -B build
cmake --build build
```
This may take a bit of time as it needs to download the dependent libraries.

Execution:

There are two modes to run the program--interactive mode and file mode. Personally,
I recommend file mode as it can be easier to interact with/adjust in case of typos
and because I've provided two ready-to-run examples that demonstrate the basic
patterns of the musical language.

To run in interactive mode (not necessarily recommended at first), still in the 
root directory execute:
```
./build/App/ConsoleAppMessageThread_artefacts/ConsoleAppMessageThread
```

To run in file mode (definitely recommended), instead run:
```
./build/App/ConsoleAppMessageThread_artefacts/ConsoleAppMessageThread <file-path>
```

You can run the examples I made with:
```
./build/App/ConsoleAppMessageThread_artefacts/ConsoleAppMessageThread ./App/examples/example1.txt
```
and
```
./build/App/ConsoleAppMessageThread_artefacts/ConsoleAppMessageThread ./App/examples/example2.txt
```

Thank you for two wonderful quarters of C++!

