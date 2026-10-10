#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinFnmatch.h"
#include "commands/diff/DiffDirectories.h"
#include "commands/diff/DiffOutput.h"

namespace Haisos {

namespace {

int CompareDirectoryEntries(BuiltinContext& context, const DiffSettings& settings, const DiffTreeSettings& tree,
             const std::string& d0, const std::string& d1, bool topLevel);

// D + "/" + NAME, without doubling the slash ("rb" and "rb/" both give
// "rb/x").
std::string JoinPath(const std::string& dir, const std::string& name) {
    if (!dir.empty() && dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

// The last component of an operand's name: what a directory comparison
// reaches it by (diff FILE DIR compares DIR/BASENAME(FILE) with FILE).
std::string BaseName(std::string name) {
    while (name.size() > 1 && name.back() == '/') {
        name.pop_back();
    }
    const size_t slash = name.find_last_of('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

// The type word of GNU's mismatch line, "File X is a T while file Y is a T".
std::string TypeWord(const FileStatus& status) {
    switch (status.type) {
        case DirectoryEntryType::Dir: return "directory";
        case DirectoryEntryType::CharDevice: return "character special file";
        default: break;
    }
    return status.size == 0 ? "regular empty file" : "regular file";
}

// The names a directory holds, sorted by bytes, "." and ".." and every name
// an exclude pattern covers dropped. A directory that is not there (a -N
// fake) reads as empty: ReadDirectory gives nothing for it.
std::vector<std::string> EntryNames(BuiltinContext& context, const std::string& dir,
                                    const DiffTreeSettings& tree) {
    std::vector<std::string> names;
    for (const DirectoryEntry& entry : context.IO().ReadDirectory(dir)) {
        if (entry.name == "." || entry.name == "..") {
            continue;
        }
        bool excluded = false;
        for (const std::string& pattern : tree.excludes) {
            if (FnMatch(pattern, entry.name, 0)) {
                excluded = true;
                break;
            }
        }
        if (!excluded) {
            names.push_back(entry.name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

// The "diff OPTIONS A B" line before a file pair's output: the options as
// typed, then each side's label (in place of its path) or path, quoted as
// the ---/+++ header lines quote names.
std::string PairHeader(const DiffSettings& settings, const DiffTreeSettings& tree,
                       const std::string& p0, const std::string& p1) {
    const std::string shown0 = settings.label0 ? *settings.label0 : p0;
    const std::string shown1 = settings.label1 ? *settings.label1 : p1;
    return "diff" + tree.echoedOptions + " " + QuoteHeaderName(shown0) + " "
        + QuoteHeaderName(shown1);
}

// One entry pair of a directory comparison: both sides listed, or one of
// them faked by -N/--unidirectional-new-file (|missing0|/|missing1|, its
// type the other side's). |p0|/|p1| are the joined paths. Returns the
// pair's exit status, everything printed.
int DiffEntryPair(BuiltinContext& context, const DiffSettings& settings, const DiffTreeSettings& tree,
                 const std::string& p0, const std::string& p1, bool missing0, bool missing1) {
    FileStatus status0, status1;
    const bool have0 = !missing0 && context.IO().Stat(p0, status0) == 0;
    const bool have1 = !missing1 && context.IO().Stat(p1, status1) == 0;
    // A side that is not there takes the other side's type, so a faked pair
    // never mismatches; a side that vanished between listing and stat is
    // compared as a file, and the file comparison reports it.
    const char kind0 = have0 ? status0.type : have1 ? status1.type : DirectoryEntryType::File;
    const char kind1 = have1 ? status1.type : have0 ? status0.type : DirectoryEntryType::File;
    if (kind0 == DirectoryEntryType::Dir && kind1 == DirectoryEntryType::Dir) {
        if (!tree.recursive) {
            context.Out("Common subdirectories: " + p0 + " and " + p1 + "\n");
            return 0;
        }
        return CompareDirectoryEntries(context, settings, tree, p0, p1, false);
    }
    // Inside the walk only two regular files are compared by content: a file
    // against a directory, or a device on both sides (even of one type, as
    // GNU reports it), is reported by type and never read.
    if (kind0 != kind1 || (have0 && have1 && kind0 != DirectoryEntryType::File)) {
        // Both sides are real here: a faked one copies the other's type.
        const std::string shown0 = settings.label0 ? *settings.label0 : p0;
        const std::string shown1 = settings.label1 ? *settings.label1 : p1;
        context.Out("File " + shown0 + " is a " + TypeWord(status0) + " while file " + shown1
                    + " is a " + TypeWord(status1) + "\n");
        return 1;
    }
    return DiffTwoFiles(context, settings, p0, p1, PairHeader(settings, tree, p0, p1), missing0, missing1);
}

// Compares two directories entry by entry, in byte order of the names;
// |topLevel| applies -S. Returns the greatest status of the entries, 2 on a
// stop asked for.
int CompareDirectoryEntries(BuiltinContext& context, const DiffSettings& settings, const DiffTreeSettings& tree,
             const std::string& d0, const std::string& d1, bool topLevel) {
    std::vector<std::string> names0 = EntryNames(context, d0, tree);
    std::vector<std::string> names1 = EntryNames(context, d1, tree);
    // At the top level only, -S FILE starts the walk there: in either list,
    // the names that sort before FILE are skipped.
    if (topLevel && tree.startingFile) {
        const auto before = [&](const std::string& name) { return name < *tree.startingFile; };
        names0.erase(std::remove_if(names0.begin(), names0.end(), before), names0.end());
        names1.erase(std::remove_if(names1.begin(), names1.end(), before), names1.end());
    }
    int status = 0;
    size_t i0 = 0, i1 = 0;
    while (i0 < names0.size() || i1 < names1.size()) {
        if (context.StopRequested()) {
            return 2;
        }
        std::string name;
        bool in0, in1;
        if (i1 >= names1.size() || (i0 < names0.size() && names0[i0] < names1[i1])) {
            name = names0[i0++];
            in0 = true;
            in1 = false;
        } else if (i0 >= names0.size() || names1[i1] < names0[i0]) {
            name = names1[i1++];
            in0 = false;
            in1 = true;
        } else {
            name = names0[i0++];
            ++i1;
            in0 = in1 = true;
        }
        if (!in0 || !in1) {
            const bool faked = tree.newFile || (tree.unidirectionalNewFile && !in0);
            if (!faked) {
                context.Out("Only in " + std::string(in0 ? d0 : d1) + ": " + name + "\n");
                status = std::max(status, 1);
                continue;
            }
        }
        const std::string p0 = JoinPath(d0, name);
        const std::string p1 = JoinPath(d1, name);
        status = std::max(status, DiffEntryPair(context, settings, tree, p0, p1, !in0, !in1));
    }
    return status;
}

} // namespace

int DiffOperands(BuiltinContext& context, const DiffSettings& settings, const DiffTreeSettings& tree,
                 const std::string& name0, const std::string& name1) {
    // "-" is standard input: always there, never a directory.
    FileStatus status0, status1;
    const bool have0 = name0 != "-" && context.IO().Stat(name0, status0) == 0;
    const bool have1 = name1 != "-" && context.IO().Stat(name1, status1) == 0;
    // A missing side is faked (an empty file of the other side's type) only
    // with -N, or --unidirectional-new-file on the first side, and only when
    // the other one is there: two missing sides are both errors, -N or not.
    const bool missing0 = !have0 && name0 != "-"
        && (tree.newFile || tree.unidirectionalNewFile) && (have1 || name1 == "-");
    const bool missing1 = !have1 && name1 != "-" && tree.newFile && (have0 || name0 == "-");
    bool trouble = false;
    if (!have0 && name0 != "-" && !missing0) {
        context.Error(name0 + ": No such file or directory");
        trouble = true;
    }
    if (!have1 && name1 != "-" && !missing1) {
        context.Error(name1 + ": No such file or directory");
        trouble = true;
    }
    if (trouble) {
        return 2;
    }

    const bool isDir0 = have0 && status0.type == DirectoryEntryType::Dir;
    const bool isDir1 = have1 && status1.type == DirectoryEntryType::Dir;
    if ((name0 == "-" && isDir1) || (name1 == "-" && isDir0)) {
        context.Error("cannot compare '-' to a directory");
        return 2;
    }

    // A faked side has the other side's type, so a directory against a faked
    // missing one compares as two directories.
    if ((isDir0 || (missing0 && isDir1)) && (isDir1 || (missing1 && isDir0))) {
        return CompareDirectoryEntries(context, settings, tree, name0, name1, true);
    }
    if (isDir0 || isDir1) {
        // One directory: it is compared through the same-named entry under
        // it, DIR/BASENAME(FILE), which must be there even with -N.
        const bool dirIs0 = isDir0;
        const std::string joined = JoinPath(dirIs0 ? name0 : name1, BaseName(dirIs0 ? name1 : name0));
        FileStatus joinedStatus;
        if (context.IO().Stat(joined, joinedStatus) != 0) {
            context.Error(joined + ": No such file or directory");
            return 2;
        }
        if (joinedStatus.type == DirectoryEntryType::Dir) {
            // The other operand is not a directory, or the walk above took
            // the pair: this is a mismatch, in operand order.
            const FileStatus& other = dirIs0 ? status1 : status0;
            const std::string otherName = dirIs0 ? name1 : name0;
            const std::string left = dirIs0 ? joined : otherName;
            const std::string right = dirIs0 ? otherName : joined;
            const FileStatus& leftStatus = dirIs0 ? joinedStatus : other;
            const FileStatus& rightStatus = dirIs0 ? other : joinedStatus;
            const std::string& shown0 = settings.label0 ? *settings.label0 : left;
            const std::string& shown1 = settings.label1 ? *settings.label1 : right;
            context.Out("File " + shown0 + " is a " + TypeWord(leftStatus) + " while file " + shown1
                        + " is a " + TypeWord(rightStatus) + "\n");
            return 1;
        }
        return DiffTwoFiles(context, settings, dirIs0 ? joined : name0,
                            dirIs0 ? name1 : joined, "");
    }
    return DiffTwoFiles(context, settings, name0, name1, "", missing0, missing1);
}

} // namespace Haisos