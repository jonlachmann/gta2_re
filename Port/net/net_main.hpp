// Port/net: the netcode's command line (net_main.cpp), called by Port/main.cpp before the game starts.
#pragma once
#include <string>
#include <vector>

// Takes the netcode's options out of <args>. 0: no network game; 1: start the network game (the caller adds the
// game's -N); -1: an error, already printed
int NetMain_Init(std::vector<std::string>& args);

// The frontend's network page (Port/net/menu.cpp): a join in the background. NetMenu_Poll each frame: 1 welcomed (the
// network game is set up: the frontend boots it), 0 not yet, -1 failed (<error> says why)
bool NetMenu_Join(const std::string& address, const std::string& name, std::string* error);
int NetMenu_Poll(std::string* error);
void NetMenu_Cancel();
const std::string& NetMenu_DefaultName(); // --name, if the command line gave one
void NetMenu_Install(); // the page's hooks in the frontend (Port/net/menu.cpp)
int NetMenu_Page(); // its frontend page
void NetMenu_SetStatus(const std::string& status); // the line under its options
