# Text Graph Synth & Sequencer

My program is a highly customizable text to musical sequence parser, that takes
in compile-time randomized or runtime user-specified binds between letters and
audio processors with customizable (and also compile-time randomized) parameters,
along with the specific order of letters, how they are divided into words, as well
as a parenthetical syntax that allows for generating "pulses" as an alternative to
holding out notes indefinitely. From a sequence of letters, the program builds an
audio processor graph which is ultimately connected out to your speakers.

The audio processor nodes are fairly rudimentary at the moment, but the LetterRegistry
and TypeTable data structures makes adding new nodes very straightforward and minimizes
code duplication.

In the root directory, build with:
```
cmake --build build
```
This may take a bit of time as it needs to download the dependent libraries.

Execution:

There are two modes to run the program—interactive mode and file mode. Personally,
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

