#include "keymap/ProfileDiff.h"

namespace hhkbs::keymap {

std::vector<KeyChange> diffProfiles(const Keymap::Layers& before, const Keymap::Layers& after)
{
    std::vector<KeyChange> changes;
    for (std::size_t layer = 0; layer < Keymap::layerCount; ++layer) {
        for (std::size_t slot = 0; slot < Keymap::keysPerLayer; ++slot) {
            if (before[layer][slot] != after[layer][slot])
                changes.push_back({layer, slot, before[layer][slot], after[layer][slot]});
        }
    }
    return changes;
}

}  // namespace hhkbs::keymap
