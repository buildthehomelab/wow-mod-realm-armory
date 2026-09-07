#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <sstream>
#include <string>

namespace ArmoryEnchantments
{
    constexpr std::size_t SlotCount = 12;
    struct Slot
    {
        std::uint32_t Id = 0;
        std::uint32_t Duration = 0;
        std::uint32_t Charges = 0;
    };
    struct Result
    {
        std::array<Slot, SlotCount> Slots{};
        bool Valid = false;
    };

    // Item::SaveToDB writes ID/duration/charges for every slot, including empty slots.
    inline Result Parse(std::string const& text)
    {
        Result result;
        std::istringstream input(text);
        for (auto& slot : result.Slots)
        {
            for (auto* value : { &slot.Id, &slot.Duration, &slot.Charges })
            {
                std::string token;
                if (!(input >> token)) return {};
                auto parsed = std::from_chars(token.data(), token.data() + token.size(), *value);
                if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) return {};
            }
        }
        std::string extra;
        if (input >> extra) return {};
        result.Valid = true;
        return result;
    }
}
