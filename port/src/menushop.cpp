// clang-format off
// nlohmann must precede the game headers, whose macros it would otherwise see.
#include "platform/native_config.hpp"
// clang-format on

#include "common.h"

#include <algorithm>
#include <vector>

#include "menushop.hpp"
#include "stubs/menushop.hpp"
#include "userdata.hpp"

namespace {

/**
 * Goods one shop offers.
 */
struct ShopEntry {
    int              id;    /**< Shop number. */
    std::vector<int> items; /**< Item number of each of the goods. */
};

/**
 * Buying and selling price of one item.
 */
struct PriceEntry {
    int item; /**< Item number. */
    int buy;  /**< Price to buy one. */
    int sell; /**< Price paid for one. */
};

/**
 * Typed contents of a shop_lists document.
 */
struct ShopLists {
    std::vector<ShopEntry>  shops;  /**< Goods of every shop, in script order. */
    std::vector<PriceEntry> prices; /**< Prices, in script order. */
};

/**
 * Parses a shop_lists document.
 */
ShopLists ParseShopLists(const native_config::Json &document) {
    using namespace native_config;
    ShopLists lists;
    for (const auto &shop : Array(document, "shops")) {
        lists.shops.push_back({.id = Int(shop, "id"), .items = IntList(shop, "items")});
    }
    for (const auto &price : Array(document, "prices")) {
        lists.prices.push_back({.item = Int(price, "item"), .buy = Int(price, "buy"), .sell = Int(price, "sell")});
    }
    return lists;
}

/**
 * Takes the goods of the shop being opened from its entry, choosing how they are paid for.
 */
void ApplyShop(const ShopEntry &shop) {
    if (shop.id != Now_Shop_ID) {
        return;
    }
    Now_ShopListNum = static_cast<short>(shop.items.size());
    GetUserDataMan();
    switch (Now_Shop_ID) {
        case 0x17:
        case 0x1C:
            NowSellMode = SHOP_SELL_MODE_ROBO_ABS;
            break;
        case 0x20:
            NowSellMode = SHOP_SELL_MODE_MEDAL;
            break;
        case 0x21:
            NowSellMode = SHOP_SELL_MODE_DONY;
            break;
        default:
            break;
    }
    // Retail writes past the goods table for an over-long entry; the port keeps the first SHOP_ITEM_MAX.
    for (std::size_t index = 0; index < shop.items.size() && index < SHOP_ITEM_MAX; ++index) {
        Now_ShopDataReadPtr[index] = shop.items[index];
    }
    Now_ShopListNum = std::min<int>(Now_ShopListNum, SHOP_ITEM_MAX);
}

} // namespace

PORT_OVERRIDE void CShop::AnalyzeShopList(char *script, int /*length*/) {
    Now_Shop_ID = shop_id;
    Now_ShopDataReadPtr = item_no;
    Spi_PriceList = price;
    NowSellMode = 0;
    if (const auto lists = native_config::ReadBuffer<ShopLists>(script, "shop_lists", ParseShopLists)) {
        for (const auto &shop : lists->shops) {
            ApplyShop(shop);
        }
        for (const auto &entry : lists->prices) {
            if (entry.item >= 0 && entry.item < SHOP_PRICE_MAX) {
                price[entry.item].buy = entry.buy;
                price[entry.item].sell = entry.sell;
            }
        }
    }
    item_num = Now_ShopListNum;
    CheckEventItem();
    CheckSyojiHin();
}
