#pragma once
#include <memory>
#include "interfaces/IHaisosOS.h"
#include "src/components/libheaders/CurrentProcessHandle.h"
#include <nlohmann/json.hpp>

namespace Haisos::Tools {

class OSListProcessesTool : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    // Built around the process it acts for, never around an OS: ICurrentProcess
    // is the only door out of a process, so whatever OS that process was given
    // is exactly what this tool can reach (see the Security section of the root
    // CLAUDE.md).
    static std::shared_ptr<OSListProcessesTool> Create(std::shared_ptr<CurrentProcessHandle> process) {
        return std::shared_ptr<OSListProcessesTool>(new OSListProcessesTool(std::move(process)));
    }
    ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }

private:
    explicit OSListProcessesTool(std::shared_ptr<CurrentProcessHandle> process);

    std::shared_ptr<CurrentProcessHandle> m_process;
};

}
