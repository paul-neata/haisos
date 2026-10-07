#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "commands/hsh/HshAst.h"
#include "interfaces/IFileIO.h"

namespace Haisos::Hsh {

class Shell;

// Makes slot |fd| of |io| hold |descriptor|; a null one empties the slot.
// Uses AddDescriptor then Dup2 and CloseDescriptor of the temporary slot.
// False when the table is full.
bool PlaceDescriptor(IFileIO& io, int fd, std::shared_ptr<IFileDescriptor> descriptor);

// Applies a command's redirections to the shell's own descriptor table and
// undoes them when it ends, unless Keep() was called (exec without a command).
class RedirectionScope {
public:
    explicit RedirectionScope(Shell& shell);
    ~RedirectionScope();  // Restore()
    RedirectionScope(const RedirectionScope&) = delete;
    RedirectionScope& operator=(const RedirectionScope&) = delete;

    // Applies |redirections| in order. On the first failure, undoes what this
    // call applied and returns the message to report (without the
    // "hsh: <line>: " prefix): "cannot open /x: No such file". Throws
    // ShellError (through Shell::Fail) for "Syntax error: Bad fd number" and
    // for expansion errors, which are fatal in dash.
    std::optional<std::string> Apply(const std::vector<Redirection>& redirections);
    // Puts every slot this scope changed back as it was, latest change undone
    // first. Never writes anything and never throws.
    void Restore();
    // Forgets the saved slots: the redirections stay in effect.
    void Keep();

private:
    std::optional<std::string> ApplyOne(const Redirection& redirection);
    // Opens |target| created/truncating per |flags| into slot |fd|,
    // the noclobber check when |clobberCheck|.
    std::optional<std::string> OpenAndPlace(int fd, const std::string& target, int flags, bool clobberCheck);
    // A heredoc or here-string: a pipe sized exactly to |text| (so writing it
    // whole never blocks and no thread is needed), its read end in slot |fd|.
    std::optional<std::string> PipeInput(int fd, const std::string& text);
    // Records slot |fd|'s current content (possibly null), if not recorded
    // yet in the current Apply.
    void Save(int fd);
    // Undoes the slots saved at index |first| and later, latest first.
    void RestoreFrom(size_t first);

    Shell& m_shell;
    std::vector<std::pair<int, std::shared_ptr<IFileDescriptor>>> m_saved;
    size_t m_applyFirst = 0;  // m_saved's size when the current Apply started
};

} // namespace Haisos::Hsh
