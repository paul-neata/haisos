#include <regex>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinCommandList.h"

// man reaches the pages through CreateStandardBuiltinCommands(): a pure
// function returning fresh, stateless command objects compiled into Haisos --
// program data, like a static table, not something outside the process. man
// is handed no IHaisosOS, no IBuiltinCommands and no filesystem, and touches
// its context only for the standard streams; the ICurrentProcess rule holds.
// The consequence: man shows the page of every builtin compiled into Haisos,
// whether or not it is placed on the filesystem, and regardless of which
// IBuiltinCommands the OS was created with.

namespace Haisos {

namespace {

enum ManOption {
    kManWhatis = 1,
    kManApropos,
    kManIgnoreCase,
    kManMatchCase,
};

// man-db's is_section: one of the known section names, or two or more
// characters whose first is a digit and second is not ("1x", "3ssl"; "10" is
// not a section).
bool IsSection(const std::string& word) {
    static const char* kSections[] = {
        "1", "n", "l", "8", "3", "0", "2", "3type", "3posix", "3pm", "3perl", "3am", "5", "4", "9", "6", "7",
    };
    for (const char* section : kSections) {
        if (word == section) {
            return true;
        }
    }
    return word.size() >= 2 && isdigit(static_cast<unsigned char>(word[0]))
        && !isdigit(static_cast<unsigned char>(word[1]));
}

// ASCII case folding: names are plain ASCII, and so is the folding.
std::string FoldedAscii(const std::string& text) {
    std::string folded = text;
    for (auto& c : folded) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return folded;
}

bool NameEquals(const std::string& a, const std::string& b, bool matchCase) {
    return matchCase ? a == b : FoldedAscii(a) == FoldedAscii(b);
}

// man-db's "%-20s - %s": "<name> (1)" left-aligned in 20 columns (no padding
// when longer), then " - ", then the one-line summary.
std::string WhatisLine(const std::string& shown, const std::string& summary) {
    std::string line = shown + " (1)";
    if (line.size() < 20) {
        line.append(20 - line.size(), ' ');
    }
    return line + " - " + summary + "\n";
}

const IBuiltinCommand* FindPage(const std::vector<std::shared_ptr<IBuiltinCommand>>& pages,
                                const std::string& name, bool matchCase) {
    for (const auto& page : pages) {
        if (NameEquals(page->Name(), name, matchCase)) {
            return page.get();
        }
    }
    return nullptr;
}

class ManCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "man"; }
    std::string Version() const override { return "1.0.0"; }

    // Every option of man-db 2.12's man, aliases of one option in a row each;
    // what Haisos does not act on is still parsed and reported as not treated.
    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'C', "config-file", kBuiltinNotTreated, BuiltinArgument::Required, "FILE"},
            {'d', "debug", kBuiltinNotTreated},
            {'D', "default", kBuiltinNotTreated},
            {0, "warnings", kBuiltinNotTreated, BuiltinArgument::Optional, "WARNINGS"},
            {'f', "whatis", kManWhatis, BuiltinArgument::None, "", "one-line description of each PAGE"},
            {'k', "apropos", kManApropos, BuiltinArgument::None, "", "search names and descriptions"},
            {'K', "global-apropos", kBuiltinNotTreated},
            {'l', "local-file", kBuiltinNotTreated},
            {'w', "where", kBuiltinNotTreated},
            {0, "path", kBuiltinNotTreated},
            {0, "location", kBuiltinNotTreated},
            {'W', "where-cat", kBuiltinNotTreated},
            {0, "location-cat", kBuiltinNotTreated},
            {'c', "catman", kBuiltinNotTreated},
            {'R', "recode", kBuiltinNotTreated, BuiltinArgument::Required, "ENCODING"},
            {'L', "locale", kBuiltinNotTreated, BuiltinArgument::Required, "LOCALE"},
            {'m', "systems", kBuiltinNotTreated, BuiltinArgument::Required, "SYSTEM"},
            {'M', "manpath", kBuiltinNotTreated, BuiltinArgument::Required, "PATH"},
            {'S', "sections", kBuiltinNotTreated, BuiltinArgument::Required, "LIST"},
            {'s', "", kBuiltinNotTreated, BuiltinArgument::Required, "LIST"},
            {'e', "extension", kBuiltinNotTreated, BuiltinArgument::Required, "EXTENSION"},
            {'i', "ignore-case", kManIgnoreCase, BuiltinArgument::None, "", "case-insensitive names (default)"},
            {'I', "match-case", kManMatchCase, BuiltinArgument::None, "", "case-sensitive names"},
            {0, "regex", kBuiltinNotTreated},
            {0, "wildcard", kBuiltinNotTreated},
            {0, "names-only", kBuiltinNotTreated},
            {'a', "all", kBuiltinNotTreated},
            {'u', "update", kBuiltinNotTreated},
            {0, "no-subpages", kBuiltinNotTreated},
            {'P', "pager", kBuiltinNotTreated, BuiltinArgument::Required, "PAGER"},
            {'r', "prompt", kBuiltinNotTreated, BuiltinArgument::Required, "STRING"},
            {'7', "ascii", kBuiltinNotTreated},
            {'E', "encoding", kBuiltinNotTreated, BuiltinArgument::Required, "ENCODING"},
            {0, "no-hyphenation", kBuiltinNotTreated},
            {0, "nh", kBuiltinNotTreated},
            {0, "no-justification", kBuiltinNotTreated},
            {0, "nj", kBuiltinNotTreated},
            {'p', "preprocessor", kBuiltinNotTreated, BuiltinArgument::Required, "STRING"},
            {'t', "troff", kBuiltinNotTreated},
            {'T', "troff-device", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "DEVICE"},
            {'H', "html", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "BROWSER"},
            {'X', "gxditview", kBuiltinNotTreated, BuiltinArgument::OptionalAttached, "RESOLUTION"},
            {'Z', "ditroff", kBuiltinNotTreated},
            {'?', "", kBuiltinOptionHelp, BuiltinArgument::None, "", "show this help"},
            {0, "usage", kBuiltinNotTreated},
            {'V', "", kBuiltinOptionVersion, BuiltinArgument::None, "", "show the version"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "an interface to the system reference manuals",
            {"man [OPTION...] [SECTION] PAGE...", "man -k [OPTION...] REGEXP...", "man -f [OPTION...] PAGE..."},
            "Pages are compiled in: one per builtin, all in section 1, printed as plain text without a pager.\n"
            "-k and -f search the builtins' names and one-line summaries.\n"};
    }

    int Run(BuiltinContext& context) override {
        // man-db's usage error ends in its own Try line, one BeginBuiltin's
        // (which lacks --usage) cannot print -- so the parse error is handled
        // here, and BeginBuiltin re-parses (no error now) for --help, -?,
        // --version, -V and the not-treated reports.
        ParsedBuiltinArgs first = ParseBuiltinArgs(context.Args(), Options());
        if (!first.error.empty()) {
            context.Error(first.error);
            context.ErrorText("Try 'man --help' or 'man --usage' for more information.\n");
            return 1;
        }
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        int mode = 0; // 0 pages, kManWhatis, kManApropos: the last given wins
        bool matchCase = false;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kManWhatis:
                case kManApropos:
                    mode = option.id;
                    break;
                case kManIgnoreCase:
                    matchCase = false;
                    break;
                case kManMatchCase:
                    matchCase = true;
                    break;
                default:
                    break;
            }
        }

        const auto pages = CreateStandardBuiltinCommands();
        if (mode == kManWhatis) {
            return RunWhatis(context, pages, parsed->operands, matchCase);
        }
        if (mode == kManApropos) {
            return RunApropos(context, pages, parsed->operands);
        }
        return RunPages(context, pages, parsed->operands, matchCase);
    }

private:
    static int RunPages(BuiltinContext& context, const std::vector<std::shared_ptr<IBuiltinCommand>>& pages,
                        const std::vector<std::string>& operands, bool matchCase) {
        if (operands.empty()) {
            context.ErrorText("What manual page do you want?\nFor example, try 'man man'.\n");
            return 1;
        }
        std::string section;
        size_t first = 0;
        if (IsSection(operands[0])) {
            if (operands.size() == 1) {
                context.ErrorText("No manual entry for " + operands[0] +
                    "\n(Alternatively, what manual page do you want from section " + operands[0] + "?)"
                    "\nFor example, try 'man man'.\n");
                return 1;
            }
            section = operands[0];
            first = 1;
        }
        int status = 0;
        for (size_t i = first; i < operands.size(); ++i) {
            // Every page is in section 1, and only the exact section 1 finds
            // it (man 1x ls finds nothing, as man-db).
            const IBuiltinCommand* found =
                (section.empty() || section == "1") ? FindPage(pages, operands[i], matchCase) : nullptr;
            if (found) {
                context.Out(found->ManPage());
            } else {
                context.ErrorText("No manual entry for " + operands[i] +
                    (section.empty() ? "" : " in section " + section) + "\n");
                status = 16;
            }
        }
        return status;
    }

    static int RunWhatis(BuiltinContext& context, const std::vector<std::shared_ptr<IBuiltinCommand>>& pages,
                         const std::vector<std::string>& operands, bool matchCase) {
        if (operands.empty()) {
            context.Out("whatis what?\n");
            return 1;
        }
        bool any = false;
        for (const auto& keyword : operands) {
            const IBuiltinCommand* found = FindPage(pages, keyword, matchCase);
            if (found) {
                // The name shown is the keyword as typed ("man -f LS" prints
                // "LS (1)", as man-db does).
                context.Out(WhatisLine(keyword, found->Help().summary));
                any = true;
            } else {
                context.ErrorText(keyword + ": nothing appropriate.\n");
            }
        }
        return any ? 0 : 16;
    }

    static int RunApropos(BuiltinContext& context, const std::vector<std::shared_ptr<IBuiltinCommand>>& pages,
                          const std::vector<std::string>& operands) {
        if (operands.empty()) {
            context.Out("apropos what?\n");
            return 1;
        }
        // Every keyword is a POSIX extended regular expression, always
        // case-insensitive; one that does not compile is fatal before
        // anything else is printed.
        std::vector<std::regex> regexes;
        for (const auto& keyword : operands) {
            try {
                regexes.emplace_back(keyword, std::regex::extended | std::regex::icase);
            } catch (const std::regex_error&) {
                context.ErrorText("apropos: fatal: regex `" + keyword + "': Invalid regular expression\n");
                return 2;
            }
        }
        std::vector<bool> matched(operands.size(), false);
        std::string out;
        for (const auto& page : pages) {
            const std::string summary = page->Help().summary;
            bool pageMatched = false;
            for (size_t i = 0; i < regexes.size(); ++i) {
                if (std::regex_search(page->Name(), regexes[i]) || std::regex_search(summary, regexes[i])) {
                    matched[i] = true;
                    pageMatched = true;
                }
            }
            if (pageMatched) {
                out += WhatisLine(page->Name(), summary);
            }
        }
        context.Out(out);
        bool any = false;
        for (size_t i = 0; i < operands.size(); ++i) {
            if (matched[i]) {
                any = true;
            } else {
                context.ErrorText(operands[i] + ": nothing appropriate.\n");
            }
        }
        return any ? 0 : 16;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateManCommand() {
    return std::make_shared<ManCommand>();
}

} // namespace Haisos
