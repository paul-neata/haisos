#include "PipeService.h"
#include "Pipe.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

std::shared_ptr<PipeService> PipeService::Create() {
    return std::shared_ptr<PipeService>(new PipeService());
}

PipeService::PipeService()
    : m_openPipes(std::make_shared<std::atomic<size_t>>(0))
{
}

PipeService::~PipeService() = default;

PipeEnds PipeService::CreatePipe(size_t capacity) {
    if (capacity == 0) {
        capacity = kDefaultPipeCapacity;
    }
    auto buffer = PipeBuffer::Create(capacity, m_openPipes);
    PipeEnds ends;
    ends.readEnd = PipeReadEnd::Create(buffer);
    ends.writeEnd = PipeWriteEnd::Create(std::move(buffer));
    return ends;
}

size_t PipeService::OpenPipeCount() const {
    return m_openPipes->load();
}

}
