#include "world/Side.h"

namespace wargame {

const char* sideName(Side s) {
    switch (s) {
        case Side::None: return "none";
        case Side::Blue: return "Blue";
        case Side::Red: return "Red";
    }
    return "?";
}

}  // namespace wargame
