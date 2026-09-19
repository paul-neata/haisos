#pragma once
#include "interfaces/IProcess.h"

namespace Haisos {

// What the OS needs from a process on top of what anyone else may ask of it.
// IProcess::TriggerStop is only a request, and an OS that is shutting down has
// to be able to insist -- so Kill lives here instead, inside the OS component,
// out of reach of another process holding an IProcess handle.
//
// Every process runtime under an IHaisosOS implements this; it is the type the
// OS tracks its processes as.
class IOSProcess : public ICurrentProcess {
public:
    // Forces the process down, as far as its runtime allows. Still asynchronous:
    // pair it with WaitToFinish.
    virtual void Kill() = 0;
};

}
