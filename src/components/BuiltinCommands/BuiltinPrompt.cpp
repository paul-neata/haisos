#include "BuiltinPrompt.h"

namespace Haisos {

BuiltinPrompt::BuiltinPrompt(BuiltinContext& context)
    : m_context(context)
{
}

bool BuiltinPrompt::Ask(const std::string& question) {
    // ErrorText flushes stdout first, so the prompt shows before anything the
    // answer's path writes next, in either stream's order.
    m_context.ErrorText(question);
    if (!m_reader) {
        m_reader.emplace(m_context.IO().GetDescriptor(IFileIO::kStdIn));
    }
    const auto line = m_reader->ReadLine();
    // End of input, a failed read, a stop while waiting: all no. GNU leaves
    // the prompt as the last thing on the line and carries on.
    if (!line || line->empty()) {
        return false;
    }
    return (*line)[0] == 'y' || (*line)[0] == 'Y';
}

} // namespace Haisos