#pragma once

namespace Haisos {

// While one of these is in scope on a thread, Windows fails a call that finds
// a drive not ready -- a removable or optical drive with no medium in it, say
// -- with an error, instead of first showing its "There is no disk in the
// drive" dialog and waiting for someone to click it away. Haisos mostly runs
// with no one to click it: a process listing /a, or a path leading through a
// drive whose medium was just taken out, would hang there.
//
// It sets the calling thread's own error mode (SetThreadErrorMode, adding
// SEM_FAILCRITICALERRORS and SEM_NOOPENFILEERRORBOX), so nothing changes on
// any other thread, and the previous mode is put back when this goes out of
// scope. Every call a filesystem here makes to the host runs with one in
// scope. Elsewhere it does nothing. (Defined with each platform's FileSystem.)
class NoCriticalErrorDialogs {
public:
    NoCriticalErrorDialogs();
    ~NoCriticalErrorDialogs();

    NoCriticalErrorDialogs(const NoCriticalErrorDialogs&) = delete;
    NoCriticalErrorDialogs& operator=(const NoCriticalErrorDialogs&) = delete;

private:
    unsigned long m_previousMode = 0;
};

} // namespace Haisos
