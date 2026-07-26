// $ c++ -O3 -std=c++23 depot.cc -o depot
// SPDX-License-Identifier: BSD-2-Clause
// See bottom of the file for full license text

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <print>
#include <ranges>
#include <set>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32

#error "windows platform is not supported"

#else // !defined(_WIN32)

#include <sys/wait.h>
#include <unistd.h>

#endif // #ifdef _WIN32

// {{{
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

// {{{
#define ASSERT(cond, ...)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::print(stderr, "ASSERTION `{}` FAILED: ", #cond);              \
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

// {{{
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
        args[i] = cmd_line.at(i).c_str();
    }
    args[cmd_line.size()] = nullptr;

    ::pid_t child_pid = ::fork();
    if (child_pid == -1) {
        std::println(stderr, "error: fork failed: {}", std::strerror(errno));
        std::exit(1);
    } else if (child_pid == 0) { // child
        ::execvp(cmd_line.at(0).c_str(), (char* const*)args);
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
                             WEXITSTATUS(wstatus));
                std::exit(WEXITSTATUS(wstatus));
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

// {{{
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

// {{{

struct Token {
    Location loc;
    enum struct Kind {
        Proc,
        Link,
        Ident,
        Open_Curly,
        Close_Curly,
        Number,
        String,

        Extern,
        Semicolon, // TODO: better name?

        Drop,
        Dup,
        Swap,
        Over,
        Plus,
        Minus,
        Mult,
        Div,
    } kind;
    std::string text;
};

static const std::unordered_map<std::string_view, Token::Kind> keywords = {
    { "proc", Token::Kind::Proc },     { "link", Token::Kind::Link },
    { "{", Token::Kind::Open_Curly },  { "}", Token::Kind::Close_Curly },
    { "drop", Token::Kind::Drop },     { "dup", Token::Kind::Dup },
    { "swap", Token::Kind::Swap },     { "over", Token::Kind::Over },

    { "+", Token::Kind::Plus },        { "-", Token::Kind::Minus },
    { "*", Token::Kind::Mult },        { "/", Token::Kind::Div },
    { "extern", Token::Kind::Extern }, { ";", Token::Kind::Semicolon },
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
    case Token::Kind::Extern:
        return plural ? "`extern` keywords" : "`extern` keyword";
    case Token::Kind::Semicolon:
        return ";";
    case Token::Kind::Drop:
        return plural ? "`drop` keywords" : "`drop` keyword";
    case Token::Kind::Dup:
        return plural ? "`drop` keywords" : "`drop` keyword";
    case Token::Kind::Swap:
        return plural ? "`drop` keywords" : "`drop` keyword";
    case Token::Kind::Over:
        return plural ? "`drop` keywords" : "`drop` keyword";
    case Token::Kind::Plus:
        return "`+`";
    case Token::Kind::Minus:
        return "`-`";
    case Token::Kind::Mult:
        return "`*`";
    case Token::Kind::Div:
        return "`/`";
    default:
        std::unreachable();
    }
}

struct Lexer {
    char c { 0 };
    usz cursor { 0 };
    Location loc { };
    std::string source { };

    Lexer(const std::filesystem::path& path)
    {
        std::ifstream file { path };

        if (!file.is_open()) {
            std::string reason = "Unknown error";
            if (file.fail())
                reason = std::strerror(errno);

            std::println(stderr,
                         "{}: error: Couldn't open: {}",
                         path.string(),
                         reason);
            std::exit(2);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        source = buffer.str();

        loc.file_path = path;
        if (source.empty()) {
            ;
            std::println(stderr, "{}: error: Empty file", loc);
            std::println("{}: note: consider adding procedure main", loc);
            std::println("|\t// minimal program:");
            std::println("|\tlink \"c\"\n|\tproc main {{ 0 }}");
            std::exit(2);
        }
        c = source.at(cursor);
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
        cursor++;
        c = source.at(cursor);

        if (c == '\n') {
            loc.row++;
            loc.col = 0;
        } else {
            loc.col++;
        }

        return true;
    }

    constexpr char peek(usz offset = 1) const
    {
        if (cursor + offset >= source.size() - 1)
            return 0;
        return source.at(cursor + offset);
    }

    std::vector<Token> lex()
    {
        std::vector<Token> toks;

        do {
            Location start_loc = loc;

            if (std::isspace(c)) {
                // ignore
            } else if (c == '/' && peek() == '/') {
                do
                    advance();
                while (c != '\n');
            } else if (c == '"') {
                std::string text;
                advance(); // consume opening "

                do {
                    text.push_back(c);
                    advance();
                } while (c != '"');

                // consume closing "
                advance();

                if (!std::isspace(c)) {
                    std::println(stderr,
                                 "{}: error: junk at the end of the string",
                                 loc);
                    std::exit(3);
                }

                toks.emplace_back(start_loc, Token::Kind::String, text);
            } else if ((c == '-' && std::isdigit(peek())) || std::isdigit(c)) {
                std::string text;

                do {
                    text.push_back(c);
                    advance();
                } while (std::isdigit(c));

                if (!std::isspace(c)) {
                    do {
                        text.push_back(c);
                        advance();
                    } while (!std::isspace(c));
                    toks.emplace_back(start_loc, Token::Kind::Ident, text);
                } else {
                    toks.emplace_back(start_loc, Token::Kind::Number, text);
                }
            } else if (!std::isspace(c)) {
                std::string text;

                do {
                    text.push_back(c);
                    advance();
                } while (!std::isspace(c));

                if (keywords.contains(text)) {
                    toks.emplace_back(start_loc, keywords.at(text), text);
                } else {
                    toks.emplace_back(start_loc, Token::Kind::Ident, text);
                }
            } else {
                std::println(stderr,
                             "{}: error: unexpected character {}",
                             loc,
                             c);
                std::exit(3);
            }
        } while (advance());

        return toks;
    }
};

// }}}

// {{{

struct Op {
    using As = std::variant<s64, std::string>;

    Token tok;
    enum struct Kind {
        Proc_Start, // operand(str): procedure name
        Proc_Return, // operand(str): procedure name
        Proc_Call, // operand(str): procedure name
        Extern_Call, // operand(str): procedure name
        Push_Int, // operand(int64): number
        Push_Str, // operand(int64): index inside of `Da_Thing::strings`

        Drop, // no operand (0 as int64)
        Dup, // no operand (0 as int64)
        Swap, // no operand (0 as int64)
        Over, // no operand (0 as int64)
        Plus, // no operand (0 as int64)
        Minus, // no operand (0 as int64)
        Mult, // no operand (0 as int64)
        Div, // no operand (0 as int64)
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
    case Op::Kind::Push_Int:
        return "Push_Int";
    case Op::Kind::Push_Str:
        return "Push_Str";
    case Op::Kind::Drop:
        return "Drop";
    case Op::Kind::Dup:
        return "Dup";
    case Op::Kind::Swap:
        return "Swap";
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
    default:
        std::unreachable();
    }
}

struct Proc {
    Token tok;
    std::string name;
};

struct Extern_Proc {
    Token tok;
    std::string name;
    u64 arity;
    // TODO: calling convention
};

struct Da_Thing {
    std::vector<std::string> linker_libs;
    std::vector<Op> ops;
    std::vector<std::string> strings;
    std::unordered_map<std::string, Extern_Proc> extern_procs;
};

struct Parser {
    std::vector<Token> toks;
    bool has_error { false };
    std::vector<std::string> linker_libs;
    std::unordered_map<std::string, Proc> procs;
    std::unordered_map<std::string, Extern_Proc> extern_procs;
    std::string_view current_proc_name { };
    std::vector<std::string> strings;

    Parser(std::span<Token> toks)
        : toks { toks.size() }
    {
        this->toks.assign_range(std::ranges::reverse_view(toks));
    }

    void error(const Location& loc, std::string_view text)
    {
        has_error = true;
        std::println(stderr, "{}: error: {}", loc, text);
    }

    void note(const Location& loc, std::string_view text)
    {
        std::println("{}: note: {}", loc, text);
    }

    std::optional<Token> expect(const Token& self, Token::Kind kind)
    {
        if (toks.size() <= 0) {
            error(self.loc,
                  std::format("expected {}, but got nothing", human(kind)));
            return std::nullopt;
        }

        Token tok = toks.back();
        toks.pop_back();

        if (tok.kind != kind) {
            error(tok.loc,
                  std::format("expected {}, but got {}",
                              human(kind),
                              human(tok.kind)));
            return std::nullopt;
        }

        return std::make_optional(tok);
    }

    std::optional<Token> expect(const Token& self, std::set<Token::Kind> kinds)
    {
        const auto kind_strings
            = kinds
            | std::ranges::views::transform(
                  [](Token::Kind k) -> std::string_view { return human(k); });
        const auto ored_kinds = std::ranges::to<std::string>(
            std::views::join_with(kind_strings, " or "));

        if (toks.size() <= 0) {
            error(self.loc,
                  std::format("expected {:s}, but got nothing", ored_kinds));
            return std::nullopt;
        }

        Token tok = toks.back();
        toks.pop_back();

        if (!kinds.contains(tok.kind)) {
            error(tok.loc,
                  std::format("expected {:s}, but got {}",
                              ored_kinds,
                              human(tok.kind)));
            return std::nullopt;
        }

        return std::make_optional(tok);
    }

    bool parse_proc(std::vector<Op>& ops)
    {
        const auto self = toks.back();
        toks.pop_back();

        const auto proc_name = expect(self, Token::Kind::Ident);
        if (!proc_name) {
            note(self.loc, "for this procedure definition");
            return false;
        }

        const auto& open_curly = expect(self, Token::Kind::Open_Curly);
        if (!open_curly) {
            note(self.loc, "for this procedure definition");
            return false;
        }

        if (procs.contains(proc_name->text)) {
            error(self.loc,
                  std::format("redefinition of procedure \"{}\"",
                              proc_name->text));
            note(procs.at(proc_name->text).tok.loc, "previously defined here");
            return false;
        }

        if (extern_procs.contains(proc_name->text)) {
            error(
                self.loc,
                std::format("procedure name shadows external procedure \"{}\"",
                            proc_name->text));
            note(extern_procs.at(proc_name->text).tok.loc,
                 "previously defined here");
            return false;
        }

        procs.emplace(proc_name->text, Proc { self, proc_name->text });
        current_proc_name = proc_name->text;
        ops.emplace_back(self, Op::Kind::Proc_Start, proc_name->text);

        while (!toks.empty() && toks.back().kind != Token::Kind::Close_Curly) {
            if (!parse_token(ops, toks.back())) {
                note(self.loc, "inside of this procedure body");
                return false;
            }
        }
        if (!expect(self, Token::Kind::Close_Curly)) {
            error(self.loc, "unclosed procedure block");
            note(open_curly->loc, "opened here");
            return false;
        }

        ops.emplace_back(self, Op::Kind::Proc_Return, proc_name->text);

        current_proc_name = "";

        return true;
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

        linker_libs.push_back(lib_name->text);

        return true;
    }

    bool parse_extern_proc()
    {
        const auto self = toks.back();
        toks.pop_back();
        const auto prockwd = expect(self, Token::Kind::Proc);
        if (!prockwd.has_value()) {
            note(self.loc, "for this `extern` construction");
            return false;
        }

        const auto proc_name = expect(self, Token::Kind::Ident);
        if (!proc_name.has_value()) {
            note(self.loc, "as procedure name for this `extern` construction");
            return false;
        }

        const auto arity = expect(self, Token::Kind::Number);
        if (!arity.has_value()) {
            note(self.loc, "as arity for this `extern` construction");
            return false;
        }

        // TODO: calling convention

        errno = 0;
        const s64 num = std::strtoll(arity->text.c_str(), nullptr, 10);
        if (errno == ERANGE) {
            error(arity->loc,
                  std::format("number out of range (accepted range [{}, {}])",
                              std::numeric_limits<decltype(num)>::min(),
                              std::numeric_limits<decltype(num)>::max()));
            return false;
        }

        if (!expect(self, Token::Kind::Semicolon).has_value()) {
            note(self.loc, "to end this `extern` construction");
            return false;
        }

        if (extern_procs.contains(proc_name->text)) {
            error(self.loc,
                  std::format("redefinition of extern procedure \"{}\"",
                              proc_name->text));
            note(extern_procs.at(proc_name->text).tok.loc,
                 "previously defined here");
            return false;
        }

        if (procs.contains(proc_name->text)) {
            error(
                self.loc,
                std::format("external procedure name shadows procedure \"{}\"",
                            proc_name->text));
            note(procs.at(proc_name->text).tok.loc, "previously defined here");
            return false;
        }

        extern_procs.emplace(
            proc_name->text,
            Extern_Proc { self, proc_name->text, static_cast<u64>(num) });

        return true;
    }

    bool parse_token(std::vector<Op>& ops, const Token& t)
    {
        switch (t.kind) {
        case Token::Kind::Proc:
            if (!current_proc_name.empty()) {
                error(t.loc,
                      "defining procedures allowed only in global scope");
                toks.pop_back();
                return false;
            }

            if (!parse_proc(ops))
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

            errno = 0;
            const s64 num = std::strtoll(t.text.c_str(), nullptr, 10);
            if (errno == ERANGE) {
                error(
                    t.loc,
                    std::format("number out of range (accepted range [{}, {}])",
                                std::numeric_limits<decltype(num)>::min(),
                                std::numeric_limits<decltype(num)>::max()));
                return false;
            }

            ops.emplace_back(t, Op::Kind::Push_Int, num);
            toks.pop_back();
        } break;
        case Token::Kind::Ident:
            if (procs.contains(t.text)) { // func call
                if (current_proc_name.empty()) {
                    error(t.loc,
                          "calling procedures only allowed inside of "
                          "procedure bodies");
                    toks.pop_back();
                    return false;
                }

                ops.emplace_back(t, Op::Kind::Proc_Call, t.text);
                toks.pop_back();
            } else if (extern_procs.contains(t.text)) { // extern call
                if (current_proc_name.empty()) {
                    error(t.loc,
                          "calling external procedures only allowed inside of "
                          "procedure bodies");
                    toks.pop_back();
                    return false;
                }

                ops.emplace_back(t, Op::Kind::Extern_Call, t.text);
                toks.pop_back();
            } else {
                error(t.loc, std::format("unexpected identifier `{}`", t.text));
                toks.pop_back();
                return false;
            }
            break;
        case Token::Kind::String:
            if (current_proc_name.empty()) {
                error(t.loc,
                      "pushing strings onto the stack only allowed inside of "
                      "procedure bodies");
                toks.pop_back();
                return false;
            }

            ops.emplace_back(t,
                             Op::Kind::Push_Int,
                             static_cast<s64>(t.text.size()));
            ops.emplace_back(t,
                             Op::Kind::Push_Str,
                             static_cast<s64>(strings.size()));
            strings.push_back(t.text);
            toks.pop_back();
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
            ops.emplace_back(t, Op::Kind::Drop);
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
            ops.emplace_back(t, Op::Kind::Dup);
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
            ops.emplace_back(t, Op::Kind::Swap);
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
            ops.emplace_back(t, Op::Kind::Over);
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
            ops.emplace_back(t, Op::Kind::Plus);
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
            ops.emplace_back(t, Op::Kind::Minus);
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
            ops.emplace_back(t, Op::Kind::Mult);
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
            ops.emplace_back(t, Op::Kind::Div);
            toks.pop_back();
            break;
        case Token::Kind::Open_Curly:
        case Token::Kind::Semicolon:
        case Token::Kind::Close_Curly:
            error(t.loc, std::format("unexpected {}", human(t.kind)));
            toks.pop_back();
            return false;
        }

        return true;
    }

    Da_Thing parse()
    {
        std::vector<Op> ops;

        while (!toks.empty()) {
            const auto& t = toks.back();

            parse_token(ops, t);

            if (has_error)
                std::exit(4);
        }

        return {
            .linker_libs = linker_libs,
            .ops = ops,
            .strings = strings,
            .extern_procs = extern_procs,
        };
    }
};

// }}}

// {{{

// TODO: should this be inside of codegen namespace?
enum struct Target {
    x86_64_Gas,
};

namespace codegen {

namespace x86_64 {

    static std::string compile(const Da_Thing& ctx)
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
                    name = "_depot_main";

                // out << "\t.globl " << name << '\n';
                out << name << ":\n";
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
            case Op::Kind::Proc_Call:
                out << "\tmovq %rsp, %r12\n";
                out << "\tmovq _depot_saved_rsp, %rsp\n";
                out << "\tcall " << std::get<std::string>(op.as) << '\n';
                out << "\tmovq %rsp, _depot_saved_rsp\n";
                out << "\tmovq %r12, %rsp\n";
                break;
            case Op::Kind::Extern_Call: { // TODO: calling convention
                const auto proc_name = std::get<std::string>(op.as);
                ASSERT(ctx.extern_procs.contains(proc_name),
                       "Compiler Bug: Extern_Call in Codegen, but the name "
                       "wasn't registered by parser");
                const auto proc = ctx.extern_procs.at(proc_name);

                switch (proc.arity) {
                default:
                    std::println(stderr,
                                 "{}: error: incorrect arity {} for x86_64 "
                                 "target, should be [0, 6]",
                                 op.tok.loc,
                                 proc.arity);
                    std::println("{}: note: this extern procedure",
                                 proc.tok.loc);
                    std::exit(5);
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
                out << "\tcall " << proc_name << '\n';
                out << "\tmovq %rsp, _depot_saved_rsp\n";
                out << "\tmovq %r12, %rsp\n";
                out << "\tpushq %rax\n";
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
            default:
                std::unreachable();
            }
        }

        out << ".data\n";
        for (usz i = 0; i < ctx.strings.size(); ++i) {
            const auto& str = ctx.strings.at(i);
            out << "_depot_str" << i << ": .byte";
            for (const u8 c : str) {
                out << " 0x" << std::hex << (u16)c << ',';
            }
            out << " 0x0\n";
        }

        out << ".bss\n";
        out << "_depot_saved_rsp: .skip 8\n";
        out << "_depot_stack_bottom: .skip 524288\n";
        out << "_depot_stack_end:\n";

        return out.str();
    }

} // namespace x86_64

} // namespace codegen

static void
compile(Target tgt, std::filesystem::path input_path, const Da_Thing& ctx)
{
    using Fs_Path = std::filesystem::path;

    static constexpr std::string_view build_dir = ".build";

    const auto base_path = input_path.stem();
    const Fs_Path dotbuild { input_path.parent_path() / build_dir };
    std::filesystem::create_directory(dotbuild);

    switch (tgt) {
    case Target::x86_64_Gas: {
        const auto out = codegen::x86_64::compile(ctx);

        const Fs_Path asm_path { dotbuild / (base_path.string() + ".S") };
        const Fs_Path obj_path { dotbuild / (base_path.string() + ".o") };
        std::ofstream file { asm_path };
        ASSERT(file.is_open() && !file.fail(), "couldn't open file");
        file << out;
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
}

// }}}

void usage(FILE* out, const char* program_name)
{
    std::println(out, "Usage: {} <file.dpt>", program_name);
}

int main(int argc, char** argv)
{
    const char* program_name = argv[0];

    if (argc != 2) {
        usage(stderr, program_name);
        return 1;
    }

    const std::string argv1 { argv[1] };
    const std::filesystem::path file_path = argv1;

    Lexer l { file_path };
    auto toks = l.lex();

#ifdef DEPOT_DEBUG
    std::println("TOKS:");
    for (const auto& t : toks) {
        std::println("{}: {} {:?}", t.loc, human(t.kind), t.text);
    }
#endif

    Parser p { toks };
    auto o = p.parse();

#ifdef DEPOT_DEBUG
    std::println("OPS:");
    for (const auto& op : o.ops) {
        std::print("{}: {}", op.tok.loc, human(op.kind));
        static_assert(std::variant_size_v<Op::As> == 2,
                      "Exhaustive handling of Op::As variants");
        if (const auto* num = std::get_if<s64>(&op.as)) {
            std::println(" {}", *num);
        } else if (const auto* str = std::get_if<std::string>(&op.as)) {
            std::println(" {:?}", *str);
        } else {
            std::println();
        }
    }
#endif

    compile(Target::x86_64_Gas, file_path, o);
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
