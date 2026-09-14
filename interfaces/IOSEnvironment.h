#pragma once
#include <map>
#include <string>

namespace Haisos {

// An OS's environment: plain key/value strings, fixed once the OS is created
// and inherited by everything it starts (child processes and sub-OS instances).
//
// This is the Haisos OS's own environment, deliberately separate from the host
// process's: a host variable only appears here if the haisosfile imported it
// with `ENV <name>`. The LLM configuration lives here too, under the keys below.
//
// It sits in its own header because both IFactory (which creates an OS) and
// IHaisosOS (which exposes its environment) need it, and neither may include
// the other.
using OSEnvironment = std::map<std::string, std::string>;

inline constexpr const char* kEnvEndpoint = "HAISOS_ENDPOINT";
inline constexpr const char* kEnvModel = "HAISOS_MODEL";
inline constexpr const char* kEnvApiKey = "HAISOS_API_KEY";

}
