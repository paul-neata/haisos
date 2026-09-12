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
// OS's tool-only sandboxing. The base library's `dofile`/`loadfile` (which
// read directly from the real disk) are removed after opening; `load` is left
// available since it only executes Lua source/bytecode already in-process.
void OpenSafeLuaLibs(lua_State* L) {
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_settop(L, 0);

    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
}

void KillHookTrampoline(lua_State* L, lua_Debug* /*ar*/) {
    if (SelfFromState(L)->IsKillRequested()) {
        luaL_error(L, "process killed");
    }
}

} // namespace

LuaProcess::LuaProcess(
    uint64_t pid,
    uint64_t parentPid,
    const std::string& name,
    std::string scriptContent,
    std::vector<std::string> args,
    IToolFactory& toolFactory,
    std::shared_ptr<IAgentConsole> console)
    : m_pid(pid)
    , m_parentPid(parentPid)
    , m_name(name)
    , m_scriptContent(std::move(scriptContent))
    , m_args(std::move(args))
    , m_toolFactory(toolFactory)
    , m_console(std::move(console))
{
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
    LuaProcess* self = SelfFromState(L);
    const char* toolName = lua_tostring(L, lua_upvalueindex(1));

    nlohmann::json args = nlohmann::json::object();
    if (lua_gettop(L) >= 1 && lua_istable(L, 1)) {
        args = ToJson(L, 1);
    }

    auto tool = self->m_toolFactory.CreateTool(toolName ? toolName : "", nullptr);
    if (!tool) {
        lua_pushstring(L, "unknown tool");
        lua_pushboolean(L, true);
        return 2;
    }

    ToolResult result = tool->Call(nullptr, args);

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
}

int LuaProcess::LuaPrintTrampoline(lua_State* L) {
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
    return 0;
}

void LuaProcess::RegisterBindings(lua_State* L) {
    *static_cast<LuaProcess**>(lua_getextraspace(L)) = this;

    lua_pushcfunction(L, &LuaProcess::LuaPrintTrampoline);
    lua_setglobal(L, "print");

    for (const auto& toolName : m_toolFactory.GetAvailableTools()) {
        lua_pushstring(L, toolName.c_str());
        lua_pushcclosure(L, &LuaProcess::LuaToolTrampoline, 1);
        lua_setglobal(L, toolName.c_str());
    }

    lua_newtable(L);
    for (size_t i = 0; i < m_args.size(); ++i) {
        lua_pushstring(L, m_args[i].c_str());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setglobal(L, "arg");
}

void LuaProcess::RunThread() {
    m_luaState = luaL_newstate();
    if (!m_luaState) {
        LogError("LuaProcess '%s': failed to create Lua state", m_name.c_str());
    } else {
        OpenSafeLuaLibs(m_luaState);
        RegisterBindings(m_luaState);
        lua_sethook(m_luaState, &KillHookTrampoline, LUA_MASKCOUNT, 1000);

        if (luaL_loadbuffer(m_luaState, m_scriptContent.data(), m_scriptContent.size(), m_name.c_str()) != LUA_OK) {
            const char* err = lua_tostring(m_luaState, -1);
            LogError("LuaProcess '%s': failed to load script: %s", m_name.c_str(), err ? err : "unknown error");
            if (m_console) {
                m_console->Write("[" + m_name + "] Error: " + std::string(err ? err : "failed to load script"));
            }
        } else if (lua_pcall(m_luaState, 0, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(m_luaState, -1);
            LogError("LuaProcess '%s': script error: %s", m_name.c_str(), err ? err : "unknown error");
            if (m_console) {
                m_console->Write("[" + m_name + "] Error: " + std::string(err ? err : "script error"));
            }
        }

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
