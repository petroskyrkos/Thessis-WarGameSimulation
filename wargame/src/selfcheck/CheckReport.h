// Pass/fail reporting shared by every self-check.
//
// Each check prints one line, "ok" or "FAIL". The program's exit code is
// non-zero if any check failed.

#pragma once

namespace wargame::selfcheck {

// Prints one result line and remembers whether it failed.
void report(const char* what, bool pass);

// True while every check reported so far has passed.
bool allPassed();

}  // namespace wargame::selfcheck
