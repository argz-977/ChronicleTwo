// Appended to the native copy of ps2/src/menuaqua.cpp: builds the tournament prize table, whose
// record types and statics are local to that unit, from typed prize records.
#include "record_bridges.hpp"

void PortApplyFishPrizes(const NativeFishPrizes &prizes, mgCMemory *pool) {
    fish_prize_buildstack = pool;
    unsigned int bytes = static_cast<unsigned int>(prizes.count * sizeof(fish_prize_group));
    FishTournamentGoods = static_cast<fish_prize_group *>(operator new[](bytes, fish_prize_buildstack->Alloc(static_cast<int>(align16_blocks(bytes) + 2))));
    FishTournamentGoodsNum = 0;
    if (FishTournamentGoods == nullptr) {
        return;
    }
    for (const auto &group : prizes.groups) {
        // Retail writes past the table for a group it did not allocate.
        if (FishTournamentGoodsNum >= prizes.count) {
            break;
        }
        spiFishTournamentGoods = &FishTournamentGoods[FishTournamentGoodsNum++];
        spiFishTournamentGoods->prize_count = group.prize_count;
        for (std::size_t index = 0; index < group.rates.size(); ++index) {
            spiFishTournamentGoods->rate[index] = group.rates[index];
        }
        bytes = static_cast<unsigned int>(group.prize_count * sizeof(fish_prize_record));
        spiFishTournamentGoods->prizes = static_cast<fish_prize_record *>(operator new[](bytes, fish_prize_buildstack->Alloc(static_cast<int>(align16_blocks(bytes) + 2))));
        spi_fish_prize_info = spiFishTournamentGoods->prizes;
        int written = 0;
        for (const auto &prize : group.prizes) {
            if (spi_fish_prize_info == nullptr || written >= group.prize_count) {
                break;
            }
            spi_fish_prize_info->rate = prize.rate;
            for (std::size_t rank = 0; rank < prize.ranks.size(); ++rank) {
                spi_fish_prize_info->rank[rank].item_no = prize.ranks[rank][0];
                spi_fish_prize_info->rank[rank].num = prize.ranks[rank][1];
            }
            ++spi_fish_prize_info;
            ++written;
        }
    }
}
