#include <iostream>
#include <string>
#include <thread>
#include <string_view>
#include <optional>
#include <regex>
#include <stdexcept>

/* A fun Regex + Functor class that I thought I would use a lot when I first
   was brainstorming my project. It did not end up being useful but I did find
   one place to sneak it in. */

template<size_t N>
struct CompileTimeString {
    char data[N];

    constexpr CompileTimeString(const char (&str)[N]) {
        for (size_t i = 0; i < N; ++i) {
            data[i] = str[i];
        }
    }

    constexpr const char* c_str() const {
        return data;
    }

    constexpr size_t size() const {
        return N - 1;
    }

    constexpr operator std::string_view() const {
        return std::string_view(data, N - 1);
    }
};

template <std::size_t N>
CompileTimeString(const char (&)[N]) -> CompileTimeString<N>;

template <CompileTimeString Pattern>
class RegexFunctor
{
    std::string input_;
    std::regex re_{Pattern.c_str()};
    std::sregex_iterator cur_{};
    const std::sregex_iterator end_{};

    bool ok_ = false;

public:
    explicit RegexFunctor(const std::string& text) : input_(text)
    {
        try
        {
            cur_ = std::sregex_iterator(input_.begin(), input_.end(), re_);
            ok_  = true;
        }
        catch (const std::regex_error& e)
        {
            std::cerr << "regex error for \"" << Pattern.c_str() << "\": "
                      << e.what() << '\n';
            cur_ = {};                // leave at end()
            ok_  = false;
        }
    }

    /// Return the next std::smatch (or std::nullopt when exhausted / invalid)
    std::optional<std::smatch> operator() ()
    {
        if (!ok_ || cur_ == end_) return std::nullopt;

        std::smatch m = *cur_;
        ++cur_;
        return m;
    }
};