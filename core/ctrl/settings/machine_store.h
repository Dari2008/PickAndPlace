/**
 * machine_store.h - Octopus machine values the user changed, kept in /machine.txt on the Pi
 * -----------------------------------------------------------------------------
 * The Octopus keeps its settings in RAM only (octopus-fw/src/settings.h). Every
 * value set with SET on the Pi that is not a Pi setting goes to the board and
 * into /machine.txt ("steps_x = 80"); on every (re)connect pushAll() sends the
 * whole file again, then fetch() reads the board's complete list (GET).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <map>
#include <string>

namespace machineStore
{
    bool load();
    bool save();
    const std::map<std::string, std::string> &values();
    void set(const std::string &key, const std::string &value);
    void clear();
    // Send every stored value to the Octopus. Returns the number of rejected keys
    // (their error text goes to `err`).
    int pushAll(std::string &err);
    // GET from the Octopus into octo.values(). False when the board doesn't answer.
    bool fetch();
}
