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
    //
    // How far that is depends on the runtime, so the default is to ask again:
    // an agent's thread sits inside an HTTP call or a tool call that has to be
    // allowed to return, and there is nothing stronger to do to it. A runtime
    // that can genuinely be interrupted mid-instruction -- a Lua interpreter --
    // overrides this.
    virtual void Kill() { TriggerStop(); }
};

}
