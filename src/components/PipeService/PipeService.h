#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include "interfaces/IPipeService.h"

namespace Haisos {

// The IPipeService: stateless apart from the count of pipes still open, held
// shared with the PipeBuffers so a pipe outliving its service decrements a
// live counter. Holds no list of pipes: a pipe does not depend on the service
// that made it.
class PipeService : public IPipeService {
public:
    static std::shared_ptr<PipeService> Create();
    ~PipeService() override;

    PipeEnds CreatePipe(size_t capacity = 0) override;
    size_t OpenPipeCount() const override;

private:
    PipeService();

    std::shared_ptr<std::atomic<size_t>> m_openPipes;
};

}
