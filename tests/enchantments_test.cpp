#include "ArmoryEnchantments.h"
#include <cassert>
int main() {
    std::string raw = "123 60000 2 456 30000 5 789 0 0 0 0 0 987 0 0 654 0 0 321 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 ";
    auto p = ArmoryEnchantments::Parse(raw);
    assert(p.Valid && p.Slots[0].Id == 123 && p.Slots[0].Duration == 60000);
    assert(p.Slots[1].Id == 456 && p.Slots[1].Charges == 5);
    assert(p.Slots[2].Id == 789 && p.Slots[4].Id == 987 && p.Slots[5].Id == 654);
    assert(!ArmoryEnchantments::Parse("").Valid);
    assert(!ArmoryEnchantments::Parse("123 4 5").Valid);
    assert(!ArmoryEnchantments::Parse(raw + "1").Valid);
    for (auto bad : {"-1", "4294967296", "1x", "+2"})
        assert(!ArmoryEnchantments::Parse(std::string(bad) + raw.substr(3)).Valid);
    assert(ArmoryEnchantments::Parse(" \n" + raw + "\t").Valid);
}
