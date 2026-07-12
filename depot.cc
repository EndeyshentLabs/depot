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
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
    enum class Kind {
        Proc,
        Link,
        Ident,
        Open_Curly,
        Close_Curly,
        Number,
        String,
    } kind;
    std::string text;
};

static const std::unordered_map<std::string_view, Token::Kind> keywords = {
    { "proc", Token::Kind::Proc },
    { "link", Token::Kind::Link },
    { "{", Token::Kind::Open_Curly },
    { "}", Token::Kind::Close_Curly },
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

        loc.file_path = path.string();
        if (source.empty()) {
            ;
            std::println(stderr, "{}: error: Empty file", loc);
            std::println("{}: note: consider adding procedure main", loc);
            std::println("\tproc main {{ 0 }} // minimal program");
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
        loc.col++;
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
            if (c == '\n') {
                loc.row++;
                loc.col = 0;
            } else if (std::isspace(c)) {
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
    using As = std::variant<int64_t, std::string>;

    Token tok;
    enum class Kind {
        Proc_Start,
        Proc_Return,
        Proc_Call,
        Push_Int,
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
    case Op::Kind::Push_Int:
        return "Push_Int";
    default:
        std::unreachable();
    }
}

struct Da_Thing {
    std::vector<std::string> linker_libs;
    std::vector<Op> ops;
    std::vector<std::string> strings;
};

struct Proc {
    Token tok;
    std::string name;
};

struct Parser {
    std::vector<Token> toks;
    bool has_error { false };
    std::vector<std::string> linker_libs;
    std::unordered_map<std::string, Proc> procs;
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

    Token* expect(const Token& self, Token::Kind kind)
    {
        if (toks.size() <= 0) {
            error(self.loc,
                  std::format("expected {}, but got nothing", human(kind)));
            return nullptr;
        }

        Token* tok = &toks.back();
        toks.pop_back();

        if (tok->kind != kind) {
            error(tok->loc,
                  std::format("expected {}, but got {}",
                              human(kind),
                              human(tok->kind)));
            return nullptr;
        }

        return tok;
    }

    void parse_proc(std::vector<Op>& ops)
    {
        const auto& self = toks.back();
        toks.pop_back();

        const auto* proc_name = expect(self, Token::Kind::Ident);
        if (!proc_name) {
            note(self.loc, "for this procedure definition");
            return;
        }

        const auto& open_curly = expect(self, Token::Kind::Open_Curly);
        if (!open_curly) {
            note(self.loc, "for this procedure definition");
            return;
        }

        procs.emplace(proc_name->text, Proc { self, proc_name->text });
        current_proc_name = proc_name->text;
        ops.emplace_back(self, Op::Kind::Proc_Start, proc_name->text);

        while (!toks.empty() && toks.back().kind != Token::Kind::Close_Curly) {
            parse_token(ops, toks.back());
        }
        if (!expect(self, Token::Kind::Close_Curly)) {
            error(self.loc, "unclosed procedure block");
            note(open_curly->loc, "opened here");
            return;
        }
        ops.emplace_back(self, Op::Kind::Proc_Return, proc_name->text);

        current_proc_name = "";
    }

    void parse_link()
    {
        const auto& self = toks.back();
        toks.pop_back();

        if (toks.size() <= 0) {
            error(self.loc,
                  "expected library to link as string, but got nothing");
            return;
        }

        const auto& lib_name = toks.back();
        toks.pop_back();

        if (auto k = lib_name.kind; k != Token::Kind::String) {
            error(lib_name.loc,
                  std::format("expected library to link as {}, but got {}",
                              human(Token::Kind::String),
                              human(k)));
            note(self.loc, "for this link directive");
            return;
        }

        linker_libs.push_back(lib_name.text);
    }

    void parse_token(std::vector<Op>& ops, const Token& t)
    {
        switch (t.kind) {
        case Token::Kind::Proc:
            if (!current_proc_name.empty()) {
                error(t.loc,
                      "defining procedures allowed only in global scope");
                toks.pop_back();
                return;
            }
            parse_proc(ops);
            break;
        case Token::Kind::Link:
            if (!current_proc_name.empty()) {
                error(t.loc, "`link` directive allowed only in global scope");
                toks.pop_back();
                return;
            }
            parse_link();
            break;
        case Token::Kind::Number: {
            toks.pop_back();
            if (current_proc_name.empty()) {
                error(t.loc,
                      "pushing numbers onto the stack only allowed inside of "
                      "procedure bodies");
                return;
            }

            errno = 0;
            const int64_t num = std::strtoll(t.text.c_str(), nullptr, 10);
            if (errno == ERANGE) {
                error(
                    t.loc,
                    std::format("number out of range (accepted range [{}, {}])",
                                std::numeric_limits<decltype(num)>::min(),
                                std::numeric_limits<decltype(num)>::max()));
                return;
            }
            ops.emplace_back(t, Op::Kind::Push_Int, num);
        } break;
        case Token::Kind::Ident:
            toks.pop_back();
            if (current_proc_name.empty()) {
                error(t.loc,
                      "calling procedures only allowed inside of "
                      "procedure bodies");
                return;
            }

            ops.emplace_back(t, Op::Kind::Proc_Call, t.text);
            break;
        case Token::Kind::Open_Curly:
        case Token::Kind::Close_Curly:
            error(t.loc, std::format("unexpected {}", human(t.kind)));
            return;
        case Token::Kind::String:
            TODO();
            break;
        }
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
        };
    }
};

// }}}

// {{{

enum class Target {
    x86_64_Gas,
};

namespace codegen {

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
        if (const auto* num = std::get_if<int64_t>(&op.as)) {
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

            out << "\t.globl " << name << '\n';
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
        case Op::Kind::Push_Int:
            out << "\tmovq $" << std::get<int64_t>(op.as) << ", %rax\n";
            out << "\tpushq %rax\n";
            break;
        default:
            std::unreachable();
        }
    }

    out << ".bss\n";
    out << "_depot_saved_rsp: .skip 8\n";
    out << "_depot_stack_bottom: .skip 524288\n";
    out << "_depot_stack_end:\n";

    return out.str();
}

}

#ifdef _WIN32

#error "windows platform is not supported"

#else // !defined(_WIN32)

#include <sys/wait.h>
#include <unistd.h>

#endif

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

    pid_t child_pid = fork();
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
#endif
}

static void
compile(Target tgt, std::filesystem::path input_path, const Da_Thing& ctx)
{
    using Fs_Path = std::filesystem::path;
    switch (tgt) {
    case Target::x86_64_Gas: {
        const auto out = codegen::compile(ctx);

        const auto& base_path = input_path.stem();
        const Fs_Path asm_path { base_path.string() + ".S" };
        const Fs_Path obj_path { base_path.string() + ".o" };
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
            "-dynamic-linker",
            "/lib64/ld-linux-x86-64.so.2",
            "/lib64/crt1.o",
            "/lib64/crti.o",
            "/lib64/crtn.o",
        };
        for (const auto& lib : ctx.linker_libs) {
            ld_cmdline.push_back("-l" + lib);
        }
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
    argv++;
    argc--;

    if (argc != 1) {
        usage(stderr, program_name);
        return 1;
    }

    const std::filesystem::path file_path { argv[0] };

    Lexer l { file_path };
    auto toks = l.lex();

    std::println("TOKS:");
    for (const auto& t : toks) {
        std::println("{}: {} {:?}", t.loc, human(t.kind), t.text);
    }

    Parser p { toks };
    auto o = p.parse();

    std::println("OPS:");
    for (const auto& op : o.ops) {
        std::print("{}: {}", op.tok.loc, human(op.kind));
        static_assert(std::variant_size_v<Op::As> == 2,
                      "Exhaustive handling of Op::As variants");
        if (const auto* num = std::get_if<int64_t>(&op.as)) {
            std::println(" {}", *num);
        } else if (const auto* str = std::get_if<std::string>(&op.as)) {
            std::println(" {:?}", *str);
        } else {
            std::println();
        }
    }

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
