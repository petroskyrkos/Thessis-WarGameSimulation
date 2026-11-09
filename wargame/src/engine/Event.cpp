#include "engine/Event.h"

#include <sstream>

namespace wargame {

const char* eventTypeName(EventType t) {
    switch (t) {
        case EventType::SimStart: return "SimStart";
        case EventType::SimStop: return "SimStop";
        case EventType::Wake: return "Wake";
        case EventType::Arrive: return "Arrive";
        case EventType::Engage: return "Engage";
        case EventType::Destroyed: return "Destroyed";
        case EventType::Resupply: return "Resupply";
    }
    return "?";
}

std::string Event::str() const {
    std::ostringstream os;
    os << "t=" << time << " #" << seq << " " << eventTypeName(type) << " "
       << src << "->" << dest;
    for (std::int16_t i = 0; i < argCount; ++i) {
        os << (i == 0 ? " [" : ",") << args[static_cast<size_t>(i)];
    }
    if (argCount > 0) {
        os << "]";
    }
    return os.str();
}

}  // namespace wargame
