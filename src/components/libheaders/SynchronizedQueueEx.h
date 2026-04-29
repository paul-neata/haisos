#pragma once
#include <memory>
#include <future>
#include "src/components/libheaders/SynchronizedQueue.h"

namespace Haisos {

template <typename T>
struct SynchronizedQueueExItem {
    T data;
    std::shared_ptr<std::promise<void>> poppedPromise;
};

template <typename T>
class SynchronizedQueueEx {
public:
    void Post(T item) {
        SynchronizedQueueExItem<T> wrapper;
        wrapper.data = std::move(item);
        m_queue.Post(std::move(wrapper));
    }

    bool Pop(T& item) {
        SynchronizedQueueExItem<T> wrapper;
        if (!m_queue.Pop(wrapper)) {
            return false;
        }
        if (wrapper.poppedPromise) {
            wrapper.poppedPromise->set_value();
        }
        item = std::move(wrapper.data);
        return true;
    }

    void Close() {
        m_queue.Close();
    }

    void Send(T item) {
        auto promise = std::make_shared<std::promise<void>>();
        auto future = promise->get_future();
        SynchronizedQueueExItem<T> wrapper;
        wrapper.data = std::move(item);
        wrapper.poppedPromise = std::move(promise);
        m_queue.Post(std::move(wrapper));
        future.wait();
    }

private:
    SynchronizedQueue<SynchronizedQueueExItem<T>> m_queue;
};

}
