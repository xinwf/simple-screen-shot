#pragma once
#include <string>

namespace sc::platform {

// Returns true when another instance of THIS binary is already running
// (SIGUSR1 is sent to it so it re-shows its delay dialog) and this process
// should exit quietly. Returns false when we are the primary instance.
bool anotherInstanceRunning();

// Writes this process's pid to the per-binary instance file. Call after
// anotherInstanceRunning() returned false.
void publishInstancePid();

// Removes the instance file. Call when the app exits.
void clearInstancePid();

} // namespace sc::platform
