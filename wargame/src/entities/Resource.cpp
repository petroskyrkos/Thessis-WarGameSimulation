#include "entities/Resource.h"

namespace wargame {

const char* resourceName(Resource r) {
    switch (r) {
        case Resource::Munitions: return "munitions";
        case Resource::Fuel: return "fuel";
        case Resource::Supplies: return "supplies";
        case Resource::Count: break;
    }
    return "?";
}

}  // namespace wargame
