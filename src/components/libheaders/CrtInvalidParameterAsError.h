#pragma once

#ifdef _WIN32
#include <cstdint>
#include <cstdlib>
#endif

namespace Haisos {

// While one of these is in scope on a thread, a C runtime function handed an
// invalid parameter fails the way its documentation says -- returns its error
// value (-1, 0, ...) and sets errno -- instead of ending the whole program.
//
// That is what the Microsoft C runtime does by default: _close(), _read() or
// _write() given a descriptor that is not open, strftime() given a conversion
// it does not know, and many more call the "invalid parameter handler", whose
// default is to terminate the process on the spot. POSIX functions just fail.
// Haisos hands such functions values that come from outside -- a descriptor
// any caller of IFileSystem may pass, a time format a process chose -- so on
// Windows every such call runs with this in scope, and a bad value costs the
// caller an error, never every process in the program.
//
// The handler is the calling thread's own (_set_thread_local_invalid_parameter_handler),
// so this changes nothing on any other thread, and the previous one is put
// back when this goes out of scope. Elsewhere it does nothing at all.
//
// (A debug build of the C runtime still reports each such call as a failed
// assertion before the handler runs; a release build only fails the call.)
class CrtInvalidParameterAsError {
public:
#ifdef _WIN32
    CrtInvalidParameterAsError()
        : m_previous(_set_thread_local_invalid_parameter_handler(&IgnoreInvalidParameter))
    {
    }
    ~CrtInvalidParameterAsError() { _set_thread_local_invalid_parameter_handler(m_previous); }
#else
    CrtInvalidParameterAsError() = default;
#endif

    CrtInvalidParameterAsError(const CrtInvalidParameterAsError&) = delete;
    CrtInvalidParameterAsError& operator=(const CrtInvalidParameterAsError&) = delete;

private:
#ifdef _WIN32
    // Returning is all it takes: the function that called it then fails with
    // its documented error value.
    static void __cdecl IgnoreInvalidParameter(
        const wchar_t* /*expression*/, const wchar_t* /*function*/, const wchar_t* /*file*/,
        unsigned int /*line*/, uintptr_t /*reserved*/)
    {
    }

    _invalid_parameter_handler m_previous;
#endif
};

} // namespace Haisos
