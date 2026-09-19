#pragma once
#include <atomic>
#include <cstdint>

namespace Haisos {

// One process-id allocator for the whole program, not one per OS or per
// factory: a pid live in one OS can never appear in another, so a pid
// identifies a process (and, through IHaisosOS::GetOSProcessID(), the OS it
// runs under) on its own. Starts at 1 -- 0 is never allocated and means
// "no parent".
//
// IFactory::GetNextGloballyUniquePID() is the interface door onto this; an
// IHaisosOS numbers the processes it starts from here directly, since it
// holds no factory of its own.
inline uint64_t NextGloballyUniquePID() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}

}
