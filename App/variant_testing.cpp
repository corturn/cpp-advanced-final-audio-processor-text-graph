#include <unordered_map>
#include <functional>
#include <memory>
#include <iostream>     // only for the demo's std::cout

#include "oscillators.h"

// Type alias: any factory that makes an OscillatorBase-derived object
using OscFactory = std::function<std::unique_ptr<OscillatorBase>()>;

// Global map  letter → factory
static std::unordered_map<char, OscFactory> gOscTable;

/* Register or update a binding
 *   bindOsc('a', []{ return std::make_unique<SinOsc>(); });
 */
void bindOsc(char letter, OscFactory f)
{
    gOscTable[letter] = std::move(f);
}

/* Retrieve a *new* oscillator instance, built according to the
   current binding for that letter. */
std::unique_ptr<OscillatorBase> initialize(char letter)
{
    auto it = gOscTable.find(letter);
    if (it == gOscTable.end())
        throw std::runtime_error(std::string("no oscillator bound to ‘") + letter + "’");
    return (it->second)();          // call the factory
}

// ──────────────────────────────────────────────────────────────
//  Demo program
// ──────────────────────────────────────────────────────────────
int main()
{
    // 1.  Default bindings
    bindOsc('a', []{ return std::make_unique<SinOsc>();    });
    bindOsc('b', []{ return std::make_unique<SquareOsc>(); });
    bindOsc('c', []{ return std::make_unique<SawOsc>();    });

    // 2.  Create a few oscillators
    auto osc1 = initialize('a');           // gets a Sine
    auto osc2 = initialize('b');           // gets a Square
    std::cout << osc1->getName() << '\n';  // “Sine Oscillator”
    std::cout << osc2->getName() << '\n';  // “Square Oscillator”

    // 3.  Re-bind ‘a’ at run time
    bindOsc('a', []{ return std::make_unique<SquareOsc>(); });

    auto osc3 = initialize('a');           // now a *Square*
    std::cout << osc3->getName() << '\n';  // “Square Oscillator”

    // 4.  Each call makes a *new* object
    auto osc4 = initialize('a');           // another fresh Square
    std::cout << (osc3.get() != osc4.get() ? "two distinct objects\n"
                                           : "same object ??\n");
}