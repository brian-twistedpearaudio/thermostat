// Wi-Fi, network time and first-time setup through a temporary hotspot.
// Runs in its own task: heating control never waits for the network.
#pragma once
#include <stdint.h>

namespace net {

enum class State : uint8_t {
  NotSetUp,    // no saved network
  Connecting,
  Connected,
  Offline,     // saved network not reachable; keeps retrying
  Starting,    // scanning before the hotspot opens
  Hotspot,     // waiting for someone to fill in the setup page
  Trying,      // testing the network that was just entered
};

struct Status {
  State state = State::NotSetUp;
  bool setupFailed = false;  // the last network entered could not be joined
  char ssid[33] = "";        // saved network, or the one being tried
  char name[17] = "";        // thermostat name from the setup page; empty if none
  char ip[16] = "";          // when connected
  char apName[24] = "";      // hotspot name and password, while it is open
  char apPassword[16] = "";
};

void begin();
void startSetup();   // open the hotspot
void stopSetup();    // close it and go back to the saved network
Status status();     // a copy; safe from any task

}  // namespace net
