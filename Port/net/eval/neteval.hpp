// Port (docs/netcode.md, "Evaluations"): GTA2_NETEVAL, see neteval.cpp.
#pragma once
#include "../net/engine/world.hpp"
#include <vector>

void NetEval(const std::vector<net::engine::Block>& blocks, const char* what); // the comma-separated list
