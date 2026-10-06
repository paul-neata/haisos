#include "LuaProcess.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>
#include "src/components/libheaders/DestroyOffRuntimeThreads.h"
#include "src/components/libheaders/ExitCodes.h"
#include "src/components/Logger/Logger.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace Haisos {

namespace {

// How deeply tables may nest inside one another on their way across the bridge,
// in either direction: a tool's arguments (a Lua table turned into JSON) and a
// tool's result (JSON turned back into Lua tables). Both conversions recurse
// once per level and push onto the Lua stack at each one, so without a bound a
// script -- untrusted input, which an agent can write and then start -- or a
// file it merely reads could overflow the Lua stack or the C++ one, and take
// the whole program down with it. Real arguments and results nest a handful of
// levels; this is a backstop, not a budget.
constexpr int kMaxJsonNestingDepth = 100;

// Why a script's table could not be turned into tool arguments. Thrown by
// ToJson and caught by LuaToolTrampoline, which calls ToJson directly: no Lua
// frame lies between the throw and the catch, so the exception never has to
// cross Lua's C code, which unwinds with longjmp and knows nothing of C++.
class LuaArgumentsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Pushes |value| onto the Lua stack as the matching Lua value, objects and
// arrays as tables. |depth| is the nesting level of |value| if it is an object
// or an array (the result itself is at 1). Returns false when a table would
// nest deeper than kMaxJsonNestingDepth, or the Lua stack cannot grow, leaving
// whatever it had built on the stack for the caller to drop.
bool PushJson(lua_State* L, const nlohmann::json& value, int depth) {
    // A C function is only guaranteed LUA_MINSTACK free slots, and every level
    // of a table holds up to three at once: the table, a key and its value.
    // lua_checkstack only reports; luaL_checkstack would raise a Lua error,
    // i.e. longjmp out across the C++ frames below.
    if (!lua_checkstack(L, 3)) {
        return false;
    }
    switch (value.type()) {
        case nlohmann::json::value_t::null:
            lua_pushnil(L);
            return true;
        case nlohmann::json::value_t::boolean:
            lua_pushboolean(L, value.get<bool>());
            return true;
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned:
            lua_pushinteger(L, static_cast<lua_Integer>(value.get<int64_t>()));
            return true;
        case nlohmann::json::value_t::number_float:
            lua_pushnumber(L, value.get<double>());
            return true;
        case nlohmann::json::value_t::string: {
            // With its length: a string may hold NUL bytes, where a C string ends.
            const auto& text = value.get_ref<const std::string&>();
            lua_pushlstring(L, text.data(), text.size());
            return true;
        }
        case nlohmann::json::value_t::array: {
            if (depth > kMaxJsonNestingDepth) {
                return false;
            }
            lua_newtable(L);
            lua_Integer i = 1;
            for (const auto& item : value) {
                if (!PushJson(L, item, depth + 1)) {
                    return false;
                }
                lua_rawseti(L, -2, i++);
            }
            return true;
        }
        case nlohmann::json::value_t::object: {
            if (depth > kMaxJsonNestingDepth) {
                return false;
            }
            lua_newtable(L);
            for (auto it = value.begin(); it != value.end(); ++it) {
                // The key with its length too, so it is pushed rather than
                // handed to lua_setfield as a C string.
                const std::string& key = it.key();
                lua_pushlstring(L, key.data(), key.size());
                if (!PushJson(L, it.value(), depth + 1)) {
                    return false;
                }
                lua_rawset(L, -3);
            }
            return true;
        }
        default:
            lua_pushnil(L);
            return true;
    }
}

// Turns the Lua value at |index| into JSON, for a tool's arguments. A table
// whose keys are exactly 1..n becomes an array; any other table an object of
// its string-keyed entries (keys of other types have no JSON counterpart and
// are dropped). |depth| is the nesting level of the value if it is a table
// (the arguments table itself is at 1), and |ancestors| the tables enclosing
// it, by identity. Throws LuaArgumentsError instead of converting a table that
// nests too deeply or contains itself: either would recurse without end.
nlohmann::json ToJson(lua_State* L, int index, int depth, std::vector<const void*>& ancestors) {
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
        case LUA_TNIL:
            return nullptr;
        case LUA_TBOOLEAN:
            return static_cast<bool>(lua_toboolean(L, index));
        case LUA_TNUMBER:
            if (lua_isinteger(L, index)) {
                return static_cast<int64_t>(lua_tointeger(L, index));
            }
            return lua_tonumber(L, index);
        case LUA_TSTRING: {
            // With its length: a string may hold NUL bytes, where a C string ends.
            size_t length = 0;
            const char* text = lua_tolstring(L, index, &length);
            return std::string(text, length);
        }
        case LUA_TTABLE: {
            if (depth > kMaxJsonNestingDepth) {
                throw LuaArgumentsError("arguments nested too deeply (more than " +
                    std::to_string(kMaxJsonNestingDepth) + " levels of tables)");
            }
            // Only the tables on the way down from the arguments count: one
            // reached twice through different branches is not a cycle.
            const void* table = lua_topointer(L, index);
            if (std::find(ancestors.begin(), ancestors.end(), table) != ancestors.end()) {
                throw LuaArgumentsError("arguments contain a cycle (a table that contains itself)");
            }
            // lua_next keeps a key and its value on the stack for each entry,
            // and a C function is only guaranteed LUA_MINSTACK free slots in
            // all. lua_checkstack only reports; luaL_checkstack would raise a
            // Lua error, i.e. longjmp out across these C++ frames.
            if (!lua_checkstack(L, 3)) {
                throw LuaArgumentsError("arguments nested too deeply for the Lua stack");
            }
            ancestors.push_back(table);

            std::vector<std::pair<lua_Integer, nlohmann::json>> intEntries;
            nlohmann::json obj = nlohmann::json::object();
            bool hasOtherKeys = false;

            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                const int keyType = lua_type(L, -2);
                if (keyType == LUA_TNUMBER && lua_isinteger(L, -2)) {
                    const lua_Integer key = lua_tointeger(L, -2);
                    intEntries.emplace_back(key, ToJson(L, -1, depth + 1, ancestors));
                } else {
                    hasOtherKeys = true;
                    if (keyType == LUA_TSTRING) {
                        // A string key is read in place, not converted, so
                        // lua_next can carry on from it.
                        size_t keyLength = 0;
                        const char* key = lua_tolstring(L, -2, &keyLength);
                        obj[std::string(key, keyLength)] = ToJson(L, -1, depth + 1, ancestors);
                    }
                }
                lua_pop(L, 1);
            }
            ancestors.pop_back();

            bool isSequence = !hasOtherKeys && !intEntries.empty();
            if (isSequence) {
                std::sort(intEntries.begin(), intEntries.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });
                for (size_t i = 0; i < intEntries.size(); ++i) {
                    if (intEntries[i].first != static_cast<lua_Integer>(i + 1)) {
                        isSequence = false;
                        break;
                    }
                }
            }
            if (isSequence) {
                nlohmann::json arr = nlohmann::json::array();
                for (auto& entry : intEntries) {
                    arr.push_back(std::move(entry.second));
                }
                return arr;
            }
            for (auto& entry : intEntries) {
                obj[std::to_string(entry.first)] = std::move(entry.second);
            }
            return obj;
        }
        default:
            return nullptr;
    }
}

LuaProcess* SelfFromState(lua_State* L) {
    return *static_cast<LuaProcess**>(lua_getextraspace(L));
}

// Defined below, next to the kill hook they arm.
int LuaResumeTrampoline(lua_State* L);
int LuaWrapTrampoline(lua_State* L);

// Opens only the Lua libraries that are safe for a sandboxed .lua process:
// base, table, string, math, utf8, and coroutine. Deliberately omits `io`,
// `os`, `package`, and `debug`, which would grant raw filesystem/process/env
// access and native library loading, bypassing the rooted IFileSystem and the
// OS's tool-only sandboxing (the stock `os.exit` stays out with them: it calls
// C exit() and would end the whole haisos host -- a script ends itself with the
// exit() global registered in RegisterBindings instead). Four base-library
// globals are removed after opening:
//  - `dofile`/`loadfile` read directly from the real disk, escaping the jail;
//  - `load` defaults to mode "bt", i.e. it accepts *binary* chunks, and Lua's
//    bytecode loader does not validate untrusted input: a crafted binary chunk
//    yields arbitrary memory read/write and native code execution, defeating
//    the whole sandbox. Nil'ing `load` is simpler and strictly safer than
//    wrapping it to force mode "t", and no .lua process needs to compile source
//    at runtime;
//  - `warn` writes straight to the host process's stderr, bypassing the
//    process's own stderr (its descriptor 2), which is where a script's
//    diagnostics belong.
void OpenSafeLuaLibs(lua_State* L) {
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_settop(L, 0);

    // coroutine.resume and coroutine.wrap are replaced with wrappers that arm
    // the latched kill hook on the resuming thread when a kill or an exit()
    // latched while the coroutine ran (see LuaResumeTrampoline). The originals
    // are kept as each wrapper's upvalue, so coroutines behave exactly as
    // before when nothing latched.
    lua_getglobal(L, LUA_COLIBNAME);
    lua_getfield(L, -1, "resume");
    lua_pushcclosure(L, &LuaResumeTrampoline, 1);
    lua_setfield(L, -2, "resume");
    lua_getfield(L, -1, "wrap");
    lua_pushcclosure(L, &LuaWrapTrampoline, 1);
    lua_setfield(L, -2, "wrap");
    lua_pop(L, 1);

    static const char* const kRemovedGlobals[] = {"dofile", "loadfile", "load", "warn"};
    for (const char* name : kRemovedGlobals) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
}

// Arms the latched kill hook on L and on the script's main thread. A hook set
// is per lua_State (thread), and a kill or an exit() raised inside a coroutine
// must also stop the thread it was ultimately resumed from: resume catches the
// error and hands control back, so a hook armed on the coroutine alone would
// never fire again. The main thread is found through the registry rather than
// a C++ member, so the trampolines this runs inside never touch one (an
// intermediate thread between a nested coroutine and the main one is armed by
// the coroutine.resume/coroutine.wrap wrappers below, as its resume returns).
// Only atomics and Lua API calls: no C++ allocation, so nothing here can throw
// across Lua's frames.
void ArmLatchedKillHook(lua_State* L);

// Latched kill hook. A plain error raised from a count hook is an ordinary
// catchable Lua error, so `while true do pcall(f) end` would swallow the kill
// and keep running forever. Once a kill has been requested the hook therefore
// re-arms itself to fire on *every* instruction, call, return and line (the
// same masks the stock Lua interpreter uses for Ctrl-C) before raising: the
// script then cannot execute a single further instruction without erroring
// again, so each caught error strips one `pcall` level - and no new level can
// be entered, since the hook fires before the call instruction runs - until the
// error reaches the top-level lua_pcall and the script terminates.
// lua_sethook() re-arms the trap flag on every Lua frame on the stack, so the
// hook survives the error unwinding back into an outer frame. The hook is armed
// on the main thread as well as on L (see ArmLatchedKillHook), so it latches
// across coroutines too; a thread that resumed a coroutine is armed by the
// resume/wrap wrappers the moment control comes back to it.
void KillHookTrampoline(lua_State* L, lua_Debug* /*ar*/) {
    LuaProcess* self = SelfFromState(L);
    // The exit flag latches exactly like the kill one: exit() aborts through
    // this same hook so that a pcall wrapping it is stripped just the same.
    if (self->IsKillRequested() || self->IsExitRequested()) {
        ArmLatchedKillHook(L);
        luaL_error(L, "process killed");
    }
}

void ArmLatchedKillHook(lua_State* L) {
    lua_sethook(L, &KillHookTrampoline,
                LUA_MASKCOUNT | LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    lua_State* mainThread = lua_tothread(L, -1);
    lua_pop(L, 1);
    if (mainThread != nullptr && mainThread != L) {
        lua_sethook(mainThread, &KillHookTrampoline,
                    LUA_MASKCOUNT | LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE, 1);
    }
}

// coroutine.resume, wrapped. Running a coroutine hands control to its thread;
// when a kill or an exit() latched while it ran, its error came back caught
// (that is what resume does) and the hook was armed on the coroutine and the
// main thread only -- the resuming thread itself, when it is neither (a
// nested resume), would run on for up to a count period, tool calls included.
// This wrapper arms the calling thread as control comes back, so the latched
// hook raises on its very next instruction and the `false, "exit"` resume
// returned is never acted on. It holds no C++ object across lua_call (see the
// comment on LuaToolTrampoline).
int LuaResumeTrampoline(lua_State* L) {
    // The original coroutine.resume is the upvalue; the arguments follow it.
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    LuaProcess* self = SelfFromState(L);
    if (self->IsKillRequested() || self->IsExitRequested()) {
        ArmLatchedKillHook(L);
    }
    return lua_gettop(L);
}

// One call of a function coroutine.wrap returned, wrapped. The wrapped call
// raises the coroutine's error in the caller, so it runs protected here, the
// caller is armed when the latch is set, and the error is re-raised unchanged:
// the hook still strips each pcall level, as it does for resume. lua_error is
// allowed here because this closure holds no C++ locals (see the comment on
// LuaToolTrampoline).
int LuaWrapCallTrampoline(lua_State* L) {
    // The function the original coroutine.wrap returned is the upvalue.
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    const int status = lua_pcall(L, lua_gettop(L) - 1, LUA_MULTRET, 0);
    LuaProcess* self = SelfFromState(L);
    if (self->IsKillRequested() || self->IsExitRequested()) {
        ArmLatchedKillHook(L);
    }
    if (status != LUA_OK) {
        return lua_error(L);
    }
    return lua_gettop(L);
}

// coroutine.wrap, wrapped: as the original, but the function it hands back is
// wrapped in LuaWrapCallTrampoline above.
int LuaWrapTrampoline(lua_State* L) {
    // The original coroutine.wrap is the upvalue; wrap(f) takes one argument
    // and returns one function, which becomes the call wrapper's upvalue.
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, 1, 1);
    lua_pushcclosure(L, &LuaWrapCallTrampoline, 1);
    return 1;
}

// The message handler of the script's top-level lua_pcall: the standalone
// interpreter's msghandler (lua.c), minus the traceback it appends. A string
// (or number, which lua_tostring converts in place) is the message as it is;
// otherwise its __tostring result when that produces a string; otherwise
// "(error object is a <type> value)". It runs inside the protected call, so a
// __tostring that raises (or is interrupted by a kill) is just another error
// of the script. Run after lua_pcall had returned, the same metamethod raising
// would be an unprotected error -- and Lua aborts the whole host on one.
int LuaErrorMessageHandler(lua_State* L) {
    if (lua_tostring(L, 1) == nullptr) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) {
            return 1;
        }
        lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    return 1;
}

// The error value on top of the stack as text. It is a string whenever the
// message handler above produced it, and for every load error; nothing here
// runs Lua code, so nothing here can raise.
std::string RenderLuaError(lua_State* L) {
    if (const char* msg = lua_tostring(L, -1)) {
        return msg;
    }
    return std::string("(error object is a ") + luaL_typename(L, -1) + " value)";
}

} // namespace

std::shared_ptr<LuaProcess> LuaProcess::Create(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& path,
    const std::string& workingDirectory,
    std::weak_ptr<IHaisosOS> os,
    std::shared_ptr<CurrentProcessHandle> selfHandle,
    std::string scriptContent,
    std::vector<std::string> args,
    std::shared_ptr<IToolFactory> toolFactory,
    const StartProcessOptions& options)
{
    // The script's own thread can hold the last reference to it -- inside an
    // os_* tool call -- and the destructor waits for that thread.
    auto process = std::shared_ptr<LuaProcess>(
        new LuaProcess(
            pid,
            parentPid,
            std::move(environment),
            path,
            workingDirectory,
            std::move(os),
            std::move(scriptContent),
            std::move(args),
            std::move(toolFactory)),
        DestroyOffRuntimeThreads<LuaProcess>("LuaProcess '" + path + "' pid=" + std::to_string(pid)));
    // Slots 0, 1 and 2 before the thread starts: print() and the error path
    // below write through them from the script's first instruction. False means
    // a null stream was passed in -- a caller that skipped
    // HaisosOS::ResolveStandardStreams.
    if (!process->m_io->InstallStandardStreams(options.stdIn, options.stdOut, options.stdErr)) {
        LogError("LuaProcess: refusing to create a process for '%s': its standard streams could not be installed",
            path.c_str());
        return nullptr;
    }
    // The script's tools reach the process through this handle. It is filled in
    // before Start(), so the script's thread can never observe it empty.
    if (selfHandle) {
        selfHandle->Set(process);
    }
    process->Start();
    return process;
}

LuaProcess::LuaProcess(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& path,
    const std::string& workingDirectory,
    std::weak_ptr<IHaisosOS> os,
    std::string scriptContent,
    std::vector<std::string> args,
    std::shared_ptr<IToolFactory> toolFactory)
    : m_pid(pid)
    , m_parentPid(parentPid)
    , m_environment(std::move(environment))
    , m_path(path)
    , m_os(os)
    , m_io(ProcessFileIO::Create(std::move(os), workingDirectory))
    , m_scriptContent(std::move(scriptContent))
    , m_args(std::move(args))
    , m_toolFactory(std::move(toolFactory))
{
}

void LuaProcess::Start() {
    // Under the join lock, as every use of m_thread is: IsOwnThread() may be
    // asked from the script's thread itself.
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    m_thread = std::thread(&LuaProcess::RunThread, this);
}

LuaProcess::~LuaProcess() {
    LogDebug("LuaProcess '%s' pid=%llu: destroying", m_path.c_str(), static_cast<unsigned long long>(m_pid));
    Kill();
    if (!WaitToFinish(5000)) {
        LogWarning("LuaProcess '%s' thread did not finish within 5s during destruction, waiting indefinitely", m_path.c_str());
    }
    WaitToFinish();
}

uint64_t LuaProcess::GetPid() const {
    return m_pid;
}

uint64_t LuaProcess::GetParentPid() const {
    return m_parentPid;
}

std::string LuaProcess::Path() const {
    return m_path;
}

std::string LuaProcess::StartingAgentName() const {
    // A Lua script is not an agent.
    return std::string();
}

std::shared_ptr<IEnvironment> LuaProcess::GetEnvironment() const {
    // A clone: reading a process's environment from outside must never be a way
    // to change what the process itself sees.
    return m_environment ? m_environment->Clone() : nullptr;
}

std::shared_ptr<IFileIO> LuaProcess::IO() const {
    return m_io;
}

void LuaProcess::TriggerStop() {
    // A Lua script runs to completion; there is no command queue to close, so a
    // request to stop is the same thing as a request to abort it.
    Kill();
}

bool LuaProcess::IsFinished() const {
    return m_finished.load();
}

void LuaProcess::WaitToFinish() {
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool LuaProcess::WaitToFinish(uint64_t timeoutMs) {
    // A wait for the script, made on the script's own thread, can only time
    // out: that thread is the one that would have to finish. It is how
    // ~HaisosOS once lost 5 s on the very process it was running on, so it is
    // reported loudly (the wait itself goes ahead, as asked).
    if (timeoutMs > 0 && IsOwnThread()) {
        LogError("LuaProcess '%s' pid=%llu: waiting %llums for itself on its own thread, which cannot finish while it waits",
            m_path.c_str(), static_cast<unsigned long long>(m_pid), static_cast<unsigned long long>(timeoutMs));
    }
    std::unique_lock<std::mutex> lock(m_finishedMutex);
    bool finished = m_finishedCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return m_finished.load(); });
    lock.unlock();
    if (finished) {
        std::lock_guard<std::mutex> joinLock(m_joinMutex);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
    return finished;
}

void LuaProcess::Kill() {
    m_killed = true;
    // Wake a pipe Read/Write the script is blocked in, so a stuck pipeline
    // cannot outlast the kill hook.
    m_stopToken->RequestStop();
}

bool LuaProcess::IsOwnThread() {
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    return m_thread.get_id() == std::this_thread::get_id();
}

std::shared_ptr<IAgent> LuaProcess::AsAgent() {
    return nullptr;
}

std::shared_ptr<IHaisosOS> LuaProcess::OS() const {
    // The one door out of this process: everything the script reaches beyond
    // its own memory comes from here (see ICurrentProcess).
    return m_os.lock();
}

bool LuaProcess::IsKillRequested() const {
    return m_killed.load();
}

bool LuaProcess::IsExitRequested() const {
    return m_exitRequested.load();
}

std::optional<int> LuaProcess::ExitCode() const {
    std::lock_guard<std::mutex> lock(m_finishedMutex);
    return m_exitCode;
}

void LuaProcess::StopForBrokenPipe() {
    m_brokenPipe = true;
    // Stopped the way a kill stops it -- a script has no gentler request.
    Kill();
}

void LuaProcess::WriteToDescriptor(int fd, const std::string& bytes) {
    if (m_brokenPipe) {
        // The process is already dying quietly for an earlier broken pipe:
        // nothing more is written.
        return;
    }
    auto descriptor = m_io->GetDescriptor(fd);
    if (!descriptor) {
        // The slot is empty (or was never filled): the bytes are dropped, not
        // redirected anywhere else.
        return;
    }
    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = descriptor->Write(bytes.data() + written, bytes.size() - written);
        if (n == kIOBrokenPipe) {
            // The pipe's reader is gone: the script stops quietly, exit code
            // 141, as a standalone lua would from SIGPIPE -- on stderr's pipe
            // (the error line below) as well as stdout's (print).
            StopForBrokenPipe();
            return;
        }
        if (n < 0) {
            return;
        }
        written += static_cast<size_t>(n);
    }
}

int LuaProcess::LuaToolTrampoline(lua_State* L) {
    // Lua is built as C and unwinds with longjmp, so a C++ exception escaping
    // into its frames would reach std::terminate rather than any handler. Tool
    // code can genuinely throw -- nlohmann's dump() raises on invalid UTF-8,
    // which a directory listing can easily contain -- so everything is funnelled
    // into the (message, is_error) shape scripts already handle. lua_pushfstring
    // is used in the handlers because it allocates through Lua, not the C++ heap.
    try {
        LuaProcess* self = SelfFromState(L);
        const char* toolName = lua_tostring(L, lua_upvalueindex(1));

        nlohmann::json args = nlohmann::json::object();
        if (lua_gettop(L) >= 1 && lua_istable(L, 1)) {
            try {
                std::vector<const void*> ancestors;
                args = ToJson(L, 1, /*depth=*/1, ancestors);
            } catch (const LuaArgumentsError& e) {
                // The script's mistake, not a failure of the tool, which is
                // never called: the script gets the usual error pair and goes on.
                LogWarning("LuaProcess '%s': not calling tool '%s': its %s",
                    self->m_path.c_str(), toolName ? toolName : "", e.what());
                // Whatever the abandoned conversion left on the stack goes first.
                lua_settop(L, 0);
                lua_pushfstring(L, "%s: %s", toolName ? toolName : "tool", e.what());
                lua_pushboolean(L, true);
                return 2;
            }
        }

        auto tool = self->m_toolFactory
            ? self->m_toolFactory->CreateTool(toolName ? toolName : "", nullptr)
            : nullptr;
        if (!tool) {
            LogWarning("LuaProcess '%s': unknown tool '%s'", self->m_path.c_str(), toolName ? toolName : "");
            lua_pushstring(L, "unknown tool");
            lua_pushboolean(L, true);
            return 2;
        }

        LogDebug("LuaProcess '%s': calling tool '%s'", self->m_path.c_str(), toolName ? toolName : "");
        ToolResult result = tool->Call(nullptr, args);
        LogDebug("LuaProcess '%s': tool '%s' returned is_error=%d (%zu bytes)",
            self->m_path.c_str(), toolName ? toolName : "", result.isError ? 1 : 0, result.content.size());

        // Tool results that happen to be JSON (e.g. os_list_directory) are handed
        // back as a Lua table rather than a raw string, so scripts don't need
        // their own JSON parser for the common case.
        auto parsed = nlohmann::json::parse(result.content, nullptr, false);
        bool pushedAsTable = false;
        if (!result.isError && !parsed.is_discarded() && (parsed.is_object() || parsed.is_array())) {
            const int top = lua_gettop(L);
            pushedAsTable = PushJson(L, parsed, /*depth=*/1);
            if (!pushedAsTable) {
                // Nested deeper than the bridge converts: dropped half-built,
                // and handed back as the text it is, as a result that is not
                // JSON is.
                lua_settop(L, top);
                LogDebug("LuaProcess '%s': tool '%s' returned JSON nested more than %d levels deep; handing it back as text",
                    self->m_path.c_str(), toolName ? toolName : "", kMaxJsonNestingDepth);
            }
        }
        if (!pushedAsTable) {
            // With its length: a result (a file's contents, say) may hold NUL bytes.
            lua_pushlstring(L, result.content.data(), result.content.size());
        }
        lua_pushboolean(L, result.isError);
        return 2;
    } catch (const std::exception& e) {
        lua_pushfstring(L, "tool call failed: %s", e.what());
        lua_pushboolean(L, true);
        return 2;
    } catch (...) {
        lua_pushstring(L, "tool call failed: unknown error");
        lua_pushboolean(L, true);
        return 2;
    }
}

int LuaProcess::LuaPrintTrampoline(lua_State* L) {
    // See LuaToolTrampoline: a C++ exception must not unwind into Lua's C frames.
    // Building the line and writing it both allocate, so a failure here drops
    // the output rather than taking the process down.
    try {
        LuaProcess* self = SelfFromState(L);
        int n = lua_gettop(L);
        std::string line;
        for (int i = 1; i <= n; ++i) {
            if (i > 1) {
                line += "\t";
            }
            size_t len = 0;
            const char* s = luaL_tolstring(L, i, &len);
            line.append(s, len);
            lua_pop(L, 1);
        }
        // As print() on a terminal: the line and one newline, on stdout.
        self->WriteToDescriptor(IFileIO::kStdOut, line + "\n");
        if (self->m_brokenPipe) {
            // The write hit a pipe with no reader: the script stops as it
            // would from SIGPIPE. Never raise (luaL_error) from here -- this
            // trampoline has C++ locals, and a longjmp over them is undefined
            // -- so the kill hook is re-armed to fire on the next instruction
            // exactly as it re-arms itself (on the main thread too, so this
            // latches from inside a coroutine as well), and the unwinding runs
            // as for a kill: RunThread then sees a kill and writes no lua: line.
            ArmLatchedKillHook(L);
        }
    } catch (...) {
        return 0;
    }
    return 0;
}

// The exit([code]) global: how a script ends itself with its own code. It
// only touches atomics and Lua API calls (no std::string building), so no C++
// exception can escape into Lua's frames (see LuaToolTrampoline).
int LuaProcess::LuaExitTrampoline(lua_State* L) {
    LuaProcess* self = SelfFromState(L);
    lua_Integer code = 0;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        if (lua_isboolean(L, 1)) {
            // As os.exit takes them: true is success, false failure.
            code = lua_toboolean(L, 1) ? 0 : 1;
        } else {
            int isInteger = 0;
            // lua_tointegerx refuses a float without an exact integer value,
            // and anything that is not a number at all.
            code = lua_tointegerx(L, 1, &isInteger);
            if (!isInteger) {
                return luaL_argerror(L, 1, "number or boolean expected");
            }
        }
    }
    self->m_exitCodeRequested = static_cast<int>(code);
    self->m_exitRequested = true;
    // Re-armed exactly as on a kill (every instruction/call/return/line, on the
    // main thread too, so an exit() taken inside a coroutine latches across to
    // the thread that resumed it; the resume/wrap wrappers arm any thread in
    // between), so the error raised below cannot be swallowed: a pcall around
    // exit() strips one pcall level per caught error until the top-level
    // lua_pcall returns.
    ArmLatchedKillHook(L);
    return luaL_error(L, "exit");
}

void LuaProcess::RegisterBindings(lua_State* L) {
    *static_cast<LuaProcess**>(lua_getextraspace(L)) = this;

    lua_pushcfunction(L, &LuaProcess::LuaPrintTrampoline);
    lua_setglobal(L, "print");

    // The sandbox's one global addition: exit(), which os.exit would have been
    // if `os` could be opened (see OpenSafeLuaLibs).
    lua_pushcfunction(L, &LuaProcess::LuaExitTrampoline);
    lua_setglobal(L, "exit");

    if (m_toolFactory) {
        for (const auto& toolName : m_toolFactory->GetAvailableTools()) {
            lua_pushstring(L, toolName.c_str());
            lua_pushcclosure(L, &LuaProcess::LuaToolTrampoline, 1);
            lua_setglobal(L, toolName.c_str());
        }
    }

    lua_newtable(L);
    for (size_t i = 0; i < m_args.size(); ++i) {
        // With its length, so an argument keeps every byte it was given.
        lua_pushlstring(L, m_args[i].data(), m_args[i].size());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setglobal(L, "arg");
}

void LuaProcess::RunThread() {
    // A runtime thread (see DestroyOffRuntimeThreads.h): a tool call made from
    // here can hold the last reference to this process and to its OS, and
    // whatever it lets go of last is then destroyed on the destruction thread,
    // not here. It also names this thread in every log line.
    RuntimeThreadScope runtimeThread("lua " + m_path + " pid=" + std::to_string(m_pid));
    // The process's stop token on its own thread, so a blocked pipe call
    // notices when the process is asked to stop.
    StopTokenScope stopTokenScope(m_stopToken);
    LogDebug("LuaProcess '%s' RunThread starting (%zu bytes of script)", m_path.c_str(), m_scriptContent.size());
    // How the script ended drives the exit code below: ranToEnd says the chunk
    // returned on its own; scriptFailed a load or runtime error (not a stop and
    // not an exit()). Both start false for a state that was never created.
    bool ranToEnd = false;
    bool scriptFailed = false;
    // Anything thrown here would otherwise take down the whole program and, worse,
    // leave m_finished false so every WaitToFinish() hangs. The finished-marking
    // below therefore has to run on every path out of the Lua work.
    try {
    m_luaState = luaL_newstate();
    if (!m_luaState) {
        LogError("LuaProcess '%s': failed to create Lua state", m_path.c_str());
        scriptFailed = true;
    } else {
        OpenSafeLuaLibs(m_luaState);
        RegisterBindings(m_luaState);
        lua_sethook(m_luaState, &KillHookTrampoline, LUA_MASKCOUNT, 1000);

        // Mode "t" accepts source text only. The default ("bt") would also accept
        // precompiled bytecode, which Lua's undump does not validate -- and a .lua
        // program is untrusted input, since an agent can write one via os_write_file
        // and then launch it via os_start_process. The "@" makes Lua take the
        // chunk name as a path, so its messages read "<path>:<line>:" as the
        // standalone interpreter's do.
        const std::string chunkName = "@" + m_path;
        // The message handler goes under the chunk, so the error value is made
        // text inside the protected call (see LuaErrorMessageHandler).
        lua_pushcfunction(m_luaState, &LuaErrorMessageHandler);
        const int messageHandler = lua_gettop(m_luaState);
        if (luaL_loadbufferx(m_luaState, m_scriptContent.data(), m_scriptContent.size(), chunkName.c_str(), "t") != LUA_OK) {
            const std::string rendered = RenderLuaError(m_luaState);
            LogError("LuaProcess '%s': failed to load script: %s", m_path.c_str(), rendered.c_str());
            // As the standalone lua prints it, without the traceback.
            WriteToDescriptor(IFileIO::kStdErr, "lua: " + rendered + "\n");
            scriptFailed = true;
        } else if (lua_pcall(m_luaState, 0, 0, messageHandler) != LUA_OK) {
            if (IsExitRequested()) {
                // Not a fault: exit() aborts the script by raising through the
                // kill hook, so this is the expected way such a script unwinds.
                LogInfo("LuaProcess '%s': ended by exit(%d)", m_path.c_str(), m_exitCodeRequested.load());
            } else if (IsKillRequested()) {
                // Not a fault: the kill hook aborts the script by raising, so this
                // is the expected way a killed process unwinds.
                LogInfo("LuaProcess '%s': killed by request", m_path.c_str());
            } else {
                const std::string rendered = RenderLuaError(m_luaState);
                LogError("LuaProcess '%s': script error: %s", m_path.c_str(), rendered.c_str());
                WriteToDescriptor(IFileIO::kStdErr, "lua: " + rendered + "\n");
                scriptFailed = true;
            }
        } else {
            ranToEnd = true;
        }

        lua_close(m_luaState);
        m_luaState = nullptr;
    }
    } catch (const std::exception& e) {
        LogError("LuaProcess '%s': unexpected exception: %s", m_path.c_str(), e.what());
        scriptFailed = true;
    } catch (...) {
        LogError("LuaProcess '%s': unexpected unknown exception", m_path.c_str());
        scriptFailed = true;
    }
    if (m_luaState) {
        lua_close(m_luaState);
        m_luaState = nullptr;
    }

    // Every descriptor this process opened is released before it reports
    // finished, so a pipe's reader sees end of file when its writer's program
    // ends. Like the finished-marking below, it has to run on every path out.
    try {
        m_io->ReleaseAllDescriptors();
    } catch (...) {
        LogError("LuaProcess '%s': unexpected exception releasing its descriptors", m_path.c_str());
    }
    {
        std::lock_guard<std::mutex> lock(m_finishedMutex);
        // The Lua runtime's one ProcessEnd decision point (see ExitCodes.h): a
        // broken pipe first of all -- 141 -- so even a script's own error line
        // written into a pipe nobody reads ends that way, as a standalone lua
        // would from SIGPIPE; then a script's own exit() is the code it asked
        // for; a stop that caught the script still running is 143 (one
        // finished before a late Kill() keeps its own code); otherwise a load
        // or runtime error is 1 and a clean run 0.
        if (m_brokenPipe) {
            m_exitCode = ExitCodeFor(ProcessEnd::BrokenPipe, 0);
        } else if (m_exitRequested) {
            m_exitCode = ExitCodeFor(ProcessEnd::Exited, m_exitCodeRequested);
        } else if (m_killed && !ranToEnd) {
            m_exitCode = ExitCodeFor(ProcessEnd::Stopped, 0);
        } else {
            m_exitCode = ExitCodeFor(ProcessEnd::Exited, scriptFailed ? 1 : 0);
        }
        m_finished = true;
    }
    m_finishedCv.notify_all();
    LogDebug("LuaProcess '%s' RunThread finished", m_path.c_str());
}

}
