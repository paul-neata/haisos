#include "LuaProcess.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>
#include "src/components/libheaders/DestroyOffRuntimeThreads.h"
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

// Opens only the Lua libraries that are safe for a sandboxed .lua process:
// base, table, string, math, utf8, and coroutine. Deliberately omits `io`,
// `os`, `package`, and `debug`, which would grant raw filesystem/process/env
// access and native library loading, bypassing the rooted IFileSystem and the
// OS's tool-only sandboxing. Four base-library globals are removed after
// opening:
//  - `dofile`/`loadfile` read directly from the real disk, escaping the jail;
//  - `load` defaults to mode "bt", i.e. it accepts *binary* chunks, and Lua's
//    bytecode loader does not validate untrusted input: a crafted binary chunk
//    yields arbitrary memory read/write and native code execution, defeating
//    the whole sandbox. Nil'ing `load` is simpler and strictly safer than
//    wrapping it to force mode "t", and no .lua process needs to compile source
//    at runtime;
//  - `warn` writes straight to the host process's stderr, bypassing the `print`
//    override and the per-process console tagging.
void OpenSafeLuaLibs(lua_State* L) {
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_settop(L, 0);

    static const char* const kRemovedGlobals[] = {"dofile", "loadfile", "load", "warn"};
    for (const char* name : kRemovedGlobals) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
}

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
// hook survives the error unwinding back into an outer frame.
void KillHookTrampoline(lua_State* L, lua_Debug* /*ar*/) {
    if (SelfFromState(L)->IsKillRequested()) {
        lua_sethook(L, &KillHookTrampoline,
                    LUA_MASKCOUNT | LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE, 1);
        luaL_error(L, "process killed");
    }
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
    std::shared_ptr<IAgentConsole> console)
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
            std::move(toolFactory),
            std::move(console)),
        DestroyOffRuntimeThreads<LuaProcess>("LuaProcess '" + path + "' pid=" + std::to_string(pid)));
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
    std::shared_ptr<IToolFactory> toolFactory,
    std::shared_ptr<IAgentConsole> console)
    : m_pid(pid)
    , m_parentPid(parentPid)
    , m_environment(std::move(environment))
    , m_path(path)
    , m_os(os)
    , m_io(ProcessFileIO::Create(std::move(os), workingDirectory))
    , m_scriptContent(std::move(scriptContent))
    , m_args(std::move(args))
    , m_toolFactory(std::move(toolFactory))
    , m_console(std::move(console))
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
    // Building the line and writing to the console both allocate, so a failure
    // here drops the output rather than taking the process down.
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
        if (self->m_console) {
            self->m_console->Write(line);
        }
    } catch (...) {
        return 0;
    }
    return 0;
}

void LuaProcess::RegisterBindings(lua_State* L) {
    *static_cast<LuaProcess**>(lua_getextraspace(L)) = this;

    lua_pushcfunction(L, &LuaProcess::LuaPrintTrampoline);
    lua_setglobal(L, "print");

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
    LogDebug("LuaProcess '%s' RunThread starting (%zu bytes of script)", m_path.c_str(), m_scriptContent.size());
    // Anything thrown here would otherwise take down the whole program and, worse,
    // leave m_finished false so every WaitToFinish() hangs. The finished-marking
    // below therefore has to run on every path out of the Lua work.
    try {
    m_luaState = luaL_newstate();
    if (!m_luaState) {
        LogError("LuaProcess '%s': failed to create Lua state", m_path.c_str());
    } else {
        OpenSafeLuaLibs(m_luaState);
        RegisterBindings(m_luaState);
        lua_sethook(m_luaState, &KillHookTrampoline, LUA_MASKCOUNT, 1000);

        // Mode "t" accepts source text only. The default ("bt") would also accept
        // precompiled bytecode, which Lua's undump does not validate -- and a .lua
        // program is untrusted input, since an agent can write one via os_write_file
        // and then launch it via os_start_process.
        if (luaL_loadbufferx(m_luaState, m_scriptContent.data(), m_scriptContent.size(), m_path.c_str(), "t") != LUA_OK) {
            const char* err = lua_tostring(m_luaState, -1);
            LogError("LuaProcess '%s': failed to load script: %s", m_path.c_str(), err ? err : "unknown error");
            if (m_console) {
                m_console->Write("[" + m_path + "] Error: " + std::string(err ? err : "failed to load script"));
            }
        } else if (lua_pcall(m_luaState, 0, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(m_luaState, -1);
            if (IsKillRequested()) {
                // Not a fault: the kill hook aborts the script by raising, so this
                // is the expected way a killed process unwinds.
                LogInfo("LuaProcess '%s': killed by request", m_path.c_str());
            } else {
                LogError("LuaProcess '%s': script error: %s", m_path.c_str(), err ? err : "unknown error");
                if (m_console) {
                    m_console->Write("[" + m_path + "] Error: " + std::string(err ? err : "script error"));
                }
            }
        }

        lua_close(m_luaState);
        m_luaState = nullptr;
    }
    } catch (const std::exception& e) {
        LogError("LuaProcess '%s': unexpected exception: %s", m_path.c_str(), e.what());
    } catch (...) {
        LogError("LuaProcess '%s': unexpected unknown exception", m_path.c_str());
    }
    if (m_luaState) {
        lua_close(m_luaState);
        m_luaState = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_finishedMutex);
        m_finished = true;
    }
    m_finishedCv.notify_all();
    LogDebug("LuaProcess '%s' RunThread finished", m_path.c_str());
}

}
