#ifndef LETTER_BINDS_H
#define LETTER_BINDS_H

#include <array>
#include <tuple>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <type_traits>
#include <stdexcept>
#include <sstream>
#include <variant>
#include <string>
#include <cctype>
#include <iostream>
#include <algorithm>

#include <juce_audio_processors/juce_audio_processors.h>

#include "oscillators.h"
#include "effects.h"
#include "midi_pulse.h"

using Value = std::variant<int, double, std::string>;

// Helper to make printing work with variants
template<typename OStream>
OStream& operator<<(OStream& os, const Value& v)
{
    std::visit([&os](auto&& arg) { os << arg; }, v);
    return os;
}

// helper: numeric cast with runtime check
namespace lreg_detail
{
template<typename T>
T value_cast(const Value& v)
{
    return std::visit([](auto&& arg)->T
    {
        using U = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<U,T>) return arg;
        else if constexpr (std::is_arithmetic_v<U> && std::is_arithmetic_v<T>)
            return static_cast<T>(arg);
        else if constexpr (std::is_same_v<T,int> && std::is_same_v<U,std::string>)
            return std::stoi(arg);
        else if constexpr (std::is_same_v<T,double> && std::is_same_v<U,std::string>)
            return std::stod(arg);
        else
            throw std::runtime_error("type mismatch in value_cast");
    }, v);
}

// convert vector<Value> -> tuple<Args...>
template<std::size_t Idx = 0, typename... Args>
inline void assign_tuple (std::tuple<Args...>& tup,
                         const std::vector<Value>& vals)
{
    if constexpr (Idx < sizeof...(Args))
    {
        if (Idx >= vals.size()) return;
        
        using Target = std::tuple_element_t<Idx, std::tuple<Args...>>;
        std::get<Idx>(tup) = value_cast<Target>(vals[Idx]);
        assign_tuple<Idx + 1>(tup, vals);
    }
}
}

// primary
template<typename Proc, typename = void> struct ctor_descriptor;

// Specializations name and type args, defaults are also set here.
template<typename Osc>
struct ctor_descriptor< Osc, std::enable_if_t<std::is_base_of_v<OscillatorBase, Osc>> >
{
    static constexpr std::array names { "note" };
    using types = std::tuple<int>;
    static constexpr types defaults { 66 };
};

template<> struct ctor_descriptor<FilterProcessor> {
    static constexpr std::array names{ "cutoff" };
    using types = std::tuple<double>;
    static constexpr types defaults { 2000.0 };
};

template<> struct ctor_descriptor<DelayProcessor> {
    static constexpr std::array
    names{ "time","feedback","wet","dry" };
    using types = std::tuple<double,double,double,double>;
    static constexpr types defaults { 0.5, 0.5, 0.5, 0.5 };
};

template<> struct ctor_descriptor<ReverbProcessor> {
    static constexpr std::array
    names{ "size","damp","wet","dry","width" };
    using types = std::tuple<double,double,double,double,double>;
    static constexpr types defaults { 0.5, 0.4, 0.5, 0.5, 0.2 };
};

template<> struct ctor_descriptor<MidiBeatPulseProcessor> {
    static constexpr std::array
    names{ "bpm","on","off" };
    using types = std::tuple<double,int,int>;
    static constexpr types defaults { 120.0, 1, 1 };
};

class LetterRegistry
{
    struct BindingBase
    {
        virtual ~BindingBase() = default;
        virtual std::unique_ptr<juce::AudioProcessor> create() const = 0;
        virtual void set_params (const std::vector<Value>&) = 0;
        virtual void set_param (std::string_view, const Value&) = 0;
        virtual const std::type_info& type_info() const = 0;
        virtual std::string_view type_name() const = 0;
        virtual void print_params(std::ostream& os) const = 0;
    };
    
    template<typename Proc>
    struct Binding final : BindingBase
    {
        using Desc = ctor_descriptor<Proc>;
        using Tuple = typename Desc::types;
        Tuple params;
        std::string typeName;
        
        template<typename... Args>
        explicit Binding (std::string_view name, Args&&... xs) 
            : params(std::forward<Args>(xs)...), typeName(name) {}
        
        std::unique_ptr<juce::AudioProcessor> create() const override
        {
            return std::apply([](auto&&... xs){ return std::make_unique<Proc>(xs...); }, params);
        }
        
        void set_params (const std::vector<Value>& v) override
        {
            lreg_detail::assign_tuple(params, v);
        }
        
        void set_param (std::string_view key, const Value& val) override
        {
            constexpr std::size_t N = std::tuple_size_v<Tuple>;
            bool ok = set_paramImpl<N-1>(key, val);
            if (!ok) throw std::runtime_error("unknown parameter name");
        }
        
        const std::type_info& type_info() const override { return typeid(Proc); }
        
        std::string_view type_name() const override { return typeName; }
        
        void print_params(std::ostream& os) const override
        {
            print_params_impl(os, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        }
        
    private:
        template<std::size_t... Is>
        void print_params_impl(std::ostream& os, std::index_sequence<Is...>) const
        {
            ((os << "    - " << Desc::names[Is] << " = " << std::get<Is>(params) 
                 << " (default: " << std::get<Is>(Desc::defaults) << ")\n"), ...);
        }
        
        template<std::size_t I>
        bool set_paramImpl (std::string_view key, const Value& val)
        {
            if (key == Desc::names[I]) { set_idx<I>(val); return true; }
            if constexpr (I > 0) return set_paramImpl<I-1>(key, val);
            return false;
        }
        template<std::size_t I>
        void set_idx (const Value& v)
        {
            using T = std::tuple_element_t<I, Tuple>;
            std::get<I>(params) = lreg_detail::value_cast<T>(v);
        }
    };
    
public:
    template<typename Proc, typename... Args>
    void bind (char letter, std::string_view typeName, Args&&... args)
    {
        static_assert(std::is_base_of_v<juce::AudioProcessor, Proc>, "Proc must derive AudioProcessor");
        bindings[letter] = std::make_unique<Binding<Proc>>(typeName, std::forward<Args>(args)...);
    }
    
    [[nodiscard]] std::unique_ptr<juce::AudioProcessor> initialize(char letter) const
    {
        auto it = bindings.find(letter);
        if (it == bindings.end()) throw std::runtime_error("initialize: unknown letter");
        return it->second->create();
    }
    
    template<typename... Args>
    void set_params(char letter, Args&&... args)
    {
        auto it = bindings.find(letter);
        if (it == bindings.end()) throw std::runtime_error("set_params: unknown letter");
        std::vector<Value> v{ Value(std::forward<Args>(args))... };
        it->second->set_params(v);
    }
    
    void set_param(char letter, std::string_view key, const Value& value)
    {
        auto it = bindings.find(letter);
        if (it == bindings.end()) throw std::runtime_error("set_param: unknown letter");
        it->second->set_param(key, value);
    }
    
    const std::type_info& getType_info(char letter) const
    {
        auto it = bindings.find(letter);
        if (it == bindings.end()) throw std::runtime_error("getType_info: unknown letter");
        return it->second->type_info();
    }
    
    // Print all bindings
    void printBindings() const
    {
        if (bindings.empty()) {
            std::cout << "No letters are currently bound.\n";
            return;
        }
        
        std::cout << "Current letter bindings:\n";
        std::cout << "------------------------\n";
        
        // Sort letters for consistent output
        std::vector<char> sortedLetters;
        for (const auto& [letter, _] : bindings) {
            sortedLetters.push_back(letter);
        }
        std::sort(sortedLetters.begin(), sortedLetters.end());
        
        for (char letter : sortedLetters) {
            auto it = bindings.find(letter);
            if (it != bindings.end()) {
                std::cout << "  '" << letter << "' -> " << it->second->type_name() << "\n";
            }
        }
        std::cout << "------------------------\n";
    }
    
    void printBindingsDetailed() const
    {
        if (bindings.empty()) {
            std::cout << "No letters are currently bound.\n";
            return;
        }
        
        std::cout << "Current letter bindings with parameters:\n";
        std::cout << "----------------------------------------\n";
        
        // Sort letters for consistent output
        std::vector<char> sortedLetters;
        for (const auto& [letter, _] : bindings) {
            sortedLetters.push_back(letter);
        }
        std::sort(sortedLetters.begin(), sortedLetters.end());
        
        for (char letter : sortedLetters) {
            auto it = bindings.find(letter);
            if (it != bindings.end()) {
                std::cout << "Letter '" << letter << "': " << it->second->type_name() << "\n";
                it->second->print_params(std::cout);
                std::cout << "\n";
            }
        }
        std::cout << "----------------------------------------\n";
    }
    
    // Get all bound letters
    std::vector<char> getBoundLetters() const
    {
        std::vector<char> letters;
        for (const auto& [letter, _] : bindings) {
            letters.push_back(letter);
        }
        std::sort(letters.begin(), letters.end());
        return letters;
    }
    
public:
    // does a binding already exist
    bool is_bound(char letter) const { return bindings.find(letter) != bindings.end(); }
    
private:
    std::unordered_map<char, std::unique_ptr<BindingBase>> bindings;
};

template<unsigned N>
struct CompileTimeRandom {
    // Linear congruential generator
    static constexpr unsigned value = (1103515245 * N + 12345) & 0x7fffffff;
};

// Hash a string at compile time (for seeding)
constexpr unsigned hash_string(const char* str, unsigned h = 0) {
    return !str[h] ? 5381 : (hash_string(str, h+1)*33) ^ static_cast<unsigned int>(str[h]);
}

// Get compile-time seed from __TIME__ and __DATE__
constexpr unsigned compile_time_seed() {
    return hash_string(__TIME__) ^ hash_string(__DATE__);
}

// Compile-time modulo that avoids bias
template<unsigned N, unsigned M>
struct SafeModulo {
    static constexpr unsigned value = N % M;
};

// Type list for compile-time type selection
template<typename... Types>
struct TypeList {
    static constexpr std::size_t size = sizeof...(Types);
    
    template<std::size_t N>
    using get = std::tuple_element_t<N, std::tuple<Types...>>;
};

// Select type at compile time based on index
template<typename TList, std::size_t Index>
struct TypeSelector;

template<typename... Types, std::size_t Index>
struct TypeSelector<TypeList<Types...>, Index> {
    using type = typename TypeList<Types...>::template get<Index>;
};

// All available processor types
using AllProcessorTypes = TypeList<
    SinOsc, SquareOsc, SawOsc, TriangleOsc, NoiseOsc,
    FilterProcessor, DelayProcessor, ReverbProcessor, MidiBeatPulseProcessor
>;

// Compile-time letter-to-type mapping
template<char Letter, unsigned Seed = compile_time_seed()>
struct RandomTypeForLetter {
    static constexpr unsigned random_value = CompileTimeRandom<Seed + Letter>::value;
    static constexpr std::size_t type_index = SafeModulo<random_value, AllProcessorTypes::size>::value;
    using type = typename TypeSelector<AllProcessorTypes, type_index>::type;
};

// Store type name at registration time
template<typename T>
struct TypeNameRegistry {
    static inline std::string name;
};

// Modified TypeTable to store type names for later use
class TypeTable
{
    using BinderFn = std::function<void(LetterRegistry&, char, const std::vector<Value>&)>;
public:
    template<typename Proc>
    static void register_type(std::string_view name)
    {
        // Store the name for compile-time use
        TypeNameRegistry<Proc>::name = std::string(name);
        
        table()[std::string(name)] = [name](LetterRegistry& r, char c, const std::vector<Value>& vals)
        {
            using Desc = ctor_descriptor<Proc>;
            using Tuple = typename Desc::types;
            
            Tuple tup = Desc::defaults;
            lreg_detail::assign_tuple(tup, vals);
            
            std::apply([&](auto&&... xs){ r.bind<Proc>(c, name, xs...); }, tup);
        };
    }
    
    static void bind(LetterRegistry& reg, char letter, std::string_view typeName, const std::vector<Value>& vals)
    {
        auto it = table().find(std::string(typeName));
        if (it == table().end()) throw std::runtime_error("unknown processor type");
        it->second(reg, letter, vals);
    }
    
    static bool is_known(std::string_view name)
    {
        return table().find(std::string(name)) != table().end();
    }
private:
    static std::unordered_map<std::string, BinderFn>& table()
    {
        static std::unordered_map<std::string, BinderFn> t; return t;
    }
};

// Get registered name for a type
template<typename T>
const std::string& get_type_name() {
    return TypeNameRegistry<T>::name;
}

// Compile-time string for documenting the random mapping
template<char... Letters>
struct RandomMappingString {
    static std::string get() {
        std::string result = "Compile-time random letter mappings:\n";
        ((result += std::string("  '") + Letters + "' -> " + 
          get_type_name<typename RandomTypeForLetter<Letters>::type>() + "\n"), ...);
        return result;
    }
};

// Helper to bind a single letter with its random type
template<char Letter>
void bind_random_letter(LetterRegistry& reg) {
    using ProcessorType = typename RandomTypeForLetter<Letter>::type;
    using Desc = ctor_descriptor<ProcessorType>;
    
    // Create with default parameters
    auto defaults = Desc::defaults;
    std::apply([&reg](auto&&... args) {
        reg.bind<ProcessorType>(Letter, get_type_name<ProcessorType>(), args...);
    }, defaults);
}

// Bind multiple letters at once
template<char... Letters>
void bind_random_letters(LetterRegistry& reg) {
    (bind_random_letter<Letters>(reg), ...);
}

// Bind all letters with random types
inline void bind_all_letters_random(LetterRegistry& reg) {
    bind_random_letters<'a','b','c','d','e','f','g','h','i','j','k','l','m',
                      'n','o','p','q','r','s','t','u','v','w','x','y','z'>(reg);
}

template<char... Letters>
void bind_random_letters_and_params(LetterRegistry& reg) {
    (bind_random_letter_with_random_params<Letters>(reg), ...);
}

inline void bind_all_letters_and_params_random(LetterRegistry& reg) {
    bind_random_letters_and_params<'a','b','c','d','e','f','g','h','i','j','k','l','m',
                      'n','o','p','q','r','s','t','u','v','w','x','y','z'>(reg);
}

// random bindings with random parameters too, exciting, but slightly more work
// if someone wanted to come in and add their own
template<char Letter, unsigned ParamSeed = compile_time_seed() + 1000>
struct RandomParametersForLetter {
    using ProcessorType = typename RandomTypeForLetter<Letter>::type;
    using Desc = ctor_descriptor<ProcessorType>;
    
template<std::size_t I>
static constexpr auto get_random_param() {
    using ParamType = std::tuple_element_t<I, typename Desc::types>;
    constexpr unsigned rand = CompileTimeRandom<ParamSeed + Letter * 100 + I>::value;
    
    if constexpr (std::is_same_v<ParamType, int>) {
            // Check if this is a MIDI-related parameter
            if constexpr (std::is_same_v<ProcessorType, MidiBeatPulseProcessor>) {
                // For "on" and "off" beat counts, use 1-8
                return 1 + (rand % 8);
            } else {
                // Random MIDI note between 36 and 84 (C2 to C6)
                return 36 + (rand % 48);
            }
        } else if constexpr (std::is_same_v<ParamType, double>) {
            // Special handling for specific parameters
            if constexpr (std::is_same_v<ProcessorType, FilterProcessor>) {
                // Cutoff frequency: 200-8000 Hz
                return 200.0 + (rand % 7800);
            } else if constexpr (std::is_same_v<ProcessorType, MidiBeatPulseProcessor> && I == 0) {
                // BPM: 60-180
                return 60.0 + (rand % 120);
            } else if constexpr (std::is_same_v<ProcessorType, DelayProcessor> && I == 0) {
                // Delay time: 0.1-2.0 seconds
                return 0.1 + static_cast<double>(rand % 1900) / 1000.0;
            } else {
                // Default: 0.0-1.0 for most parameters (feedback, wet, dry, etc.)
                return static_cast<double>(rand % 1000) / 1000.0;
            }
        } else {
            return ParamType{};
        }
    }
    
    template<std::size_t... Is>
    static auto make_random_params(std::index_sequence<Is...>) {
        return std::make_tuple(get_random_param<Is>()...);
    }
    
    static auto get_params() {
        constexpr auto size = std::tuple_size_v<typename Desc::types>;
        return make_random_params(std::make_index_sequence<size>{});
    }
};

// Bind with random parameters
template<char Letter>
void bind_random_letter_with_random_params(LetterRegistry& reg) {
    using ProcessorType = typename RandomTypeForLetter<Letter>::type;
    auto params = RandomParametersForLetter<Letter>::get_params();
    
    std::apply([&reg](auto&&... args) {
        reg.bind<ProcessorType>(Letter, get_type_name<ProcessorType>(), args...);
    }, params);
}

inline Value parse_token(const std::string& tok)
{
    bool numeric = !tok.empty() && (std::isdigit(tok[0]) || tok[0]=='-' || tok[0]=='+' );
    if (numeric)
    {
        if (tok.find_first_of(".eE") != std::string::npos) return Value(std::stod(tok));
        else return Value(std::stoi(tok));
    }
    return Value(tok);
}

inline void execute_bind_command(LetterRegistry& reg, const std::string& line)
{
    std::istringstream ss(line);
    std::string cmd; ss >> cmd;
    if (cmd != "set")
        throw std::runtime_error("unknown command (expected 'set')");
    
    char letter; ss >> letter;
    
    // First remaining token: either processor type or parameter key
    std::string firstTok; ss >> firstTok;
    if (firstTok.empty())
        throw std::runtime_error("incomplete set command");
    
    bool treatAsType = TypeTable::is_known(firstTok) || !reg.is_bound(letter);
    
    std::vector<std::pair<std::string, Value>> kv; // collected keyword args
    
    if (treatAsType)
    {
        // Bind letter to processor with default‑constructed parameters
        TypeTable::bind(reg, letter, firstTok, {});
        
        // Consume the rest as key/value pairs
        std::string k, v;
        while (ss >> k >> v)
            kv.emplace_back(k, parse_token(v));
    }
    else
    {
        // firstTok is a key; must already have a binding
        if (!reg.is_bound(letter))
            throw std::runtime_error("letter not bound to any processor type yet");
        
        std::string vTok; ss >> vTok;
        if (vTok.empty())
            throw std::runtime_error("parameter key without value");
        kv.emplace_back(firstTok, parse_token(vTok));
        
        // remaining pairs
        std::string k, v;
        while (ss >> k >> v)
            kv.emplace_back(k, parse_token(v));
    }
    
    // Apply parameters
    for (auto& [k, v] : kv)
        reg.set_param(letter, k, v);
}

// Helper for compile-time parameter printing
template<typename Proc>
struct param_printer
{
    using Desc = ctor_descriptor<Proc>;
    
    template<std::size_t... Is>
    static void print_defaults(std::ostream& os, std::index_sequence<Is...>)
    {
        os << "  Parameters:\n";
        ((os << "    - " << Desc::names[Is] << " (default: " << std::get<Is>(Desc::defaults) << ")\n"), ...);
    }
    
    static void print(std::ostream& os)
    {
        print_defaults(os, std::make_index_sequence<std::tuple_size_v<typename Desc::types>>{});
    }
};

// Type catalog for compile-time listing of all available types
template<typename... Procs>
struct TypeCatalog
{
    static void print_available_types()
    {
        std::cout << "Available processor types:\n";
        std::cout << "-------------------------\n";
        (print_type<Procs>(), ...);
    }
    
private:
    template<typename Proc>
    static void print_type()
    {
        std::cout << "Type: " << typeid(Proc).name() << "\n";
        param_printer<Proc>::print(std::cout);
        std::cout << "\n";
    }
};

// This enables matching type names to types
namespace {
const bool _reg_SinOsc = (TypeTable::register_type<SinOsc>("sin"), true);
const bool _reg_SquareOsc = (TypeTable::register_type<SquareOsc>("square"), true);
const bool _reg_SawOsc = (TypeTable::register_type<SawOsc>("saw"), true);
const bool _reg_TriangleOsc = (TypeTable::register_type<TriangleOsc>("triangle"), true);
const bool _reg_NoiseOsc = (TypeTable::register_type<NoiseOsc>("noise"), true);
const bool _reg_FilterProcessor = (TypeTable::register_type<FilterProcessor>("filter"), true);
const bool _reg_DelayProcessor = (TypeTable::register_type<DelayProcessor>("delay"), true);
const bool _reg_ReverbProcessor = (TypeTable::register_type<ReverbProcessor>("reverb"), true);
const bool _reg_MidiBeatPulseProcessor = (TypeTable::register_type<MidiBeatPulseProcessor>("midi"), true);
}

#endif