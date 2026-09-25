#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include "src/components/Logger/Logger.h"

namespace Haisos {

// Nothing whose destructor waits for a runtime thread is destroyed on one.
//
// A runtime thread runs a program on behalf of a process: an agent's
// conversation, a Lua script, a builtin command, or the loop feeding an
// interactive agent its console. Such a thread can hold the last reference to
// the very object that owns it -- an os_* tool holds its process and its OS for
// the length of a call, and an agent hands shared_from_this() to every tool it
// calls. Destroyed there, that object's destructor would wait for the thread it
// is running on: ~Agent would wait forever, ~LuaProcess and ~BuiltinProcess
// would end in std::terminate when they come to join themselves, and ~HaisosOS
// would wait out its stop timeout on the process whose thread it is running on.
//
// So such objects are created with the DestroyOffRuntimeThreads deleter, and
// every runtime thread's function starts with a RuntimeThreadScope. Released on
// a runtime thread, the object is handed to the DestructionThread, which may
// wait for anything; released anywhere else, it is destroyed on the spot.

// Marks the calling thread as a runtime thread -- and names it in every log
// line (see LogThreadName) -- for as long as it lives.
class RuntimeThreadScope {
public:
    explicit RuntimeThreadScope(std::string name)
        : m_logName(std::move(name))
        , m_wasRuntimeThread(Flag())
    {
        Flag() = true;
    }
    ~RuntimeThreadScope() { Flag() = m_wasRuntimeThread; }

    RuntimeThreadScope(const RuntimeThreadScope&) = delete;
    RuntimeThreadScope& operator=(const RuntimeThreadScope&) = delete;

    static bool IsCurrentThreadRuntime() { return Flag(); }

private:
    static bool& Flag() {
        thread_local bool isRuntimeThread = false;
        return isRuntimeThread;
    }

    LogThreadName m_logName;
    bool m_wasRuntimeThread;
};

// The one thread on which what must not be destroyed on a runtime thread is
// destroyed instead. It is started the first time it is handed something, and
// at exit it destroys everything it has been handed before the program ends.
// It is not a runtime thread itself, so whatever a destruction here releases
// in turn is destroyed on the spot.
class DestructionThread {
public:
    // Hands over |destroy|, which destroys |what|, and returns at once: the
    // caller may well be the thread that |destroy| is going to wait for.
    static void Hand(std::string what, std::function<void()> destroy) {
        Instance().Post(std::move(what), std::move(destroy));
    }

    ~DestructionThread() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closing = true;
        }
        m_cv.notify_all();
        m_thread.join();
    }

    DestructionThread(const DestructionThread&) = delete;
    DestructionThread& operator=(const DestructionThread&) = delete;

private:
    struct Pending {
        std::string what;
        std::function<void()> destroy;
    };

    DestructionThread() : m_thread(&DestructionThread::Run, this) {}

    static DestructionThread& Instance() {
        static DestructionThread instance;
        return instance;
    }

    void Post(std::string what, std::function<void()> destroy) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending.push_back({std::move(what), std::move(destroy)});
        }
        m_cv.notify_one();
    }

    void Run() {
        LogThreadName threadName("destruction");
        std::unique_lock<std::mutex> lock(m_mutex);
        while (true) {
            m_cv.wait(lock, [this] { return m_closing || !m_pending.empty(); });
            if (m_pending.empty()) {
                return;
            }
            Pending next = std::move(m_pending.front());
            m_pending.pop_front();
            lock.unlock();

            LogDebug("DestructionThread: destroying %s", next.what.c_str());
            const auto start = std::chrono::steady_clock::now();
            next.destroy();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            LogDebug("DestructionThread: destroyed %s in %lldms", next.what.c_str(), static_cast<long long>(elapsedMs));

            lock.lock();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Pending> m_pending;
    bool m_closing = false;
    // Last, so it starts only once everything Run() uses has been built.
    std::thread m_thread;
};

// A shared_ptr deleter for an object whose destructor waits for a runtime
// thread, so that it is never destroyed on one (see above). |what| names the
// object in the log.
template <typename T>
class DestroyOffRuntimeThreads {
public:
    explicit DestroyOffRuntimeThreads(std::string what) : m_what(std::move(what)) {}

    void operator()(T* object) const {
        if (!RuntimeThreadScope::IsCurrentThreadRuntime()) {
            delete object;
            return;
        }
        LogInfo("%s: its last reference was released on a runtime thread, so it is destroyed on the destruction thread",
            m_what.c_str());
        DestructionThread::Hand(m_what, [object] { delete object; });
    }

private:
    std::string m_what;
};

}
