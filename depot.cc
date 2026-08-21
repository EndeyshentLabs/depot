// $ c++ -O3 -std=c++23 depot.cc -o depot
// SPDX-License-Identifier: BSD-2-Clause
// See bottom of the file for full license text

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <ranges>
#include <set>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32

#error "windows platform is not supported"

#else // !defined(_WIN32)

#include <sys/wait.h>
#include <unistd.h>

#endif // #ifdef _WIN32

// DEFS {{{
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using usz = std::size_t;

using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
using ssz = std::ptrdiff_t;

using f32 = float;
using f64 = double;
// }}}

// UTIL {{{
#define ASSERT(cond, ...)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::print(stderr,                                                 \
                       "{}:{}: ASSERTION `{}` FAILED: ",                       \
                       __FILE__,                                               \
                       __LINE__,                                               \
                       #cond);                                                 \
            std::println(stderr, __VA_ARGS__);                                 \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

[[noreturn]] void todo(std::string_view text,
                       const std::source_location sl
                       = std::source_location::current())
{
    std::println(stderr,
                 "{}:{}:{}: unimplemented: {}",
                 sl.file_name(),
                 sl.line(),
                 sl.column(),
                 text);
    std::abort();
}

#define TODO() todo(__PRETTY_FUNCTION__)
// }}}

// EXECUTE COMMAND (execv style) {{{
static void execute_command(const std::vector<std::string>& cmd_line)
{
    std::print("cmd:");
    for (const auto& s : cmd_line) {
        std::print(" {}", s);
    }
    std::println();

#ifdef _WIN32
    TODO();
#else
    const char** args = new const char*[cmd_line.size() + 1];
    for (usz i = 0; i < cmd_line.size(); ++i) {
        args[i] = cmd_line[i].c_str();
    }
    args[cmd_line.size()] = nullptr;

    ::pid_t child_pid = ::fork();
    if (child_pid == -1) {
        std::println(stderr, "error: fork failed: {}", std::strerror(errno));
        std::exit(1);
    } else if (child_pid == 0) { // child
        ::execvp(cmd_line[0].c_str(), (char* const*)args);
    } else { // parent
        int wstatus = 0;
        do {
            pid_t w = ::waitpid(child_pid, &wstatus, WUNTRACED | WCONTINUED);
            if (w == -1) {
                std::println(stderr,
                             "error: waitpid failed: {}",
                             std::strerror(errno));
                std::exit(1);
            }

            if (WIFEXITED(wstatus)) {
                int exit_code = WEXITSTATUS(wstatus);
                std::println("cmd: child process exited with {}",
                             WEXITSTATUS(wstatus));
                if (exit_code != 0)
                    std::exit(exit_code);
                break;
            } else if (WIFSIGNALED(wstatus)) {
                std::println("cmd: child process was killed with signal {}",
                             WTERMSIG(wstatus));
                std::exit(WTERMSIG(wstatus));
            }
        } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
    }

    if (args) {
        delete[] args;
        args = nullptr;
    }
#endif
}
// }}}

// PERFORMANCE STOPWATCH {{{
static std::unordered_map<std::string, double> performance_map;

struct Scoped_Stopwatch {
    const std::chrono::time_point<std::chrono::high_resolution_clock> start;
    const std::string name;

    Scoped_Stopwatch(const std::string& name)
        : start { std::chrono::high_resolution_clock::now() }
        , name { name }
    {
        if (!performance_map.contains(name))
            performance_map.emplace(name, 0.0);
    }

    ~Scoped_Stopwatch()
    {
        performance_map[name]
            += std::chrono::duration<double, std::milli>(
                   std::chrono::high_resolution_clock::now() - start)
                   .count();
    }
};
// }}}

// {I,O}FSTREAM ERROR CHECKING {{{
template <typename T>
    requires(std::is_same_v<T, std::ifstream>
             || std::is_same_v<T, std::ofstream>)
bool check_fstream_error(const T& file, std::string_view path)
{
    if (!file.is_open()) {
        std::string reason = "Unknown error";
        if (file.fail())
            reason = std::strerror(errno);

        std::println(stderr, "{}: error: Couldn't open: {}", path, reason);
        return false;
    }

    return true;
}
// }}}

// BETTER "STRING TO S64" {{{
struct Better_Strtoll_Result {
    enum struct Error_Kind {
        Ok,
        Out_Of_Range,
        Garbage_On_The_End,
    } error { Error_Kind::Ok };
    s64 value { 0 };

    constexpr operator bool() const { return error == Error_Kind::Ok; }
    operator s64() { return value; }
};

Better_Strtoll_Result better_strtoll(const std::string& str, s32 base = 10)
{
    char* endptr = nullptr;
    errno = 0;
    s64 num = std::strtoll(str.c_str(), &endptr, base);
    if (errno == ERANGE)
        return { Better_Strtoll_Result::Error_Kind::Out_Of_Range };

    if (*endptr != '\0')
        return { Better_Strtoll_Result::Error_Kind::Garbage_On_The_End,
                 *endptr };

    return {
        Better_Strtoll_Result::Error_Kind::Ok,
        num,
    };
}
// }}}

// DEPOT SOURCE LOCATION {{{
struct Location {
    std::filesystem::path file_path { };
    u64 row { 0 };
    u64 col { 0 };
};

template <>
struct std::formatter<Location> {
    template <typename PC>
    constexpr PC::iterator parse(PC& ctx)
    {
        return ctx.begin();
    }

    template <typename FC>
    FC::iterator format(const Location& loc, FC& ctx) const
    {
        return std::format_to(ctx.out(),
                              "{}:{}:{}",
                              loc.file_path.string(),
                              loc.row + 1,
                              loc.col + 1);
    }
};
// }}}

// LEXER {{{
struct Token {
    using As = std::variant<s64, std::string>;
    Location loc;
    enum struct Kind {
        Proc,
        Link,
        Ident,
        Open_Curly,
        Close_Curly,
        Number,
        String,

        If,
        Else,
        Elif,
        Then,
        While,

        Extern,
        Semicolon, // TODO: better name?

        Sig_Delimit,

        Drop,
        Dup,
        Swap,
        Rot,
        Over,
        Plus,
        Minus,
        Mult,
        // TODO(:Idiv): merge these two into a sinlge Idiv token
        Div,
        Mod,

        Equal,
        Not_Equal,
        Less_Than,
        Less_Equal,
        Greater_Than,
        Greater_Equal,

        Load64,
        Load32,
        Load16,
        Load8,
        Store64,
        Store32,
        Store16,
        Store8,

        To_Int64,
        To_Ptr,
        To_Bool,

        True,
        False,

        Include,

        Memory,

        Dispatch,
    } kind;
    std::string text;
    As as;
};

static const std::unordered_map<std::string_view, Token::Kind> keywords = {
    { "proc", Token::Kind::Proc },
    { "link", Token::Kind::Link },
    { "{", Token::Kind::Open_Curly },
    { "}", Token::Kind::Close_Curly },

    { "drop", Token::Kind::Drop },
    { "dup", Token::Kind::Dup },
    { "swap", Token::Kind::Swap },
    { "rot", Token::Kind::Rot },
    { "over", Token::Kind::Over },

    { "+", Token::Kind::Plus },
    { "-", Token::Kind::Minus },
    { "*", Token::Kind::Mult },
    { "/", Token::Kind::Div },
    { "mod", Token::Kind::Mod },

    { "extern", Token::Kind::Extern },
    { ";", Token::Kind::Semicolon },
    { "--", Token::Kind::Sig_Delimit },

    { "if", Token::Kind::If },
    { "else", Token::Kind::Else },
    { "elif", Token::Kind::Elif },
    { "then", Token::Kind::Then },
    { "while", Token::Kind::While },

    { "=", Token::Kind::Equal },
    { "!=", Token::Kind::Not_Equal },
    { "<", Token::Kind::Less_Than },
    { "<=", Token::Kind::Less_Equal },
    { ">", Token::Kind::Greater_Than },
    { ">=", Token::Kind::Greater_Equal },

    { "@64", Token::Kind::Load64 },
    { "@32", Token::Kind::Load32 },
    { "@16", Token::Kind::Load16 },
    { "@8", Token::Kind::Load8 },
    { "!64", Token::Kind::Store64 },
    { "!32", Token::Kind::Store32 },
    { "!16", Token::Kind::Store16 },
    { "!8", Token::Kind::Store8 },

    { ">int64", Token::Kind::To_Int64 },
    { ">ptr", Token::Kind::To_Ptr },
    { ">bool", Token::Kind::To_Bool },

    { "true", Token::Kind::True },
    { "false", Token::Kind::False },

    { "include", Token::Kind::Include },

    { "memory", Token::Kind::Memory },

    { "dispatch", Token::Kind::Dispatch },
};

static constexpr std::string_view human(Token::Kind kind, bool plural = false)
{
    switch (kind) {
    case Token::Kind::Proc:
        return plural ? "`proc` keywords" : "`proc` keyword";
    case Token::Kind::Link:
        return plural ? "`link` keywords" : "`link` keyword";
    case Token::Kind::Ident:
        return plural ? "identifiers" : "an identifier";
    case Token::Kind::Open_Curly:
        return plural ? "`{`" : "`{`";
    case Token::Kind::Close_Curly:
        return plural ? "`}`" : "`}`";
    case Token::Kind::Number:
        return plural ? "numbers" : "a number";
    case Token::Kind::String:
        return plural ? "strings" : "a string";
    case Token::Kind::If:
        return plural ? "`if` keywords" : "`if` keyword";
    case Token::Kind::Else:
        return plural ? "`else` keywords" : "`else` keyword";
    case Token::Kind::Elif:
        return plural ? "`elif` keywords" : "`elif` keyword";
    case Token::Kind::Then:
        return plural ? "`then` keywords" : "`then` keyword";
    case Token::Kind::While:
        return plural ? "`while` keywords" : "`while` keyword";
    case Token::Kind::Extern:
        return plural ? "`extern` keywords" : "`extern` keyword";
    case Token::Kind::Semicolon:
        return "`;`";
    case Token::Kind::Sig_Delimit:
        return "`--`";
    case Token::Kind::Drop:
        return plural ? "`drop` keywords" : "`drop` keyword";
    case Token::Kind::Dup:
        return plural ? "`dup` keywords" : "`dup` keyword";
    case Token::Kind::Swap:
        return plural ? "`swap` keywords" : "`swap` keyword";
    case Token::Kind::Rot:
        return plural ? "`rot` keywords" : "`rot` keyword";
    case Token::Kind::Over:
        return plural ? "`over` keywords" : "`over` keyword";
    case Token::Kind::Plus:
        return "`+`";
    case Token::Kind::Minus:
        return "`-`";
    case Token::Kind::Mult:
        return "`*`";
    case Token::Kind::Div:
        return "`/`";
    case Token::Kind::Mod:
        return "`mod`";
    case Token::Kind::Equal:
        return "`=`";
    case Token::Kind::Not_Equal:
        return "`!=`";
    case Token::Kind::Less_Than:
        return "`<`";
    case Token::Kind::Less_Equal:
        return "`<=`";
    case Token::Kind::Greater_Than:
        return "`>`";
    case Token::Kind::Greater_Equal:
        return "`>=`";
    case Token::Kind::Load64:
        return "`@64`";
    case Token::Kind::Load32:
        return "`@32`";
    case Token::Kind::Load16:
        return "`@16`";
    case Token::Kind::Load8:
        return "`@8`";
    case Token::Kind::Store64:
        return "`!64`";
    case Token::Kind::Store32:
        return "`!32`";
    case Token::Kind::Store16:
        return "`!16`";
    case Token::Kind::Store8:
        return "`!8`";
    case Token::Kind::To_Int64:
        return plural ? "casts to an int64" : "cast to an int64";
    case Token::Kind::To_Ptr:
        return plural ? "casts to a ptr" : "cast to a ptr";
    case Token::Kind::To_Bool:
        return plural ? "casts to a bool" : "cast to a bool";
    case Token::Kind::True:
        return plural ? "`true` keyword-constants" : "`true` keyword-constant";
    case Token::Kind::False:
        return plural ? "`false` keyword-constants"
                      : "`false` keyword-constant";
    case Token::Kind::Include:
        return plural ? "`include` keywords" : "`include` keyword";
    case Token::Kind::Memory:
        return plural ? "`memory` keywords" : "`memory` keyword";
    case Token::Kind::Dispatch:
        return plural ? "`dispatch` keywords" : "`dispatch` keyword";
    default:
        std::unreachable();
    }
}

struct Lexer {
    char c { 0 };
    usz cursor { 0 };
    Location loc { };
    std::string source { };

    bool has_error { false };

    Lexer(const std::filesystem::path& path)
    {
        std::ifstream file { path };

        if (!check_fstream_error(file, path.string())) {
            has_error = true;
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        source = buffer.str();

        loc.file_path = path.lexically_normal();
        if (source.empty()) {
            std::println(stderr, "{}: error: Empty file", loc);
            std::println("{}: note: consider adding procedure main", loc);
            std::println("|\t// minimal program:");
            std::println("|\tlink \"c\"\n|\tproc main -- int64 {{ 0 }}");
            has_error = true;
            return;
        }
        c = source[cursor];
        ASSERT(c != 0,
               "{}: error: First character of the file cannot be NUL",
               loc);
    }

    bool advance()
    {
        if (cursor >= source.size() - 1) {
            c = 0;
            return false;
        }

        if (c == '\n') {
            loc.row++;
            loc.col = 0;
        } else {
            loc.col++;
        }

        cursor++;
        c = source[cursor];

        return true;
    }

    constexpr char peek(usz offset = 1) const
    {
        if (cursor + offset >= source.size() - 1)
            return 0;
        return source[cursor + offset];
    }

    bool lex(std::vector<Token>& toks)
    {
        const Scoped_Stopwatch stopwatch { "lexing" };

        do {
            Location start_loc = loc;

            if (std::isspace(c)) {
                // ignore
            } else if (c == '/' && peek() == '/') {
                do
                    advance();
                while (c && c != '\n');
            } else if (c == '"') {
                std::string text;
                std::string str;

                // consume opening "
                text.push_back(c);
                advance();

                while (c && c != '"') {
                    text.push_back(c);

                    if (c == '\\') { // TODO: more escapes
                        if (peek() == 'n')
                            str.push_back('\n');
                        else if (peek() == 'r')
                            str.push_back('\r');
                        else if (peek() == 't')
                            str.push_back('\t');
                        else if (peek() == 'v')
                            str.push_back('\v');
                        else if (peek() == 'f')
                            str.push_back('\f');
                        else if (peek() == 'a')
                            str.push_back('\a');
                        else if (peek() == 'b')
                            str.push_back('\b');
                        else if (peek() == '\\')
                            str.push_back('\\');
                        else if (peek() == '0')
                            str.push_back('\0');
                        else {
                            std::println(
                                stderr,
                                "{}: error: unknown string escape character {}",
                                start_loc,
                                peek());
                            return false;
                        }

                        advance();
                        text.push_back(c);
                    } else
                        str.push_back(c);

                    advance();
                }

                // consume closing "
                text.push_back(c);
                advance();

                if (!std::isspace(c)) {
                    std::println(stderr,
                                 "{}: error: junk at the end of the string",
                                 loc);
                    return false;
                }

                toks.emplace_back(start_loc, Token::Kind::String, text, str);
            } else if ((c == '-' && std::isdigit(peek())) || std::isdigit(c)) {
                std::string text;

                do {
                    text.push_back(c);
                    advance();
                } while (c && std::isdigit(c) && c != '#');

                if (c == '#') { // `text` is base
                    // consume #
                    text.push_back(c);
                    advance();
                    if (std::isalnum(c)) {
                        std::string num_str;
                        do {
                            text.push_back(c);
                            num_str.push_back(c);
                            advance();
                        } while (c && std::isalnum(c));

                        const auto base
                            = better_strtoll(text.substr(0, text.find('#')));

                        if (base.error
                            == Better_Strtoll_Result::Error_Kind::
                                Garbage_On_The_End) {
                            std::println(stderr,
                                         "{}: error: unexpected character {}",
                                         loc,
                                         static_cast<char>(base.value));
                            return false;
                        }

                        if (!(base.value >= 2 && base.value <= 36)) {
                            std::println(stderr,
                                         "{}: error: invalid base {}, accepted "
                                         "number literal base range is [2, 36]",
                                         start_loc,
                                         base.value);
                            return false;
                        }

                        const auto num = better_strtoll(num_str, base.value);
                        if (num.error
                            == Better_Strtoll_Result::Error_Kind::
                                Out_Of_Range) {
                            std::println(
                                stderr,
                                "{}: error: number out of range (accepted "
                                "range [{}, {}])",
                                start_loc,
                                std::numeric_limits<decltype(num.value)>::min(),
                                std::numeric_limits<
                                    decltype(num.value)>::max());
                            return false;
                        }

                        if (num.error
                            == Better_Strtoll_Result::Error_Kind::
                                Garbage_On_The_End) {
                            std::println(stderr,
                                         "{}: error: unexpected character {} "
                                         "in a number of base {}",
                                         start_loc,
                                         num.value,
                                         base.value);
                            return false;
                        }

                        toks.emplace_back(start_loc,
                                          Token::Kind::Number,
                                          text,
                                          num.value);
                    } else {
                        do {
                            text.push_back(c);
                            advance();
                        } while (c && !std::isspace(c));

                        toks.emplace_back(start_loc, Token::Kind::Ident, text);
                    }
                } else if (!std::isspace(c)) {
                    std::println(stderr,
                                 "{}: error: unexpected character {}",
                                 loc,
                                 c);
                    return false;
                } else {
                    char* endptr = nullptr;
                    const s64 num = std::strtoll(text.c_str(), &endptr, 10);

                    if (*endptr != '\0') {
                        std::println(stderr,
                                     "{}: error: unexpected character {}",
                                     loc,
                                     c);
                        return false;
                    }

                    toks.emplace_back(start_loc,
                                      Token::Kind::Number,
                                      text,
                                      num);
                }
            } else if (std::isprint(c)) {
                std::string text;

                do {
                    text.push_back(c);
                    advance();
                } while (c && !std::isspace(c));

                if (keywords.contains(text))
                    toks.emplace_back(start_loc, keywords.at(text), text);
                else
                    toks.emplace_back(start_loc, Token::Kind::Ident, text);
            } else {
                std::println(stderr,
                             "{}: error: unexpected character {}",
                             loc,
                             c);
                return false;
            }
        } while (advance());

        return true;
    }
};
// }}}

// PARSER {{{
struct Op {
    using As = std::variant<s64, std::string>;

    Token tok;
    enum struct Kind {
        // Empty, // no operand (0 as int64)
        Proc_Start, // operand(str): procedure name
        Proc_Return, // operand(str): procedure name
        Proc_Call, // operand(str): procedure name
        Extern_Call, // operand(str): procedure name
        Dispatch, // operand(str): dispatch group name
        Push_Int, // operand(int64): number
        Push_Str, // operand(int64): index inside of `Da_Thing::strings`
        Push_Global_Memory, // operand(int64): index inside of
                            // `Da_Thing::memories`
        If,
        Else,
        Elif,
        Then,

        While,
        Do,
        End_While,

        Drop, // no operand (0 as int64)
        Dup, // no operand (0 as int64)
        Swap, // no operand (0 as int64)
        Rot, // no operand (0 as int64)
        Over, // no operand (0 as int64)
        Plus, // no operand (0 as int64)
        Minus, // no operand (0 as int64)
        Mult, // no operand (0 as int64)
        // TODO(:Idiv)
        Div, // no operand (0 as int64)
        Mod, // no operand (0 as int64)

        Equal, // no operand (0 as int64)
        Not_Equal, // no operand (0 as int64)
        Less_Than, // no operand (0 as int64)
        Less_Equal, // no operand (0 as int64)
        Greater_Than, // no operand (0 as int64)
        Greater_Equal, // no operand (0 as int64)

        Load64, // no operand (0 as int64)
        Load32, // no operand (0 as int64)
        Load16, // no operand (0 as int64)
        Load8, // no operand (0 as int64)
        Store64, // no operand (0 as int64)
        Store32, // no operand (0 as int64)
        Store16, // no operand (0 as int64)
        Store8, // no operand (0 as int64)

        To_Int64, // no operand (0 as int64)
        To_Ptr, // no operand (0 as int64)
        To_Bool, // no operand (0 as int64)
    } kind;

    As as;
};

static constexpr std::string_view human(Op::Kind kind)
{
    switch (kind) {
    case Op::Kind::Proc_Start:
        return "Proc_Start";
    case Op::Kind::Proc_Return:
        return "Proc_Return";
    case Op::Kind::Proc_Call:
        return "Proc_Call";
    case Op::Kind::Extern_Call:
        return "Extern_Call";
    case Op::Kind::Dispatch:
        return "Dispatch";
    case Op::Kind::Push_Int:
        return "Push_Int";
    case Op::Kind::Push_Str:
        return "Push_Str";
    case Op::Kind::Push_Global_Memory:
        return "Push_Global_Memory";
    case Op::Kind::If:
        return "If";
    case Op::Kind::Else:
        return "Else";
    case Op::Kind::Elif:
        return "Else_If";
    case Op::Kind::Then:
        return "Then";
    case Op::Kind::While:
        return "While";
    case Op::Kind::Do:
        return "Do";
    case Op::Kind::End_While:
        return "End_While";
    case Op::Kind::Drop:
        return "Drop";
    case Op::Kind::Dup:
        return "Dup";
    case Op::Kind::Swap:
        return "Swap";
    case Op::Kind::Rot:
        return "Rot";
    case Op::Kind::Over:
        return "Over";
    case Op::Kind::Plus:
        return "Plus";
    case Op::Kind::Minus:
        return "Minus";
    case Op::Kind::Mult:
        return "Mult";
    case Op::Kind::Div:
        return "Div";
    case Op::Kind::Mod:
        return "Mod";
    case Op::Kind::Equal:
        return "Equal";
    case Op::Kind::Not_Equal:
        return "Not_Equal";
    case Op::Kind::Less_Than:
        return "Less_Than";
    case Op::Kind::Less_Equal:
        return "Less_Equal";
    case Op::Kind::Greater_Than:
        return "Greater_Than";
    case Op::Kind::Greater_Equal:
        return "Greater_Equal";
    case Op::Kind::Load64:
        return "Load64";
    case Op::Kind::Load32:
        return "Load32";
    case Op::Kind::Load16:
        return "Load16";
    case Op::Kind::Load8:
        return "Load8";
    case Op::Kind::Store64:
        return "Store64";
    case Op::Kind::Store32:
        return "Store32";
    case Op::Kind::Store16:
        return "Store16";
    case Op::Kind::Store8:
        return "Store8";
    case Op::Kind::To_Int64:
        return "To_Int64";
    case Op::Kind::To_Ptr:
        return "To_Ptr";
    case Op::Kind::To_Bool:
        return "To_Bool";
    default:
        std::unreachable();
    }
}

struct Type {
    std::string name;
    u64 size_of;

    constexpr bool operator==(const Type& other) const noexcept
    {
        return name == other.name && size_of == other.size_of;
    }

    constexpr bool operator!=(const Type& other) const noexcept
    {
        return !(*this == other);
    }
};

template <>
struct std::hash<Type> {
    std::size_t operator()(const Type& t) const noexcept
    {
        const std::size_t h1 = std::hash<decltype(t.name)> { }(t.name);
        const std::size_t h2 = std::hash<decltype(t.size_of)> { }(t.size_of);
        return h1 ^ (h2 << 1);
    }
};

static const std::unordered_map<std::string, Type> builtin_types = {
    { "int64", Type { "int64", 8 } },
    { "bool", Type { "bool", 1 } },
    { "ptr", Type { "ptr", 8 } },
};

struct Type_Sig {
    std::vector<Type> input_types;
    std::vector<Type> return_types;

    constexpr bool operator==(const Type_Sig& other) const noexcept
    {
        return input_types == other.input_types
            && return_types == other.return_types;
    }

    constexpr bool operator!=(const Type_Sig& other) const noexcept
    {
        return !(*this == other);
    }
};

struct Proc {
    Token tok;
    std::string name;
    s64 op_index;
    Type_Sig sig;
};

struct Extern_Proc {
    Token tok;
    std::string name;
    Type_Sig sig;
    // TODO: calling convention
};

struct Memory {
    Token tok;
    std::string name;
    u64 id;
    u64 size;
};

struct Da_Thing {
    std::vector<std::string> linker_libs;
    std::vector<Op> ops;
    std::vector<std::string> strings;
    std::vector<Memory> memories;
    std::unordered_map<std::string, Proc> procs;
    std::unordered_map<std::string, Extern_Proc> extern_procs;
    std::vector<std::filesystem::path> extra_include_paths;
    std::unordered_multimap<std::string, std::string> dispatch_groups;
};

constexpr auto find_memory_def(const Da_Thing& ctx, std::string_view name)
{
    return std::ranges::find_if(
        ctx.memories,
        [&name](const Memory& mem) -> bool { return mem.name == name; });
}

static std::set<std::filesystem::path> included_paths { };
struct Parser {
    std::vector<Token> toks;
    bool has_error { false };
    Da_Thing& ctx;
    std::filesystem::path current_file { };
    std::string_view current_proc_name { };

    Parser(std::span<Token> toks,
           Da_Thing& ctx,
           const std::filesystem::path& file_path)
        : toks { toks.size() }
        , ctx { ctx }
        , current_file { file_path }
    {
        this->toks.assign_range(std::ranges::reverse_view(toks));
    }

    enum struct Name_Availabilty {
        Available,
        Exists_Proc,
        Exists_Extern_Proc,
        Exists_Builtin_Type,
        Exists_Memory,
        Exists_Dispatch_Group_Name,
    };

    Name_Availabilty is_name_available(const std::string& name)
    {
        if (ctx.procs.contains(name))
            return Name_Availabilty::Exists_Proc;
        if (ctx.extern_procs.contains(name))
            return Name_Availabilty::Exists_Extern_Proc;
        if (builtin_types.contains(name))
            return Name_Availabilty::Exists_Builtin_Type;
        if (find_memory_def(ctx, name) != ctx.memories.end())
            return Name_Availabilty::Exists_Memory;
        if (ctx.dispatch_groups.contains(name))
            return Name_Availabilty::Exists_Dispatch_Group_Name;

        return Name_Availabilty::Available;
    }

    template <typename... Args>
    void
    error(const Location& loc, std::format_string<Args&...> text, Args... args)
    {
        has_error = true;
        std::print(stderr, "{}: error: ", loc);
        std::println(stderr, text, args...);
    }

    template <typename... Args>
    void
    note(const Location& loc, std::format_string<Args&...> text, Args... args)
    {
        std::print("{}: note: ", loc);
        std::println(text, args...);
    }

    std::optional<Token>
    expect(const Token& self, Token::Kind kind, bool consume = true)
    {
        if (toks.size() <= 0) {
            error(self.loc, "expected {}, but got nothing", human(kind));
            return std::nullopt;
        }

        Token tok = toks.back();
        if (consume)
            toks.pop_back();

        if (tok.kind != kind) {
            error(tok.loc,
                  "expected {}, but got {}",
                  human(kind),
                  human(tok.kind));
            return std::nullopt;
        }

        return std::make_optional(tok);
    }

    std::optional<Token> expect(const Token& self,
                                const std::set<Token::Kind>& kinds,
                                bool consume = true)
    {
        const auto kind_strings
            = kinds
            | std::ranges::views::transform(
                  [](Token::Kind k) -> std::string_view { return human(k); });
        const auto ored_kinds = std::ranges::to<std::string>(
            std::views::join_with(kind_strings, " or "));

        if (toks.size() <= 0) {
            error(self.loc, "expected {:s}, but got nothing", ored_kinds);
            return std::nullopt;
        }

        Token tok = toks.back();
        if (consume)
            toks.pop_back();

        if (!kinds.contains(tok.kind)) {
            error(tok.loc,
                  "expected {}, but got {}",
                  ored_kinds,
                  human(tok.kind));
            return std::nullopt;
        }

        return std::make_optional(tok);
    }

    std::optional<Type_Sig> parse_type_sig(const Token& self)
    {
        if (toks.empty()) {
            error(self.loc, "expected type signature, but got nothing");
            return std::nullopt;
        }

        bool return_type_mode = false;
        Location delimit_loc { };

        Type_Sig result;
        while (!toks.empty() && toks.back().kind != Token::Kind::Open_Curly
               && toks.back().kind != Token::Kind::Semicolon) {
            const auto tok = toks.back();
            toks.pop_back();

            if (tok.kind == Token::Kind::Sig_Delimit) {
                if (return_type_mode) {
                    error(tok.loc,
                          "duplicate {}",
                          human(Token::Kind::Sig_Delimit));
                    note(delimit_loc, "previous was found there");
                    return std::nullopt;
                }

                delimit_loc = tok.loc;
                return_type_mode = true;
                continue;
            }

            if (tok.kind != Token::Kind::Ident) {
                error(tok.loc,
                      "expected type name as {}, but got {}",
                      human(Token::Kind::Ident),
                      human(tok.kind));
                return std::nullopt;
            }

            if (!builtin_types.contains(tok.text)) {
                error(tok.loc, "unknown type {}", tok.text);
                return std::nullopt;
            }

            if (return_type_mode) {
                result.return_types.push_back(builtin_types.at(tok.text));
            } else {
                result.input_types.push_back(builtin_types.at(tok.text));
            }
        }

        if (!expect(self,
                    { Token::Kind::Open_Curly, Token::Kind::Semicolon },
                    false)) {
            error(self.loc, "unterminated type signature");
            return std::nullopt;
        }

        return std::make_optional(result);
    }

    std::optional<std::string> parse_proc()
    {
        const auto self = toks.back();
        toks.pop_back();

        const auto proc_name = expect(self, Token::Kind::Ident);
        if (!proc_name) {
            note(self.loc, "for this procedure definition");
            return std::nullopt;
        }

        const auto& proc_str = proc_name->text;

        const auto sig = parse_type_sig(self);
        if (!sig) {
            note(self.loc, "for this procedure definition");
            return std::nullopt;
        }

        const auto open_curly = expect(self, Token::Kind::Open_Curly);
        if (!open_curly) {
            note(self.loc, "for this procedure definition");
            return std::nullopt;
        }

        switch (is_name_available(proc_str)) {
        case Name_Availabilty::Exists_Proc:
            error(self.loc, "redefinition of procedure \"{}\"", proc_str);
            note(ctx.procs.at(proc_str).tok.loc, "previously defined here");
            return std::nullopt;
        case Name_Availabilty::Exists_Extern_Proc:
            error(self.loc,
                  "procedure name shadows external procedure \"{}\"",
                  proc_str);
            note(ctx.extern_procs.at(proc_str).tok.loc,
                 "previously defined here");
            return std::nullopt;
        case Name_Availabilty::Exists_Builtin_Type:
            error(self.loc,
                  "procedure name shadows builtin type \"{}\"",
                  proc_str);
            return std::nullopt;
        case Name_Availabilty::Exists_Memory:
            error(self.loc,
                  "procedure name shadows memory definition \"{}\"",
                  proc_str);
            return std::nullopt;
        case Name_Availabilty::Exists_Dispatch_Group_Name:
            error(self.loc,
                  "procedure name shadows dispatch group \"{}\"",
                  proc_str);
            return std::nullopt;
        case Name_Availabilty::Available:
            break;
        default:
            std::unreachable();
        }

        current_proc_name = proc_str;
        ctx.procs.emplace(proc_str,
                          Proc { self,
                                 proc_str,
                                 static_cast<s64>(ctx.ops.size()),
                                 sig.value() });
        ctx.ops.emplace_back(self, Op::Kind::Proc_Start, proc_str);

        while (!toks.empty() && toks.back().kind != Token::Kind::Close_Curly) {
            if (!parse_token(toks.back())) {
                note(self.loc, "inside of this procedure body");
                return std::nullopt;
            }
        }

        const auto close_curly = expect(self, Token::Kind::Close_Curly);
        if (!close_curly.has_value()) {
            error(self.loc, "unclosed procedure block");
            note(open_curly->loc, "opened here");
            return std::nullopt;
        }

        ctx.ops.emplace_back(close_curly.value(),
                             Op::Kind::Proc_Return,
                             proc_str);

        current_proc_name = "";

        return std::make_optional(proc_str);
    }

    bool parse_link()
    {
        const auto self = toks.back();
        toks.pop_back();

        if (toks.size() <= 0) {
            error(self.loc,
                  "expected library to link as string, but got nothing");
            return false;
        }

        const auto lib_name = expect(self, Token::Kind::String);

        if (!lib_name) {
            note(self.loc, "for this link directive");
            return false;
        }

        ctx.linker_libs.push_back(std::get<std::string>(lib_name->as));

        return true;
    }

    bool parse_if(bool elif = false)
    {
        const auto self = toks.back();
        toks.pop_back();

        std::string_view block_name = "if";
        if (elif)
            block_name = "elif";

        const auto jump_if_index = ctx.ops.size();

        if (elif)
            ctx.ops.emplace_back(self, Op::Kind::Elif, 1337);
        else
            ctx.ops.emplace_back(self, Op::Kind::If, 1337);

        while (!toks.empty() && toks.back().kind != Token::Kind::Then
               && toks.back().kind != Token::Kind::Else) {
            if (!parse_token(toks.back())) {
                note(self.loc, "inside of this {} block", block_name);
                return false;
            }
        }

        const auto else_or_then_tok
            = expect(self, { Token::Kind::Then, Token::Kind::Else }, !elif);

        if (!else_or_then_tok.has_value()) {
            error(self.loc, "unclosed {} block", block_name);
            return false;
        }

        if (else_or_then_tok->kind == Token::Kind::Else) {
            if (elif)
                toks.pop_back();

            const auto jump_index = ctx.ops.size();
            ctx.ops.emplace_back(else_or_then_tok.value(),
                                 Op::Kind::Else,
                                 1337);
            ctx.ops[jump_if_index].as = static_cast<s64>(ctx.ops.size());

            while (!toks.empty() && toks.back().kind != Token::Kind::Then) {
                if (toks.back().kind == Token::Kind::Elif) {
                    if (!parse_elif())
                        return false;
                } else if (!parse_token(toks.back())) {
                    note(else_or_then_tok->loc, "inside of this else branch");
                    note(self.loc, "inside of this {} block", block_name);
                    return false;
                }
            }

            if (!elif) {
                if (!expect(self, Token::Kind::Then)) {
                    error(else_or_then_tok->loc, "unclosed else branch");
                    note(self.loc, "of this {} block", block_name);
                    return false;
                }

                ctx.ops.emplace_back(else_or_then_tok.value(), Op::Kind::Then);
            }

            ctx.ops[jump_index].as = static_cast<s64>(ctx.ops.size());
        } else { // else_or_then_tok->kind == Token::Kind::Then
            if (elif) {
                error(else_or_then_tok->loc,
                      "`if` with `elif`, but without final `else`, may cause "
                      "unknown stack state if all the checks failed");
                note(self.loc, "last opened `elif` branch");
                note(else_or_then_tok->loc,
                     "consider adding `else` branch to resolve the error");
                return false;
            }
            ctx.ops[jump_if_index].as = static_cast<s64>(ctx.ops.size());
            ctx.ops.emplace_back(else_or_then_tok.value(), Op::Kind::Then);
        }

        return true;
    }

    constexpr bool parse_elif() { return parse_if(true); }

    bool parse_while()
    {
        const auto self = toks.back();
        toks.pop_back();

        const s64 cond_op_index = ctx.ops.size();
        ctx.ops.emplace_back(self, Op::Kind::While);
        while (!toks.empty() && toks.back().kind != Token::Kind::Open_Curly) {
            if (!parse_token(toks.back())) {
                note(self.loc, "inside of this while loop body");
                return false;
            }
        }

        const auto open_curly = expect(self, Token::Kind::Open_Curly);
        if (!open_curly) {
            error(self.loc, "while loops without a body are not allowed");
            return false;
        }

        const u64 jump_if_index = ctx.ops.size();
        ctx.ops.emplace_back(open_curly.value(), Op::Kind::Do, 1337);
        while (!toks.empty() && toks.back().kind != Token::Kind::Close_Curly) {
            if (!parse_token(toks.back())) {
                note(self.loc, "inside of this while loop body");
                return false;
            }
        }

        const auto close_curly = expect(self, Token::Kind::Close_Curly);
        if (!close_curly) {
            error(self.loc, "unclosed while loop block");
            note(open_curly->loc, "opened here");
            return false;
        }
        ctx.ops.emplace_back(close_curly.value(),
                             Op::Kind::End_While,
                             cond_op_index);
        ctx.ops[jump_if_index].as = static_cast<s64>(ctx.ops.size());

        return true;
    }

    bool parse_extern_proc()
    {
        const auto self = toks.back();
        toks.pop_back();
        const auto proc_or_external_name
            = expect(self, { Token::Kind::Proc, Token::Kind::String }, false);
        if (!proc_or_external_name.has_value()) {
            note(self.loc, "for this `extern` construction");
            return false;
        }

        std::string external_name { "" };
        if (proc_or_external_name->kind == Token::Kind::String) {
            external_name = std::get<std::string>(proc_or_external_name->as);
            toks.pop_back();

            if (!expect(self, Token::Kind::Proc)) {
                note(self.loc, "for this `extern` construction");
                return false;
            }
        } else
            toks.pop_back();

        const auto proc_name = expect(self, Token::Kind::Ident);
        if (!proc_name.has_value()) {
            note(self.loc, "as procedure name for this `extern` construction");
            return false;
        }

        const auto sig = parse_type_sig(self);
        if (!sig.has_value()) {
            note(self.loc, "for this `extern` construction");
            return false;
        }

        // TODO: calling convention

        if (!expect(self, Token::Kind::Semicolon).has_value()) {
            note(self.loc, "to end this `extern` construction");
            return false;
        }

        switch (is_name_available(proc_name->text)) {
        case Name_Availabilty::Exists_Proc:
            error(self.loc,
                  "external procedure name shadows procedure \"{}\"",
                  proc_name->text);
            note(ctx.procs.at(proc_name->text).tok.loc,
                 "previously defined here");
            return false;
        case Name_Availabilty::Exists_Extern_Proc:
            error(self.loc,
                  "redefinition of extern procedure \"{}\"",
                  proc_name->text);
            note(ctx.extern_procs.at(proc_name->text).tok.loc,
                 "previously defined here");
            return false;
        case Name_Availabilty::Exists_Builtin_Type:
            error(self.loc,
                  "external procedure name shadows builtin type \"{}\"",
                  proc_name->text);
            return false;
        case Name_Availabilty::Exists_Memory:
            error(self.loc,
                  "external procedure name shadows memory definition \"{}\"",
                  proc_name->text);
            return false;
        case Name_Availabilty::Exists_Dispatch_Group_Name:
            error(self.loc,
                  "external procedure name shadows dispatch group \"{}\"",
                  proc_name->text);
            return false;
        case Name_Availabilty::Available:
            break;
        default:
            std::unreachable();
        }

        if (external_name.empty())
            external_name = proc_name->text;

        ctx.extern_procs.emplace(
            proc_name->text,
            Extern_Proc { self, external_name, sig.value() });

        return true;
    }

    bool parse_include()
    {
        static u64 include_count = 0;
        ++include_count;

        const auto self = toks.back();
        toks.pop_back();

        if (include_count >= 1024) {
            error(self.loc, "too many levels of includes (maximum is 1024)");
            return false;
        }

        if (toks.size() <= 0) {
            error(self.loc, "expected file name as string, but got nothing");
            return false;
        }

        const auto path = expect(self, Token::Kind::String);

        if (!path) {
            note(self.loc, "for this include directive");
            return false;
        }

        const auto include_path
            = std::filesystem::path(std::get<std::string>(path.value().as))
                  .lexically_normal();

        const auto current_dir
            = (std::filesystem::current_path() / current_file)
                  .lexically_normal()
                  .remove_filename();

        std::vector<std::filesystem::path> roots = {
            // relative to input file
            current_dir,
            current_dir / "lib",
            current_dir / "include",
            // relative to current working directory
            // NOTE: may cause problems in the future
            std::filesystem::current_path(),
            std::filesystem::current_path() / "lib",
            std::filesystem::current_path() / "include",
            // NOTE: platform specific paths
            "/usr/include",
            "/usr/lib/depot",
            "/usr/local/include",
            "/usr/local/lib/depot",
        };
        roots.append_range(
            ctx.extra_include_paths
            | std::views::transform([](const std::filesystem::path& path) {
                  return path.is_relative()
                           ? (std::filesystem::current_path() / path)
                                 .lexically_normal()
                           : path.lexically_normal();
              }));
        std::optional<std::filesystem::path> found = std::nullopt;

        if (std::filesystem::exists(include_path))
            found = include_path;
        else
            for (const auto& root : roots) {
                const auto path = root / include_path;
                if (std::filesystem::exists(path)) {
                    found = path.lexically_normal();
                    break;
                }
            }

        if (!found) {
            error(self.loc,
                  "couldn't find file `{}` in the search path",
                  include_path.string());
            note(self.loc, "search path:");
            for (const auto& root : roots)
                std::println("- {}", root.string());
            return false;
        }

        if (included_paths.contains(found.value())) {
            error(self.loc,
                  "multiple includes of the same file are not allowed");
            return false;
        }

        included_paths.insert(found.value());

        Lexer l { found.value() };
        if (l.has_error) {
            note(self.loc, "in file included from here");
            return false;
        }

        std::vector<Token> included_toks;
        if (!l.lex(included_toks)) {
            note(self.loc, "in file included from here");
            return false;
        }

        Parser include_parser { included_toks, ctx, found.value() };
        if (!include_parser.parse()) {
            note(self.loc, "in file included from here");
            return false;
        }

        return true;
    }

    bool parse_memory()
    {
        const auto self = toks.back();
        toks.pop_back();

        const auto name = expect(self, Token::Kind::Ident);
        if (!name.has_value()) {
            note(self.loc, "for this `memory` construction");
            return false;
        }

        const auto size
            = expect(self, Token::Kind::Number); // TODO: constant expressions
        if (!size.has_value()) {
            note(self.loc, "for this `memory` construction");
            return false;
        }

        const auto sz = std::get<s64>(size->as);
        if (sz <= 0) {
            error(size->loc, "size should be at least 1 byte, but got {}", sz);
            note(self.loc, "for this `memory` construction");
            return false;
        }

        ctx.memories.emplace_back(Memory { self,
                                           name->text,
                                           ctx.memories.size(),
                                           static_cast<u64>(sz) });

        return true;
    }

    bool parse_dispatch()
    {
        const auto self = toks.back();
        toks.pop_back();

        const auto name = expect(self, Token::Kind::Ident);
        if (!name.has_value()) {
            note(self.loc, "for this `dispatch` construction");
            return false;
        }

        if (!expect(self, Token::Kind::Proc, false)) {
            note(self.loc, "for this `dispatch` construction");
            return false;
        }

        const auto proc = parse_proc();
        if (!proc.has_value()) {
            note(self.loc, "in the procedure of this dispatch construction");
            return false;
        }

        ctx.dispatch_groups.emplace(name->text, proc.value());

        return true;
    }

    bool parse_token(const Token& t)
    {
        switch (t.kind) {
        case Token::Kind::Proc:
            if (!current_proc_name.empty()) {
                error(t.loc,
                      "defining procedures allowed only in global scope");
                toks.pop_back();
                return false;
            }

            if (!parse_proc())
                return false;
            break;
        case Token::Kind::Link:
            if (!current_proc_name.empty()) {
                error(t.loc, "`link` directive allowed only in global scope");
                toks.pop_back();
                return false;
            }

            if (!parse_link())
                return false;
            break;
        case Token::Kind::Number: {
            if (current_proc_name.empty()) {
                error(t.loc,
                      "pushing numbers onto the stack only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }

            ctx.ops.emplace_back(t, Op::Kind::Push_Int, std::get<s64>(t.as));
            toks.pop_back();
        } break;
        case Token::Kind::Ident:
            switch (is_name_available(t.text)) {
            case Name_Availabilty::Exists_Proc:
                if (current_proc_name.empty()) {
                    error(t.loc,
                          "calling procedures only allowed inside of "
                          "procedure bodies");
                    toks.pop_back();
                    return false;
                }

                ctx.ops.emplace_back(t, Op::Kind::Proc_Call, t.text);
                toks.pop_back();
                break;
            case Name_Availabilty::Exists_Extern_Proc:
                if (current_proc_name.empty()) {
                    error(t.loc,
                          "calling external procedures only allowed inside of "
                          "procedure bodies");
                    toks.pop_back();
                    return false;
                }

                ctx.ops.emplace_back(t, Op::Kind::Extern_Call, t.text);
                toks.pop_back();
                break;
            case Name_Availabilty::Exists_Memory: {
                if (current_proc_name.empty()) {
                    error(t.loc,
                          "pushing global memory definition addresses only "
                          "allowed inside of procedure bodies");
                    toks.pop_back();
                    return false;
                }

                const auto it = find_memory_def(ctx, t.text);
                ASSERT(it != ctx.memories.end(), "Compiler Bug");
                ctx.ops.emplace_back(
                    t,
                    Op::Kind::Push_Global_Memory,
                    std::ranges::distance(ctx.memories.begin(), it));
                toks.pop_back();
            } break;
            case Name_Availabilty::Exists_Dispatch_Group_Name:
                if (current_proc_name.empty()) {
                    error(
                        t.loc,
                        "calling dispatched procedures only allowed inside of "
                        "procedure bodies");
                    toks.pop_back();
                    return false;
                }

                ctx.ops.emplace_back(t, Op::Kind::Dispatch, t.text);
                toks.pop_back();
                break;
            case Name_Availabilty::Exists_Builtin_Type:
            case Name_Availabilty::Available:
                error(t.loc, "unexpected identifier `{}`", t.text);
                toks.pop_back();
                return false;
            }
            break;
        case Token::Kind::String: {
            if (current_proc_name.empty()) {
                error(t.loc,
                      "pushing strings onto the stack only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }

            const auto str = std::get<std::string>(t.as);

            ctx.ops.emplace_back(t,
                                 Op::Kind::Push_Int,
                                 static_cast<s64>(str.size()));
            ctx.ops.emplace_back(t,
                                 Op::Kind::Push_Str,
                                 static_cast<s64>(ctx.strings.size()));
            ctx.strings.push_back(str);
            toks.pop_back();
        } break;
        case Token::Kind::If:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "if blocks only allowed inside of procedure bodies");
                toks.pop_back();
                return false;
            }
            if (!parse_if())
                return false;
            break;
        case Token::Kind::While:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "while loops only allowed inside of procedure bodies");
                toks.pop_back();
                return false;
            }

            if (!parse_while())
                return false;
            break;
        case Token::Kind::Extern:
            if (!current_proc_name.empty()) {
                error(t.loc,
                      "`extern` constructions only allowed in global scope");
                toks.pop_back();
                return false;
            }

            if (!parse_extern_proc())
                return false;
            break;
        case Token::Kind::Drop:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`drop` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Drop);
            toks.pop_back();
            break;
        case Token::Kind::Dup:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`dup` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Dup);
            toks.pop_back();
            break;
        case Token::Kind::Swap:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`swap` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Swap);
            toks.pop_back();
            break;
        case Token::Kind::Rot:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`rot` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Rot);
            toks.pop_back();
            break;
        case Token::Kind::Over:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`Over` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Over);
            toks.pop_back();
            break;
        case Token::Kind::Plus:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`+` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Plus);
            toks.pop_back();
            break;
        case Token::Kind::Minus:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`-` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Minus);
            toks.pop_back();
            break;
        case Token::Kind::Mult:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`*` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Mult);
            toks.pop_back();
            break;
        case Token::Kind::Div:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`/` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Div);
            toks.pop_back();
            break;
        case Token::Kind::Mod:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "`mod` is only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Mod);
            toks.pop_back();
            break;
        case Token::Kind::Equal:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Equal);
            toks.pop_back();
            break;
        case Token::Kind::Not_Equal:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Not_Equal);
            toks.pop_back();
            break;
        case Token::Kind::Less_Than:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Less_Than);
            toks.pop_back();
            break;
        case Token::Kind::Less_Equal:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Less_Equal);
            toks.pop_back();
            break;
        case Token::Kind::Greater_Than:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Greater_Than);
            toks.pop_back();
            break;
        case Token::Kind::Greater_Equal:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Greater_Equal);
            toks.pop_back();
            break;
        case Token::Kind::Load64:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Load64);
            toks.pop_back();
            break;
        case Token::Kind::Load32:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Load32);
            toks.pop_back();
            break;
        case Token::Kind::Load16:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Load16);
            toks.pop_back();
            break;
        case Token::Kind::Load8:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Load8);
            toks.pop_back();
            break;
        case Token::Kind::Store64:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Store64);
            toks.pop_back();
            break;
        case Token::Kind::Store32:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Store32);
            toks.pop_back();
            break;
        case Token::Kind::Store16:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Store16);
            toks.pop_back();
            break;
        case Token::Kind::Store8:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Store8);
            toks.pop_back();
            break;
        case Token::Kind::To_Int64:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::To_Int64);
            toks.pop_back();
            break;
        case Token::Kind::To_Ptr:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::To_Ptr);
            toks.pop_back();
            break;
        case Token::Kind::To_Bool:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::To_Bool);
            toks.pop_back();
            break;
        case Token::Kind::True:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Push_Int, 1);
            ctx.ops.emplace_back(t, Op::Kind::To_Bool);
            toks.pop_back();
            break;
        case Token::Kind::False:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "{} is only allowed inside of "
                      "procedure bodies",
                      human(t.kind));
                toks.pop_back();
                return false;
            }
            ctx.ops.emplace_back(t, Op::Kind::Push_Int, 0);
            ctx.ops.emplace_back(t, Op::Kind::To_Bool);
            toks.pop_back();
            break;
        case Token::Kind::Include:
            if (!current_proc_name.empty()) {
                error(t.loc,
                      "`include` directive allowed only in global scope");
                toks.pop_back();
                return false;
            }

            if (!parse_include())
                return false;
            break;
        case Token::Kind::Memory:
            if (!current_proc_name.empty()) {
                error(t.loc,
                      "`memory` directive allowed only in global scope, for "
                      "now");
                toks.pop_back();
                return false;
            }

            if (!parse_memory())
                return false;
            break;
        case Token::Kind::Dispatch:
            if (!current_proc_name.empty()) {
                error(t.loc,
                      "`dispatch` directive is only allowed in global scope",
                      human(t.kind));
                toks.pop_back();
                return false;
            }

            if (!parse_dispatch())
                return false;
            break;
        case Token::Kind::Elif:
        case Token::Kind::Else:
        case Token::Kind::Then:
        case Token::Kind::Open_Curly:
        case Token::Kind::Semicolon:
        case Token::Kind::Sig_Delimit:
        case Token::Kind::Close_Curly:
            error(t.loc, "unexpected {}", human(t.kind));
            toks.pop_back();
            return false;
        }

        return true;
    }

    bool parse()
    {
        const Scoped_Stopwatch stopwatch { "parsing" };

        while (!toks.empty()) {
            const auto& t = toks.back();

            if (!parse_token(t) || has_error)
                return false;
        }

        return true;
    }
};
// }}}

// CODEGEN {{{

// TODO: should this be inside of codegen namespace?
enum struct Target {
    x86_64_Gas,
};

namespace codegen {

namespace x86_64 {

    static std::optional<std::string> compile(const Da_Thing& ctx)
    {
        std::stringstream out;

        out << ".text\n";

        out << ".globl main\n";
        out << "main:\n";
        out << "\tsubq $8, %rsp\n";
        out << "\tmovq $_depot_stack_end, %r12\n";
        out << "\tmovq %rsp, _depot_saved_rsp\n";
        out << "\tcall _depot_main\n";
        out << "\taddq $8, %rsp\n";
        out << "\tmovq (%r12), %rax\n";
        out << "\tret\n";

        usz ip = 0;
        for (const auto& op : ctx.ops) {
            out << "op_" << ip++ << ": ";

            out << std::format("// {}: {}", op.tok.loc, human(op.kind));
            static_assert(std::variant_size_v<Op::As> == 2,
                          "Exhaustive handling of Op::As variants");
            if (const auto* num = std::get_if<s64>(&op.as)) {
                out << std::format(" {}\n", *num);
            } else if (const auto* str = std::get_if<std::string>(&op.as)) {
                out << std::format(" {:?}\n", *str);
            } else {
                out << '\n';
            }

            switch (op.kind) {
            case Op::Kind::Proc_Start: {
                auto name = std::get<std::string>(op.as);
                if (name == "main")
                    out << "_depot_main:\n";

                out << "\tsubq $8, %rsp\n";
                out << "\tmovq %rsp, _depot_saved_rsp\n";
                out << "\tmovq %r12, %rsp\n";
            } break;
            case Op::Kind::Proc_Return:
                out << "\tmovq %rsp, %r12\n";
                out << "\tmovq _depot_saved_rsp, %rsp\n";
                out << "\taddq $8, %rsp\n";
                out << "\tret\n";
                break;
            case Op::Kind::Proc_Call: {
                const auto proc_name = std::get<std::string>(op.as);
                ASSERT(ctx.procs.contains(proc_name),
                       "Compiler Bug: Proc_Call in Codegen, but the name "
                       "wasn't registered by parser");
                out << "\tmovq %rsp, %r12\n";
                out << "\tmovq _depot_saved_rsp, %rsp\n";
                out << "\tcall op_" << ctx.procs.at(proc_name).op_index << '\n';
                out << "\tmovq %rsp, _depot_saved_rsp\n";
                out << "\tmovq %r12, %rsp\n";
            } break;
            case Op::Kind::Extern_Call: { // TODO: calling convention
                const auto proc_name = std::get<std::string>(op.as);
                ASSERT(ctx.extern_procs.contains(proc_name),
                       "Compiler Bug: Extern_Call in Codegen, but the name "
                       "wasn't registered by parser");
                const auto proc = ctx.extern_procs.at(proc_name);

                const u64 arity = proc.sig.input_types.size();
                const u64 ret_arity = proc.sig.return_types.size();
                switch (arity) {
                default:
                    std::println(stderr,
                                 "{}: error: incorrect arity {} for x86_64 "
                                 "target, should be [0, 6]",
                                 op.tok.loc,
                                 arity);
                    std::println("{}: note: this extern procedure",
                                 proc.tok.loc);
                    return std::nullopt;
                    break;
                case 6:
                    out << "\tpopq %r9\n";
                    [[fallthrough]];
                case 5:
                    out << "\tpopq %r8\n";
                    [[fallthrough]];
                case 4:
                    out << "\tpopq %rcx\n";
                    [[fallthrough]];
                case 3:
                    out << "\tpopq %rdx\n";
                    [[fallthrough]];
                case 2:
                    out << "\tpopq %rsi\n";
                    [[fallthrough]];
                case 1:
                    out << "\tpopq %rdi\n";
                    [[fallthrough]];
                case 0:
                    break;
                }

                out << "\tmovq %rsp, %r12\n";
                out << "\tmovq _depot_saved_rsp, %rsp\n";
                out << "\tcall " << proc.name << '\n';
                out << "\tmovq %rsp, _depot_saved_rsp\n";
                out << "\tmovq %r12, %rsp\n";

                if (ret_arity == 1) {
                    out << "\tpushq %rax\n";
                } else if (ret_arity != 0) {
                    std::println(
                        stderr,
                        "{}: error: incorrect return arity {} for x86_64 "
                        "target, should be [0, 1]",
                        op.tok.loc,
                        ret_arity);
                    std::println("{}: note: this extern procedure",
                                 proc.tok.loc);
                    return std::nullopt;
                }
            } break;
            case Op::Kind::Push_Int:
                out << "\tmovq $" << std::get<s64>(op.as) << ", %rax\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Push_Str:
                out << "\tmovq $_depot_str" << std::get<s64>(op.as)
                    << ", %rax\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Push_Global_Memory: {
                const auto id = std::get<s64>(op.as);
                ASSERT(ctx.memories.size() > static_cast<usz>(id),
                       "Compiler Bug");
                const auto mem = ctx.memories[id];
                out << "\tmovq $_depot_mem" << id << ", %rax\n";
                out << "\tpushq %rax\n";
            } break;
            case Op::Kind::Else:
            case Op::Kind::End_While:
                out << "\tjmp op_" << std::get<s64>(op.as) << '\n';
                break;
            case Op::Kind::If:
            case Op::Kind::Elif:
            case Op::Kind::Do:
                out << "\tpopq %rax\n";
                out << "\ttest %rax, %rax\n";
                out << "\tjz op_" << std::get<s64>(op.as) << '\n';
                break;
            case Op::Kind::Drop:
                out << "\tpopq %rax\n";
                break;
            case Op::Kind::Dup:
                out << "\tpopq %rax\n";
                out << "\tpushq %rax\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Swap:
                out << "\tpopq %rax\n";
                out << "\tpopq %rcx\n";
                out << "\tpushq %rax\n";
                out << "\tpushq %rcx\n";
                break;
            case Op::Kind::Rot:
                out << "\tpopq %rax\n";
                out << "\tpopq %rcx\n";
                out << "\tpopq %rdx\n";
                out << "\tpushq %rax\n";
                out << "\tpushq %rcx\n";
                out << "\tpushq %rdx\n";
                break;
            case Op::Kind::Over:
                out << "\tpopq %rax\n";
                out << "\tpopq %rcx\n";
                out << "\tpushq %rcx\n";
                out << "\tpushq %rax\n";
                out << "\tpushq %rcx\n";
                break;
            case Op::Kind::Plus:
                out << "\tpopq %rax\n";
                out << "\tpopq %rcx\n";
                out << "\taddq %rcx, %rax\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Minus:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tsubq %rcx, %rax\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Mult:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tcqo\n";
                out << "\timulq %rcx\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Div:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tcqo\n";
                out << "\tidivq %rcx\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Mod:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tcqo\n";
                out << "\tidivq %rcx\n";
                out << "\tpushq %rdx\n";
                break;
            case Op::Kind::Equal:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tcmpq %rcx, %rax\n";
                out << "\tsete %al\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Not_Equal:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tcmpq %rcx, %rax\n";
                out << "\tsetne %al\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Less_Than:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tcmpq %rcx, %rax\n";
                out << "\tsetl %al\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Less_Equal:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tcmpq %rcx, %rax\n";
                out << "\tsetle %al\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Greater_Than:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tcmpq %rcx, %rax\n";
                out << "\tsetg %al\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Greater_Equal:
                out << "\tpopq %rcx\n";
                out << "\tpopq %rax\n";
                out << "\tcmpq %rcx, %rax\n";
                out << "\tsetge %al\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Load64:
                out << "\tpopq %rax\n";
                out << "\tmovq (%rax), %rcx\n";
                out << "\tpushq %rcx\n";
                break;
            case Op::Kind::Load32:
                out << "\tpopq %rax\n";
                out << "\tmovl (%rax), %ecx\n";
                out << "\tpushq %rcx\n";
                break;
            case Op::Kind::Load16:
                out << "\tpopq %rax\n";
                out << "\tmovzwl (%rax), %ecx\n";
                out << "\tpushq %rcx\n";
                break;
            case Op::Kind::Load8:
                out << "\tpopq %rax\n";
                out << "\tmovzbl (%rax), %ecx\n";
                out << "\tpushq %rcx\n";
                break;
            case Op::Kind::Store64:
                out << "\tpopq %rax\n";
                out << "\tpopq %rcx\n";
                out << "\tmovq %rcx, (%rax)\n";
                break;
            case Op::Kind::Store32:
                out << "\tpopq %rax\n";
                out << "\tpopq %rcx\n";
                out << "\tmovl %ecx, (%rax)\n";
                break;
            case Op::Kind::Store16:
                out << "\tpopq %rax\n";
                out << "\tpopq %rcx\n";
                out << "\tmovw %cx, (%rax)\n";
                break;
            case Op::Kind::Store8:
                out << "\tpopq %rax\n";
                out << "\tpopq %rcx\n";
                out << "\tmovb %cl, (%rax)\n";
                break;
            case Op::Kind::To_Bool:
                out << "\tpopq %rax\n";
                out << "\tcqo\n";
                out << "\tcmpq $0, %rax\n";
                out << "\tsetne %al\n";
                out << "\tpushq %rax\n";
                break;
            case Op::Kind::Dispatch:
                ASSERT(op.kind == Op::Kind::Dispatch,
                       "Compiler Bug: Dispatch was not resolved in "
                       "typecheker/sema");
                break;
            case Op::Kind::While:
            case Op::Kind::Then:
            case Op::Kind::To_Int64:
            case Op::Kind::To_Ptr:
                break;
            default:
                std::unreachable();
            }
        }

        out << ".data\n";
        for (usz i = 0; i < ctx.strings.size(); ++i) {
            const auto& str = ctx.strings[i];
            out << "_depot_str" << i << ": .byte";
            for (const u8 c : str) {
                out << " 0x" << std::hex << static_cast<u16>(c) << ',';
            }
            out << " 0x0\n";
        }

        out << ".bss\n";
        out << "_depot_saved_rsp: .skip 8\n";
        out << "_depot_stack_bottom: .skip 524288\n";
        out << "_depot_stack_end:\n";

        for (usz i = 0; i < ctx.memories.size(); ++i) {
            const auto mem = ctx.memories[i];
            out << "_depot_mem" << i << ": .skip 0x" << std::hex << mem.size
                << '\n';
        }

        return std::make_optional(out.str());
    }

} // namespace x86_64

} // namespace codegen

static bool
compile(Target tgt, std::filesystem::path input_path, const Da_Thing& ctx)
{
    using Fs_Path = std::filesystem::path;

    const Scoped_Stopwatch stopwatch { "compilation" };

    static constexpr std::string_view build_dir = ".build";

    const auto base_path = input_path.stem();
    const Fs_Path dotbuild { input_path.parent_path() / build_dir };
    std::filesystem::create_directory(dotbuild);

    switch (tgt) {
    case Target::x86_64_Gas: {
        const auto out = codegen::x86_64::compile(ctx);
        if (!out.has_value())
            return false;

        const Fs_Path asm_path { dotbuild / (base_path.string() + ".S") };
        const Fs_Path obj_path { dotbuild / (base_path.string() + ".o") };
        std::ofstream file { asm_path };
        check_fstream_error(file, asm_path.string());
        file << out.value();
        file.close();

        const std::vector<std::string> asm_cmdline = {
            "as", "--64", "-o", obj_path.string(), asm_path.string(),
        };
        execute_command(asm_cmdline);

        std::vector<std::string> ld_cmdline = {
            "ld",
            "-no-pie",
            "-o",
            base_path.string(),
            obj_path.string(),
            "-L.",
            "-Llib",
            "-rpath",
            "$ORIGIN",
            "-rpath",
            "$ORIGIN/lib",
            "-dynamic-linker",
            "/lib64/ld-linux-x86-64.so.2",
            "/lib64/crt1.o",
            "/lib64/crti.o",
            "/lib64/crtn.o",
        };

        for (const auto& lib : ctx.linker_libs)
            ld_cmdline.push_back("-l" + lib);

        execute_command(ld_cmdline);
    } break;
    default:
        std::unreachable();
    }

    return true;
}
// }}}

// TYPECHECKER {{{
static constexpr Type any_type {
    "any_type",
    0,
};

template <bool enable_logging = true>
bool ensure_stack_size(const Op& self,
                       usz have,
                       usz expect,
                       bool strict = false)
{
    if (have == 0 && expect > 0) {
        if constexpr (enable_logging)
            std::println(
                "{}: error: expeted {} values on the stack, but got nothing",
                self.tok.loc,
                expect);
        return false;
    }

    if (!strict) {
        if (have < expect) {
            if constexpr (enable_logging)
                std::println(stderr,
                             "{}: error: expected {} values on the stack, but "
                             "only got {}",
                             self.tok.loc,
                             expect,
                             have);

            return false;
        }
    } else {
        if (have != expect) {
            if constexpr (enable_logging)
                std::println(stderr,
                             "{}: error: expected {} values on the stack, but "
                             "got {}",
                             self.tok.loc,
                             expect,
                             have);

            return false;
        }
    }

    return true;
}

template <bool enable_logging = true>
constexpr bool ensure_stack_size(const Op& self,
                                 std::span<const Type> have,
                                 std::span<const Type> expect,
                                 bool strict = false)
{
    return ensure_stack_size<enable_logging>(self,
                                             have.size(),
                                             expect.size(),
                                             strict);
}

template <bool enable_logging = true>
bool ensure_stack(const Op& self,
                  std::span<const Type> have,
                  std::span<const Type> expect,
                  bool strict = false)
{
    if (!ensure_stack_size<enable_logging>(self, have, expect, strict))
        return false;

    have = have.subspan(have.size() - expect.size());

    if (expect.size() == 0)
        return true;

    for (ssz i = expect.size() - 1; i >= 0; --i) {
        if (expect[i] == any_type)
            continue;

        if (expect[i] != have[i]) {
            if constexpr (enable_logging)
                std::println(
                    stderr,
                    "{}: error: expected argument {} to be of type `{}`, "
                    "but got `{}`",
                    self.tok.loc,
                    expect.size() - i,
                    expect[i].name,
                    have[i].name);
            return false;
        }
    }

    return true;
}

constexpr auto typestack_to_string(std::span<const Type> s)
{
    return std::views::join_with(
        std::views::transform(
            s,
            [](const Type& t) -> std::string_view { return t.name; }),
        " ");
}

constexpr auto proc_to_string(const Proc& proc)
{
    return std::format("proc {} {:s} -- {:s}",
                       proc.name,
                       typestack_to_string(proc.sig.input_types),
                       typestack_to_string(proc.sig.return_types));
}

bool typecheck_and_sema(Da_Thing& ctx)
{
    using Typestack = std::vector<Type>;

    const Scoped_Stopwatch stopwatch { "typechecking" };

    static const Type& int64_type = builtin_types.at("int64");
    static const Type& bool_type = builtin_types.at("bool");
    static const Type& ptr_type = builtin_types.at("ptr");

    Typestack stack;
    std::vector<std::pair<Typestack, Op::Kind>> blocks;

    for (auto& op : ctx.ops) {
        switch (op.kind) {
        case Op::Kind::Proc_Start:
            ASSERT(stack.size() == 0, "Compiler Bug");
            stack.append_range(
                ctx.procs.at(std::get<std::string>(op.as)).sig.input_types);
            break;
        case Op::Kind::Proc_Return:
            if (!ensure_stack(
                    op,
                    stack,
                    ctx.procs.at(std::get<std::string>(op.as)).sig.return_types,
                    true))
                return false;
            stack.clear();
            break;
        case Op::Kind::Proc_Call: {
            const Proc& proc = ctx.procs.at(std::get<std::string>(op.as));
            if (!ensure_stack(op, stack, proc.sig.input_types))
                return false;

            for (usz i = 0; i < proc.sig.input_types.size(); ++i)
                stack.pop_back();

            stack.append_range(proc.sig.return_types);
        } break;
        case Op::Kind::Extern_Call: {
            const Extern_Proc& proc
                = ctx.extern_procs.at(std::get<std::string>(op.as));

            if (!ensure_stack(op, stack, proc.sig.input_types))
                return false;

            for (usz i = 0; i < proc.sig.input_types.size(); ++i)
                stack.pop_back();

            stack.append_range(proc.sig.return_types);
        } break;
        case Op::Kind::Push_Int:
            stack.push_back(int64_type);
            break;
        case Op::Kind::Push_Str:
            stack.push_back(ptr_type);
            break;
        case Op::Kind::Push_Global_Memory:
            stack.push_back(ptr_type);
            break;
        case Op::Kind::Drop:
            if (!ensure_stack_size(op, stack.size(), 1))
                return false;
            stack.pop_back();
            break;
        case Op::Kind::Dup:
            if (!ensure_stack_size(op, stack.size(), 1))
                return false;
            stack.push_back(stack.back());
            break;
        case Op::Kind::Swap: {
            if (!ensure_stack_size(op, stack.size(), 2))
                return false;

            const auto top = stack.back();
            stack.pop_back();

            const auto below = stack.back();
            stack.pop_back();

            stack.push_back(top);
            stack.push_back(below);
        } break;
        case Op::Kind::Rot: {
            if (!ensure_stack_size(op, stack.size(), 3))
                return false;

            const auto top = stack.back();
            stack.pop_back();

            const auto below = stack.back();
            stack.pop_back();

            const auto under = stack.back();
            stack.pop_back();

            stack.push_back(top);
            stack.push_back(below);
            stack.push_back(under);
        } break;
        case Op::Kind::Over:
            if (!ensure_stack_size(op, stack.size(), 2))
                return false;

            stack.push_back(stack[stack.size() - 2]);
            break;
        case Op::Kind::Plus:
        case Op::Kind::Minus:
            if (!ensure_stack_size(op, stack.size(), 2))
                return false;

            if (!ensure_stack<false>(
                    op,
                    stack,
                    std::array<Type, 2> { int64_type, int64_type })
                && !ensure_stack<false>(
                    op,
                    stack,
                    std::array<Type, 2> { ptr_type, ptr_type })) {
                std::println(stderr,
                             "{}: error: expected arguments of {} to be `int64 "
                             "int64` or `ptr ptr`",
                             op.tok.loc,
                             human(op.tok.kind));
                return false;
            }

            stack.pop_back();
            break;
        case Op::Kind::Mult:
        case Op::Kind::Div:
        case Op::Kind::Mod:
            if (!ensure_stack(op,
                              stack,
                              std::array<Type, 2> { int64_type, int64_type }))
                return false;

            stack.pop_back();
            break;
        case Op::Kind::Equal:
        case Op::Kind::Not_Equal:
            if (!ensure_stack_size(op, stack.size(), 2))
                return false;

            if (!ensure_stack<false>(
                    op,
                    stack,
                    std::array<Type, 2> { int64_type, int64_type })
                && !ensure_stack<false>(
                    op,
                    stack,
                    std::array<Type, 2> { ptr_type, ptr_type })
                && !ensure_stack<false>(
                    op,
                    stack,
                    std::array<Type, 2> { bool_type, bool_type })) {
                std::println(stderr,
                             "{}: error: expected arguments of {} to be `int64 "
                             "int64` or `ptr ptr` or `bool bool`",
                             op.tok.loc,
                             human(op.tok.kind));
                return false;
            }

            stack.pop_back();
            stack.pop_back();
            stack.push_back(bool_type);
            break;
        case Op::Kind::Less_Than:
        case Op::Kind::Less_Equal:
        case Op::Kind::Greater_Than:
        case Op::Kind::Greater_Equal:
            if (!ensure_stack_size(op, stack.size(), 2))
                return false;

            if (!ensure_stack<false>(
                    op,
                    stack,
                    std::array<Type, 2> { int64_type, int64_type })
                && !ensure_stack<false>(
                    op,
                    stack,
                    std::array<Type, 2> { ptr_type, ptr_type })) {
                std::println(stderr,
                             "{}: error: expected arguments of {} to be `int64 "
                             "int64` or `ptr ptr`",
                             op.tok.loc,
                             human(op.tok.kind));
                return false;
            }

            stack.pop_back();
            stack.pop_back();
            stack.push_back(bool_type);
            break;
        case Op::Kind::Load64:
        case Op::Kind::Load32:
        case Op::Kind::Load16:
        case Op::Kind::Load8:
            if (!ensure_stack(op, stack, std::array<Type, 1> { ptr_type }))
                return false;

            stack.pop_back();
            stack.push_back(int64_type);
            break;
        case Op::Kind::Store64:
        case Op::Kind::Store32:
        case Op::Kind::Store16:
        case Op::Kind::Store8:
            if (!ensure_stack(op,
                              stack,
                              std::array<Type, 2> { any_type, ptr_type }))
                return false;

            stack.pop_back();
            stack.pop_back();
            break;
        case Op::Kind::To_Int64:
            if (!ensure_stack_size(op, stack.size(), 1))
                return false;
            stack.pop_back();
            stack.push_back(int64_type);
            break;
        case Op::Kind::To_Ptr:
            if (!ensure_stack_size(op, stack.size(), 1))
                return false;
            stack.pop_back();
            stack.push_back(ptr_type);
            break;
        case Op::Kind::To_Bool:
            if (!ensure_stack_size(op, stack.size(), 1))
                return false;
            stack.pop_back();
            stack.push_back(bool_type);
            break;
        case Op::Kind::While:
            blocks.push_back(std::make_pair(stack, op.kind));
            break;
        case Op::Kind::Do: {
            ASSERT(blocks.size() >= 1
                       && (blocks.back().second == Op::Kind::While),
                   "Compiler Bug: Do in typechecker");
            if (!ensure_stack(op, stack, std::array<Type, 1> { bool_type }))
                return false;
            stack.pop_back();
            const auto expected_stack = blocks.back().first;
            if (!ensure_stack<false>(op, stack, expected_stack, true)) {
                std::println(stderr,
                             "{}: error: stack altered by `while-do` condition",
                             op.tok.loc);

                std::println("{}: note: got stack: {:s}",
                             op.tok.loc,
                             typestack_to_string(stack));

                std::println("{}: note: expected stack: {:s}",
                             op.tok.loc,
                             typestack_to_string(expected_stack));

                return false;
            }
        } break;
        case Op::Kind::If:
            if (!ensure_stack(op, stack, std::array<Type, 1> { bool_type }))
                return false;
            stack.pop_back();
            blocks.push_back(std::make_pair(stack, op.kind));
            break;
        case Op::Kind::Else:
            ASSERT(blocks.size() >= 1
                       && (blocks.back().second == Op::Kind::If
                           || blocks.back().second == Op::Kind::Elif),
                   "Compiler Bug: Else in typechecker");
            blocks.push_back(std::make_pair(stack, op.kind));
            stack = blocks.at(blocks.size() - 2).first;
            break;
        case Op::Kind::Then:
            if (blocks.back().second == Op::Kind::If) {
                const auto expected_stack = blocks.back().first;
                blocks.pop_back();

                if (!ensure_stack<false>(op, stack, expected_stack, true)) {
                    std::println(stderr,
                                 "{}: error: stack altered by `if` block",
                                 op.tok.loc);

                    std::println("{}: note: got stack: {:s}",
                                 op.tok.loc,
                                 typestack_to_string(stack));

                    std::println("{}: note: expected stack: {:s}",
                                 op.tok.loc,
                                 typestack_to_string(expected_stack));

                    return false;
                }
            } else if (blocks.back().second == Op::Kind::Else) {
                std::vector<Typestack> expected_stacks;

                do {
                    expected_stacks.push_back(blocks.back().first);
                    blocks.pop_back();
                } while (!blocks.empty()
                         && blocks.back().second != Op::Kind::If);

                ASSERT(
                    !blocks.empty() && blocks.back().second == Op::Kind::If,
                    "Compiler Bug: expected to have If block's stack, but "
                    "something went wrong and we either didn't get anything, "
                    "or got something that isn't an If block stack");
                blocks.pop_back(); // original stack from If

                expected_stacks.push_back(stack);

                while (expected_stacks.size() >= 2) {
                    const auto have = expected_stacks.back();
                    expected_stacks.pop_back();

                    if (!ensure_stack<false>(op,
                                             have,
                                             expected_stacks.back(),
                                             true)) {
                        std::println(
                            stderr,
                            "{}: error: stack differs in if block branches",
                            op.tok.loc);

                        std::println("{}: note: got stack: {:s}",
                                     op.tok.loc,
                                     typestack_to_string(have));

                        std::println(
                            "{}: note: expected stack: {:s}",
                            op.tok.loc,
                            typestack_to_string(expected_stacks.back()));

                        return false;
                    }
                }

                expected_stacks.pop_back();
            } else
                ASSERT(false,
                       "Compiler Bug: unreachable in typechecker, Then closes "
                       "{} operation and this is not allowed",
                       human(blocks.back().second));
            break;
        case Op::Kind::End_While: {
            ASSERT(blocks.back().second == Op::Kind::While,
                   "Compiler Bug: unreachable in typechecker, End_While closes "
                   "{} operation and this is not allowed",
                   human(blocks.back().second));

            const auto expected_stack = blocks.back().first;
            if (!ensure_stack<false>(op, stack, expected_stack, true)) {
                std::println(stderr,
                             "{}: error: stack altered by `do` block (part of "
                             "`while` block)",
                             op.tok.loc);

                std::println("{}: note: got stack: {:s}",
                             op.tok.loc,
                             typestack_to_string(stack));

                std::println("{}: note: expected stack: {:s}",
                             op.tok.loc,
                             typestack_to_string(expected_stack));

                return false;
            }
        } break;
        case Op::Kind::Elif: {
            ASSERT(blocks.back().second == Op::Kind::Else
                       || blocks.back().second == Op::Kind::Elif,
                   "Compiler Bug: unreachable in typechecker, Else_If closes "
                   "{} operation and this is not allowed",
                   human(blocks.back().second));

            if (!ensure_stack(op, stack, std::array<Type, 1> { bool_type }))
                return false;

            stack.pop_back();
            blocks.push_back(std::make_pair(stack, op.kind));

            const auto original_stack = std::ranges::find_last_if(
                blocks,
                [](const std::pair<Typestack, Op::Kind> p) -> bool {
                    return p.second == Op::Kind::If;
                });

            ASSERT(original_stack.begin() != blocks.end(),
                   "Compiler Bug: couldn't find block opened by If when "
                   "typechecking Else_If");

            stack = original_stack.back().first;
        } break;
        case Op::Kind::Dispatch: {
            std::vector<Proc> matches;
            const auto name = std::get<std::string>(op.as);
            ASSERT(ctx.dispatch_groups.contains(name),
                   "Compiler Bug: Dispatch in typecheck, but the name wasn't "
                   "registered by parser");
            const auto dispatch_procs = ctx.dispatch_groups.equal_range(name);
            for (auto it = dispatch_procs.first; it != dispatch_procs.second;
                 ++it) {
                const auto proc = ctx.procs.at(it->second);
                if (ensure_stack<false>(op, stack, proc.sig.input_types))
                    matches.push_back(proc);
            }

            if (matches.empty()) {
                std::println(stderr,
                             "{}: error: no matching candidate for '{}'",
                             op.tok.loc,
                             name);
                for (auto it = dispatch_procs.first;
                     it != dispatch_procs.second;
                     ++it) {
                    const auto proc = ctx.procs.at(it->second);
                    std::println("{}: note: candidate: `{}`",
                                 proc.tok.loc,
                                 proc_to_string(proc));
                }

                return false;
            }

            if (matches.size() > 1) {
                std::println(stderr,
                             "{}: error: ambiguous call to '{}'",
                             op.tok.loc,
                             name);
                for (const auto& proc : matches) {
                    std::println("{}: note: matched candidate: `{}`",
                                 proc.tok.loc,
                                 proc_to_string(proc));
                }

                return false;
            }

            const auto& proc = matches.back();

            // NOTE: copypasting
            if (!ensure_stack(op, stack, proc.sig.input_types)) {
                std::println("{}: note: for this dispatched procedure",
                             proc.tok.loc);
                return false;
            }

            for (usz i = 0; i < proc.sig.input_types.size(); ++i)
                stack.pop_back();

            stack.append_range(proc.sig.return_types);
            op.kind = Op::Kind::Proc_Call;
            op.as = proc.name;
        } break;
        }
    }

    return true;
}
// }}}

void usage(FILE* out, std::string_view program_name)
{
    std::println(out, "Usage: {} [FLAGS] <file.dpt>", program_name);
    std::println(out, "FLAGS:");
    std::println(out, "\t-I|-include <dir>\t\tadditional include directory");
}

int main(int argc, char** argv)
{
    std::string program_name { argv[0] };

    argc--;
    argv++;

    Da_Thing ctx;

    std::vector<std::string> arg_stack { argv, argv + argc };
    std::ranges::reverse(arg_stack);

    std::vector<std::string> positionals;
    while (!arg_stack.empty()) {
        const auto arg = arg_stack.back();
        arg_stack.pop_back();

        if (arg.starts_with("-")) {
            if (arg == "-I" || arg == "-include") {
                if (arg_stack.empty()) {
                    usage(stderr, program_name);
                    std::println(stderr,
                                 "error: expected path, but got nothing");
                    return 1;
                }
                ctx.extra_include_paths.push_back(arg_stack.back());
                arg_stack.pop_back();
            } else {
                usage(stderr, program_name);
                std::println(stderr, "error: unknown flag '{}'", arg);
                return 1;
            }
        } else
            positionals.push_back(arg);
    }

    if (positionals.empty()) {
        usage(stderr, program_name);
        std::println(stderr, "erorr: expected file path, but got nothing");
        return 1;
    } else if (positionals.size() > 1) {
        usage(stderr, program_name);
        std::println(stderr,
                     "erorr: too many positional arguments, expected only one, "
                     "but got {}",
                     positionals.size());
        return 1;
    }

    std::filesystem::path file_path { positionals.back() };
    file_path = file_path.lexically_normal();

    included_paths.insert(file_path);

    Lexer l { file_path };
    if (l.has_error)
        return 2;

    std::vector<Token> toks;
    if (!l.lex(toks))
        return 3;

#ifdef DEPOT_DEBUG
    std::println("TOKS:");
    for (const auto& t : toks) {
        std::println("{}: {} {:?}", t.loc, human(t.kind), t.text);
    }
#endif

    Parser p { toks, ctx, file_path };

    if (!p.parse())
        return 4;

#ifdef DEPOT_DEBUG
    std::println("OPS:");
    for (const auto& op : ctx.ops) {
        std::print("{}: {}", op.tok.loc, human(op.kind));
        static_assert(std::variant_size_v<Op::As> == 2,
                      "Exhaustive handling of Op::As variants");
        if (std::holds_alternative<s64>(op.as))
            std::println(" {}", std::get<s64>(op.as));
        else if (std::holds_alternative<std::string>(op.as))
            std::println(" {:?}", std::get<std::string>(op.as));
        else
            std::unreachable();
    }
#endif

    if (!ctx.procs.contains("main")) {
        const auto file_path_str = file_path.string();
        std::println(stderr,
                     "{}: error: no `main` procedure found",
                     file_path_str);
        std::println("{}: note: consider adding procedure main", file_path_str);
        return 5;
    }

    static const Type_Sig expected_sig { { }, { builtin_types.at("int64") } };
    if (const auto main_proc = ctx.procs.at("main");
        main_proc.sig != expected_sig) {
        std::println(stderr,
                     "{}: error: expected `main` procedure to have type "
                     "signature ` -- int64`, but got `{:s} -- {:s}`",
                     main_proc.tok.loc,
                     typestack_to_string(main_proc.sig.input_types),
                     typestack_to_string(main_proc.sig.return_types));
        return 5;
    }

    if (!typecheck_and_sema(ctx))
        return 5;

    if (!compile(Target::x86_64_Gas, file_path, ctx))
        return 6;

    for (const auto& [name, time] : performance_map)
        std::println("{}: {}ms", name, time);
}

// Copyright (c) 2026 EndeyshentLabs <Themikfound@gmail.com>
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//     2. Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
