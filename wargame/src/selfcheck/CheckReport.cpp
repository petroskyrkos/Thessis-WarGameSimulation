#include "selfcheck/CheckReport.h"

#include <iostream>

namespace wargame::selfcheck {

namespace {
bool g_allPassed = true;
}

void report(const char* what, bool pass) {
    std::cout << "  " << (pass ? "ok   " : "FAIL ") << what << "\n";
    g_allPassed = g_allPassed && pass;
}

bool allPassed() { return g_allPassed; }

}  // namespace wargame::selfcheck
