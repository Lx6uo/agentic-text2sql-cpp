#pragma once
#include "llm.h"

namespace t2s {
Json agent_tools();
Json run_agent(Database& database, const std::string& question, Completion complete, AgentOptions options = {}, bool verbose=false);
Json run_baseline(Database& database, const std::string& question, Completion complete, bool verbose=false);
}
