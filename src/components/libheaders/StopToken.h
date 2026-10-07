#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace Haisos {

// The per-runtime-thread stop token that makes a blocked call -- a pipe Read or
// Write -- interruptible without polling, as C++20's std::stop_token makes a
// std::jthread's. No threads, no timers, no logging.
//
// Each process owns one token; its runtime thread(s) install it as
// StopToken::Current() with a StopTokenScope, and TriggerStop() (or
// self_close) signals it with RequestStop(). A call about to block registers a
// StopCallback on Current() that wakes it; a thread carrying no token (the
// haisos main thread, a test's thread) is never interrupted.
//
// In the house style, though not an interface implementation: a private
// constructor, a static Create(), held by shared_ptr (the process and the
// thread-local both hold it).
class StopToken {
public:
    static std::shared_ptr<StopToken> Create() {
        return std::shared_ptr<StopToken>(new StopToken());
    }

    // Idempotent. Sets the flag, then runs every registered callback once,
    // **while still holding the token's mutex**. Holding it is what lets
    // ~StopCallback (which takes the same mutex to unregister) guarantee that
    // once it returns, its callback is neither running nor will ever run --
    // so a callback may capture raw pointers to the waiter's state.
    //
    // The consequence: a callback must not call into the same token (no
    // RequestStop, no new StopCallback on it), and may take only locks that
    // are never held while a StopCallback on that token is constructed or
    // destroyed. The pipe obeys this: it constructs its StopCallback before
    // taking its own mutex and destroys it after releasing it.
    void RequestStop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopRequested.exchange(true)) {
            return;
        }
        for (const auto& [id, callback] : m_callbacks) {
            callback();
        }
    }

    bool StopRequested() const { return m_stopRequested.load(); }

    // The token installed on the calling thread by a StopTokenScope, or null.
    static std::shared_ptr<StopToken> Current() { return ThreadToken(); }

private:
    StopToken() = default;
    friend class StopCallback;
    friend class StopTokenScope;

    // A thread_local behind a static function, as RuntimeThreadScope::Flag()
    // does in DestroyOffRuntimeThreads.h.
    static std::shared_ptr<StopToken>& ThreadToken() {
        thread_local std::shared_ptr<StopToken> token;
        return token;
    }

    std::mutex m_mutex;
    std::atomic<bool> m_stopRequested{false};
    std::map<uint64_t, std::function<void()>> m_callbacks;
    uint64_t m_nextId = 0;
};

// Installs token as StopToken::Current() on the calling thread for its
// lifetime, restoring the previous one after. A null token installs nothing:
// such a thread is not interruptible.
class StopTokenScope {
public:
    explicit StopTokenScope(std::shared_ptr<StopToken> token)
        : m_previous(StopToken::ThreadToken())
    {
        if (token) {
            StopToken::ThreadToken() = std::move(token);
        }
    }
    ~StopTokenScope() { StopToken::ThreadToken() = std::move(m_previous); }

    StopTokenScope(const StopTokenScope&) = delete;
    StopTokenScope& operator=(const StopTokenScope&) = delete;

private:
    std::shared_ptr<StopToken> m_previous;
};

// Registers onStop with token for its own lifetime (as C++20
// std::stop_callback). With a null token it does nothing. With a token whose
// stop is already requested, it runs onStop at once, on the calling thread,
// before returning. Otherwise onStop is registered under the token's mutex and
// runs on the thread calling RequestStop() -- under that mutex, so that once
// ~StopCallback returns, the callback is neither running nor will ever run.
class StopCallback {
public:
    StopCallback(std::shared_ptr<StopToken> token, std::function<void()> onStop)
        : m_token(std::move(token))
    {
        if (!m_token) {
            return;
        }
        // Checked under the mutex so the state cannot change underneath:
        // RequestStop() sets the flag and runs the registered callbacks
        // under it too. The flag is sticky, so a stop already requested here
        // means no later RequestStop will ever run this callback -- it can be
        // run on this thread instead, after the lock is released.
        bool alreadyStopped;
        {
            std::lock_guard<std::mutex> lock(m_token->m_mutex);
            alreadyStopped = m_token->m_stopRequested.load();
            if (!alreadyStopped) {
                m_id = m_token->m_nextId++;
                m_token->m_callbacks.emplace(m_id, std::move(onStop));
            }
        }
        if (alreadyStopped) {
            onStop();
        }
    }

    ~StopCallback() {
        if (!m_token || m_id == kNoCallback) {
            return;
        }
        // Blocked here while a RequestStop is mid-flight, this returns only
        // after our callback has run to its end (RequestStop runs callbacks
        // under this same mutex) -- the guarantee the pipe's waiters rely on.
        std::lock_guard<std::mutex> lock(m_token->m_mutex);
        m_token->m_callbacks.erase(m_id);
    }

    StopCallback(const StopCallback&) = delete;
    StopCallback& operator=(const StopCallback&) = delete;

private:
    static constexpr uint64_t kNoCallback = ~uint64_t{0};

    std::shared_ptr<StopToken> m_token;
    uint64_t m_id = kNoCallback;
};

}
