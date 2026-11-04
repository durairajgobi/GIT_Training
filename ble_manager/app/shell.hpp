// SPDX-License-Identifier: MIT
#pragma once

#include <string>

#include "blemgr/ble_manager.hpp"

namespace blemgrd {

/// Executes one interactive command. Returns false when the user asked to quit.
/// Results of asynchronous operations are printed when they complete.
bool runCommand(blemgr::BleManager& mgr, const std::string& line);

void printHelp();

}  // namespace blemgrd
