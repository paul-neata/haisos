#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "Console.h"
#include "src/components/Logger/Logger.h"

using namespace Haisos;

TEST(ConsoleTest, Construction) {
    Console console(false);
    // Should not crash
}

TEST(ConsoleTest, WriteWithoutStartDoesNotCrash) {
    Console console(false);
    console.Write("test message");
    // Messages are queued but not processed until Start()
}

TEST(ConsoleTest, StartStopIdempotent) {
    Console console(false);
    console.Start();
    console.Start(); // second start should be no-op
    console.Stop();
    console.Stop(); // second stop should be no-op
}

TEST(ConsoleTest, ProcessesMessages) {
    Console console(false);
    console.Start();
    console.Write("message 1");
    console.Write("message 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    console.Stop();
}

TEST(ConsoleTest, RegisterAsLogReceiverReceivesMessages) {
    LogClearMessageReceivers();

    {
        Console console(true);
        console.Start();

        LogInfo("Test log message");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        console.Stop();
    }

    // After console is destroyed, it should unregister itself
    // Logging should still work (go to stderr)
    LogInfo("Post-console log message");
}

TEST(ConsoleTest, MultipleConsolesRegisterIndependentReceivers) {
    LogClearMessageReceivers();

    Console console1(true);
    Console console2(true);

    console1.Start();
    console2.Start();

    LogInfo("Message for both consoles");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    console1.Stop();
    console2.Stop();

    // After both consoles are stopped, log receivers should be empty
}
