#include "LuaProcess.h"
#include <algorithm>
#include <chrono>
#include <vector>
#include "src/components/Logger/Logger.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace Haisos {

namespace {

void PushJson(lua_State* L, const nlohmann::json& value) {
    switch (value.type()) {
        case nlohmann::json::value_t::null:
            lua_pushnil(L);
            break;
        case nlohmann::json::value_t::boolean:
            lua_pushboolean(L, value.get<bool>());
            break;
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned:
            lua_pushinteger(L, static_cast<lua_Integer>(value.get<int64_t>()));
            break;
        case nlohmann::json::value_t::number_float:
            lua_pushnumber(L, value.get<double>());
            break;
        case nlohmann::json::value_t::string:
            lua_pushstring(L, value.get<std::string>().c_str());
            break;
        case nlohmann::json::value_t::array: {
            lua_newtable(L);
            lua_Integer i = 1;
            for (const auto& item : value) {
                PushJson(L, item);
                lua_rawseti(L, -2, i++);
            }
            break;
        }
        case nlohmann::json::value_t::object: {
            lua_newtable(L);
            for (auto it = value.begin(); it != value.end(); ++it) {
                PushJson(L, it.value());
                lua_setfield(L, -2, it.key().c_str());
            }
            break;
        }
        default:
            lua_pushnil(L);
            break;
    }
}

nlohmann::json ToJson(lua_State* L, int index) {
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
        case LUA_TSTRING:
            return std::string(lua_tostring(L, index));
        case LUA_TTABLE: {
            std::vector<std::pair<lua_Integer, nlohmann::json>> intEntries;
            nlohmann::json obj = nlohmann::json::object();
            bool hasStringKeys = false;

            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)) {
                    intEntries.emplace_back(lua_tointeger(L, -2), ToJson(L, -1));
                } else {
                    hasStringKeys = true;
                    std::string key = (lua_type(L, -2) == LUA_TSTRING) ? lua_tostring(L, -2) : "";
                    if (!key.empty()) {
                        obj[key] = ToJson(L, -1);
                    }
                }
                lua_pop(L, 1);
            }

            bool isSequence = !hasStringKeys && !intEntries.empty();
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
    const std::string& name,
    std::string scriptContent,
    std::vector<std::string> args,
    std::shared_ptr<IToolFactory> toolFactory,
    std::shared_ptr<IAgentConsole> console)
{
    auto process = std::shared_ptr<LuaProcess>(new LuaProcess(
        pid,
        parentPid,
        std::move(environment),
        name,
        std::move(scriptContent),
        std::move(args),
        std::move(toolFactory),
        std::move(console)));
    process->Start();
    return process;
}

LuaProcess::LuaProcess(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& name,
    std::string scriptContent,
    std::vector<std::string> args,
    std::shared_ptr<IToolFactory> toolFactory,
    std::shared_ptr<IAgentConsole> console)
    : m_pid(pid)
    , m_parentPid(parentPid)
    , m_environment(std::move(environment))
    , m_name(name)
    , m_scriptContent(std::move(scriptContent))
    , m_args(std::move(args))
    , m_toolFactory(std::move(toolFactory))
    , m_console(std::move(console))
{
}

void LuaProcess::Start() {
    m_thread = std::thread(&LuaProcess::RunThread, this);
}

LuaProcess::~LuaProcess() {
    Kill();
    if (!WaitToFinish(5000)) {
        LogWarning("LuaProcess '%s' thread did not finish within 5s during destruction, waiting indefinitely", m_name.c_str());
    }
    WaitToFinish();
}

uint64_t LuaProcess::GetPid() const {
    return m_pid;
}

uint64_t LuaProcess::GetParentPid() const {
    return m_parentPid;
}

std::string LuaProcess::Name() const {
    return m_name;
}

std::shared_ptr<IEnvironment> LuaProcess::GetEnvironment() const {
    return m_environment;
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

bool LuaProcess::Stop(unsigned timeoutMs) {
    // A Lua process runs its script to completion; there is no command queue
    // to close, so "stop" just means "wait" (mirroring a short-running agent).
    if (timeoutMs == 0) {
        return false;
    }
    return WaitToFinish(timeoutMs);
}

void LuaProcess::Kill() {
    m_killed = true;
}

std::shared_ptr<IAgent> LuaProcess::AsAgent() const {
    return nullptr;
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
            args = ToJson(L, 1);
        }

        auto tool = self->m_toolFactory
            ? self->m_toolFactory->CreateTool(toolName ? toolName : "", nullptr)
            : nullptr;
        if (!tool) {
            LogWarning("LuaProcess '%s': unknown tool '%s'", self->m_name.c_str(), toolName ? toolName : "");
            lua_pushstring(L, "unknown tool");
            lua_pushboolean(L, true);
            return 2;
        }

        LogDebug("LuaProcess '%s': calling tool '%s'", self->m_name.c_str(), toolName ? toolName : "");
        ToolResult result = tool->Call(nullptr, args);
        LogDebug("LuaProcess '%s': tool '%s' returned is_error=%d (%zu bytes)",
            self->m_name.c_str(), toolName ? toolName : "", result.isError ? 1 : 0, result.content.size());

        // Tool results that happen to be JSON (e.g. os_list_directory) are handed
        // back as a Lua table rather than a raw string, so scripts don't need
        // their own JSON parser for the common case.
        auto parsed = nlohmann::json::parse(result.content, nullptr, false);
        if (!result.isError && !parsed.is_discarded() && (parsed.is_object() || parsed.is_array())) {
            PushJson(L, parsed);
        } else {
            lua_pushstring(L, result.content.c_str());
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
        lua_pushstring(L, m_args[i].c_str());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setglobal(L, "arg");
}

void LuaProcess::RunThread() {
    LogDebug("LuaProcess '%s' RunThread starting (%zu bytes of script)", m_name.c_str(), m_scriptContent.size());
    // Anything thrown here would otherwise take down the whole program and, worse,
    // leave m_finished false so every WaitToFinish() hangs. The finished-marking
    // below therefore has to run on every path out of the Lua work.
    try {
    m_luaState = luaL_newstate();
    if (!m_luaState) {
        LogError("LuaProcess '%s': failed to create Lua state", m_name.c_str());
    } else {
        OpenSafeLuaLibs(m_luaState);
        RegisterBindings(m_luaState);
        lua_sethook(m_luaState, &KillHookTrampoline, LUA_MASKCOUNT, 1000);

        // Mode "t" accepts source text only. The default ("bt") would also accept
        // precompiled bytecode, which Lua's undump does not validate -- and a .lua
        // program is untrusted input, since an agent can write one via os_write_file
        // and then launch it via os_start_process.
        if (luaL_loadbufferx(m_luaState, m_scriptContent.data(), m_scriptContent.size(), m_name.c_str(), "t") != LUA_OK) {
            const char* err = lua_tostring(m_luaState, -1);
            LogError("LuaProcess '%s': failed to load script: %s", m_name.c_str(), err ? err : "unknown error");
            if (m_console) {
                m_console->Write("[" + m_name + "] Error: " + std::string(err ? err : "failed to load script"));
            }
        } else if (lua_pcall(m_luaState, 0, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(m_luaState, -1);
            if (IsKillRequested()) {
                // Not a fault: the kill hook aborts the script by raising, so this
                // is the expected way a killed process unwinds.
                LogInfo("LuaProcess '%s': killed by request", m_name.c_str());
            } else {
                LogError("LuaProcess '%s': script error: %s", m_name.c_str(), err ? err : "unknown error");
                if (m_console) {
                    m_console->Write("[" + m_name + "] Error: " + std::string(err ? err : "script error"));
                }
            }
        }

        lua_close(m_luaState);
        m_luaState = nullptr;
    }
    } catch (const std::exception& e) {
        LogError("LuaProcess '%s': unexpected exception: %s", m_name.c_str(), e.what());
    } catch (...) {
        LogError("LuaProcess '%s': unexpected unknown exception", m_name.c_str());
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
    LogDebug("LuaProcess '%s' RunThread finished", m_name.c_str());
}

}
