#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

namespace Haisos {

template <typename T>
class SynchronizedQueue {
public:
    void Post(T item) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(std::move(item));
        }
        m_condition.notify_one();
    }

    bool Pop(T& item) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this] { return !m_queue.empty() || m_closed; });
        if (m_queue.empty() && m_closed) {
            return false;
        }
        item = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    void Close() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_condition.notify_all();
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_closed = false;
};

}
