// Port/net: the netcode's command line (net_main.cpp), called by Port/main.cpp before the game starts.
#pragma once
#include <string>
#include <vector>

// Takes the netcode's options out of <args>. 0: no network game; 1: start the network game (the caller adds the
// game's -N); -1: an error, already printed
int NetMain_Init(std::vector<std::string>& args);
