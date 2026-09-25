#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "src/components/libheaders/DestroyOffRuntimeThreads.h"

using namespace Haisos;

// DestroyOffRuntimeThreads, RuntimeThreadScope and the DestructionThread on
// their own. The problem they solve -- an object whose destructor waits for a
// thread, released last on that very thread -- is written up in full in
// tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp, under "Objects
// released last on their own threads"; ThreadOwner below is that problem in
// miniature.

namespace {

constexpr uint64_t kWaitMs = 5000;

// Whether, where and when an object was destroyed.
struct DestructionRecord {
    std::mutex mutex;
    std::condition_variable cv;
    bool destroyed = false;
    std::thread::id destroyedOn;
    // For ThreadOwner: its thread's id, and whether that thread had finished
    // using the owner by the time the owner was destroyed.
    std::thread::id ownerThread;
    bool ownerThreadDone = false;
    bool ownerThreadDoneBeforeDestruction = false;

    void RecordDestruction() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            destroyed = true;
            destroyedOn = std::this_thread::get_id();
            ownerThreadDoneBeforeDestruction = ownerThreadDone;
        }
        cv.notify_all();
    }

    bool WaitUntilDestroyed() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(kWaitMs), [this] { return destroyed; });
    }
};

// Records where it is destroyed, and nothing else.
class Probe {
public:
    static std::shared_ptr<Probe> Create(std::shared_ptr<DestructionRecord> record) {
        return std::shared_ptr<Probe>(new Probe(std::move(record)), DestroyOffRuntimeThreads<Probe>("probe"));
    }
    ~Probe() { m_record->RecordDestruction(); }

private:
    explicit Probe(std::shared_ptr<DestructionRecord> record) : m_record(std::move(record)) {}

    std::shared_ptr<DestructionRecord> m_record;
};

// The problem in miniature. Like LuaProcess, it owns a thread and joins it in
// its destructor; like a script whose tool call returns, the thread lets go of
// the last reference to it and then goes on using it. Destroyed on the spot,
// on that thread, it would join itself: std::system_error out of a destructor,
// which is std::terminate -- the whole test program would go down.
class ThreadOwner {
public:
    // Starts the owner's thread, handing it the only reference to the owner.
    static void Launch(std::shared_ptr<DestructionRecord> record) {
        auto owner = std::shared_ptr<ThreadOwner>(
            new ThreadOwner(std::move(record)), DestroyOffRuntimeThreads<ThreadOwner>("thread owner"));
        ThreadOwner* raw = owner.get();
        // The thread may let go of the owner before m_thread is even set; the
        // lock keeps the destructor from reading it before then.
        std::lock_guard<std::mutex> lock(raw->m_threadMutex);
        raw->m_thread = std::thread(&ThreadOwner::Run, raw, std::move(owner));
    }

    ~ThreadOwner() {
        {
            std::lock_guard<std::mutex> lock(m_threadMutex);
            m_thread.join();
        }
        m_record->RecordDestruction();
    }

private:
    explicit ThreadOwner(std::shared_ptr<DestructionRecord> record) : m_record(std::move(record)) {}

    void Run(std::shared_ptr<ThreadOwner> self) {
        RuntimeThreadScope runtimeThread("thread owner");
        {
            std::lock_guard<std::mutex> lock(m_record->mutex);
            m_record->ownerThread = std::this_thread::get_id();
        }
        // The last reference to the owner, let go of on the owner's own thread.
        self.reset();
        // Still whole: its destruction waits for this thread to end.
        std::lock_guard<std::mutex> lock(m_record->mutex);
        m_record->ownerThreadDone = true;
    }

    std::shared_ptr<DestructionRecord> m_record;
    std::mutex m_threadMutex;
    std::thread m_thread;
};

} // namespace

TEST(DestroyOffRuntimeThreadsTest, ReleasedOffARuntimeThreadItIsDestroyedOnTheSpot) {
    auto record = std::make_shared<DestructionRecord>();
    auto probe = Probe::Create(record);

    probe.reset();

    // No waiting: the release itself destroyed it, here.
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_TRUE(record->destroyed);
    EXPECT_EQ(record->destroyedOn, std::this_thread::get_id());
}

TEST(DestroyOffRuntimeThreadsTest, ReleasedOnARuntimeThreadItIsDestroyedOnTheDestructionThread) {
    auto record = std::make_shared<DestructionRecord>();
    auto probe = Probe::Create(record);
    std::thread::id runtimeThreadId;

    std::thread runtimeThread([&] {
        RuntimeThreadScope scope("probe releaser");
        runtimeThreadId = std::this_thread::get_id();
        probe.reset();
    });
    runtimeThread.join();

    ASSERT_TRUE(record->WaitUntilDestroyed());
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_NE(record->destroyedOn, runtimeThreadId);
    EXPECT_NE(record->destroyedOn, std::this_thread::get_id());
}

TEST(DestroyOffRuntimeThreadsTest, AnObjectReleasedLastOnItsOwnThreadIsDestroyedOnceThatThreadIsDone) {
    auto record = std::make_shared<DestructionRecord>();

    ThreadOwner::Launch(record);

    ASSERT_TRUE(record->WaitUntilDestroyed());
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_NE(record->destroyedOn, record->ownerThread);
    EXPECT_TRUE(record->ownerThreadDoneBeforeDestruction);
}

TEST(DestroyOffRuntimeThreadsTest, AScopeMarksAndNamesItsThreadForAsLongAsItLasts) {
    const std::string marker = "logged inside a RuntimeThreadScope";
    std::mutex mutex;
    std::string loggedThread;
    const int token = LogRegisterMessageReceiver([&](const LogMessage& msg) {
        if (msg.message == marker) {
            std::lock_guard<std::mutex> lock(mutex);
            loggedThread = msg.thread;
        }
    });

    bool runtimeBefore = true;
    bool runtimeDuring = false;
    bool runtimeAfter = true;
    std::string nameBefore;
    std::string nameDuring;
    std::string nameAfter;
    std::thread([&] {
        runtimeBefore = RuntimeThreadScope::IsCurrentThreadRuntime();
        nameBefore = LogCurrentThreadName();
        {
            RuntimeThreadScope scope("lua probe.lua pid=7");
            runtimeDuring = RuntimeThreadScope::IsCurrentThreadRuntime();
            nameDuring = LogCurrentThreadName();
            LogInfo("%s", marker.c_str());
        }
        runtimeAfter = RuntimeThreadScope::IsCurrentThreadRuntime();
        nameAfter = LogCurrentThreadName();
    }).join();
    LogUnregisterMessageReceiver(token);

    EXPECT_FALSE(runtimeBefore);
    EXPECT_TRUE(runtimeDuring);
    EXPECT_FALSE(runtimeAfter);
    EXPECT_EQ(nameDuring, "lua probe.lua pid=7");
    EXPECT_EQ(loggedThread, "lua probe.lua pid=7");
    // A thread nobody named is "t<N>", and it gets that name back.
    EXPECT_EQ(nameBefore.rfind("t", 0), 0u);
    EXPECT_EQ(nameAfter, nameBefore);
}
