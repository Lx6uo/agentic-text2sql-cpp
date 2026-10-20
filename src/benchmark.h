#pragma once
#include "agent.h"

namespace t2s {
struct BenchmarkOptions {
    std::string dataset;
    std::string split="dev";
    std::string mode="agentic";
    std::string output;
    std::string predictions;
    int limit=20;
    bool verbose=false;
};
bool equivalent_results(const Json& predicted,const Json& gold,bool ordered);
bool has_outer_order_by(const std::string& sql);
Json run_benchmark(const BenchmarkOptions& options,const Config& config,Completion complete);
}
