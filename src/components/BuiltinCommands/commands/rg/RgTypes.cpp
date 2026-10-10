#include "RgTypes.h"

namespace Haisos {

const std::vector<RgFileType>& RgFileTypes() {
    static const std::vector<RgFileType> types = {
        {"c", {"*.[chH]", "*.[chH].in", "*.cats"}},
        {"cmake", {"*.cmake", "CMakeLists.txt"}},
        {"config", {"*.cfg", "*.conf", "*.config", "*.ini"}},
        {"cpp", {"*.[ChH]", "*.[ChH].in", "*.[ch]pp", "*.[ch]pp.in", "*.[ch]xx",
                 "*.[ch]xx.in", "*.cc", "*.cc.in", "*.hh", "*.hh.in", "*.inl"}},
        {"cs", {"*.cs"}},
        {"css", {"*.css", "*.scss"}},
        {"csv", {"*.csv"}},
        {"docker", {"*Dockerfile*"}},
        {"go", {"*.go"}},
        {"h", {"*.h", "*.hh", "*.hpp"}},
        {"html", {"*.ejs", "*.htm", "*.html"}},
        {"java", {"*.java", "*.jsp", "*.jspx", "*.properties"}},
        {"js", {"*.cjs", "*.js", "*.jsx", "*.mjs", "*.vue"}},
        {"json", {"*.json", "*.sarif", "composer.lock"}},
        {"jsonl", {"*.jsonl"}},
        {"kotlin", {"*.kt", "*.kts"}},
        {"lua", {"*.lua"}},
        {"make", {"*.mak", "*.mk", "[Gg][Nn][Uu]makefile", "[Gg][Nn][Uu]makefile.am",
                  "[Gg][Nn][Uu]makefile.in", "[Mm]akefile", "[Mm]akefile.am",
                  "[Mm]akefile.in"}},
        {"markdown", {"*.markdown", "*.md", "*.mdown", "*.mdwn", "*.mdx", "*.mkd", "*.mkdn"}},
        {"md", {"*.markdown", "*.md", "*.mdown", "*.mdwn", "*.mdx", "*.mkd", "*.mkdn"}},
        {"php", {"*.php", "*.php3", "*.php4", "*.php5", "*.php7", "*.php8", "*.pht", "*.phtml"}},
        {"py", {"*.py", "*.pyi"}},
        {"python", {"*.py", "*.pyi"}},
        {"readme", {"*README", "README*"}},
        {"ruby", {"*.gemspec", "*.rb", "*.rbw", ".irbrc", "Gemfile", "Rakefile", "config.ru"}},
        {"rust", {"*.rs"}},
        {"sh", {"*.bash", "*.bashrc", "*.csh", "*.cshrc", "*.ksh", "*.kshrc", "*.sh",
                "*.tcsh", "*.zsh", ".bash_login", ".bash_logout", ".bash_profile", ".bashrc",
                ".cshrc", ".kshrc", ".login", ".logout", ".profile", ".tcshrc", ".zlogin",
                ".zlogout", ".zprofile", ".zshenv", ".zshrc", "bash_login", "bash_logout",
                "bash_profile", "bashrc", "profile", "zlogin", "zlogout", "zprofile",
                "zshenv", "zshrc"}},
        {"sql", {"*.psql", "*.sql"}},
        {"swift", {"*.swift"}},
        {"toml", {"*.toml", "Cargo.lock"}},
        {"ts", {"*.cts", "*.mts", "*.ts", "*.tsx"}},
        {"txt", {"*.txt"}},
        {"typescript", {"*.cts", "*.mts", "*.ts", "*.tsx"}},
        {"xml", {"*.dtd", "*.rng", "*.sch", "*.xhtml", "*.xjb", "*.xml", "*.xml.dist",
                 "*.xsd", "*.xsl", "*.xslt"}},
        {"yaml", {"*.yaml", "*.yml"}},
        {"zig", {"*.zig"}},
    };
    return types;
}

const std::vector<std::string_view>* FindRgTypeGlobs(std::string_view name) {
    for (const auto& type : RgFileTypes()) {
        if (type.name == name) {
            return &type.globs;
        }
    }
    return nullptr;
}

std::string RgTypeListText() {
    std::string out;
    for (const auto& type : RgFileTypes()) {
        out += type.name;
        out += ":";
        for (const auto& glob : type.globs) {
            out += " ";
            out.append(glob);
            out += ",";
        }
        if (!type.globs.empty() && out.back() == ',') {
            out.pop_back();
        }
        out += "\n";
    }
    return out;
}

} // namespace Haisos