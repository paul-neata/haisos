#include "BuiltinCommand.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace Haisos {

namespace {

class SleepCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "sleep"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options;
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "delay for a specified amount of time",
            {"sleep NUMBER[SUFFIX]..."},
            "SUFFIX: s (seconds, the default), m (minutes), h (hours), d (days);\n"
            "NUMBER may be fractional, several are added.",
        };
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }
        if (parsed->operands.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }

        // Every operand is checked first; only then does the sleep begin.
        double totalSeconds = 0.0;
        bool anyInvalid = false;
        for (const std::string& arg : parsed->operands) {
            // strtod skips leading whitespace and takes decimal, exponent, hex
            // and inf/infinity; what follows must be nothing or one suffix.
            const char* begin = arg.c_str();
            char* end = nullptr;
            const double value = std::strtod(begin, &end);
            bool valid = end != begin && !std::isnan(value) && value >= 0.0;
            double seconds = value;
            if (valid) {
                const size_t rest = static_cast<size_t>(end - begin);
                if (rest < arg.size()) {
                    switch (arg[rest]) {
                        case 's': break;
                        case 'm': seconds *= 60.0; break;
                        case 'h': seconds *= 3600.0; break;
                        case 'd': seconds *= 86400.0; break;
                        default: valid = false; break;
                    }
                    if (rest + 1 < arg.size()) {
                        valid = false;
                    }
                }
            }
            if (!valid) {
                context.Error("invalid time interval " + ShellEscapeQuoted(arg, true));
                anyInvalid = true;
            } else {
                totalSeconds += seconds;
            }
        }
        if (anyInvalid) {
            context.TryHelp();
            return 1;
        }

        // In slices, so a stop ends the sleep at once.
        constexpr double kSliceSeconds = 0.05;
        if (std::isinf(totalSeconds)) {
            while (!context.StopRequested()) {
                std::this_thread::sleep_for(std::chrono::duration<double>(kSliceSeconds));
            }
        } else {
            const auto start = std::chrono::steady_clock::now();
            while (!context.StopRequested()) {
                const double elapsed =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                if (elapsed >= totalSeconds) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::duration<double>(
                    std::min(kSliceSeconds, totalSeconds - elapsed)));
            }
        }
        return 0;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateSleepCommand() {
    return std::make_shared<SleepCommand>();
}

} // namespace Haisos