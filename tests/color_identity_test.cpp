#include "../cockatrice/src/interface/widgets/cards/additional_info/deck_color_identity.h"
#include "../cockatrice/src/interface/widgets/visual_deck_storage/visual_deck_storage_sort_filter_proxy_model.h"
#include "../servatrice/src/color_identity.h"
#include "carddatabase/test_card_database_path_provider.h"

#include <gtest/gtest.h>
#include <libcockatrice/card/card_info.h>
#include <libcockatrice/card/database/card_database.h>
#include <libcockatrice/card/game_specific_terms.h>
#include <libcockatrice/deck_list/deck_list.h>
#include <libcockatrice/interfaces/noop_card_preference_provider.h>
#include <libcockatrice/interfaces/noop_card_set_priority_controller.h>
#include <optional>

namespace
{
class DeckColorIdentityTest : public ::testing::Test
{
protected:
    NoopCardPreferenceProvider preferences;
    TestCardDatabasePathProvider paths;
    NoopCardSetPriorityController priorities;
    CardDatabase database{nullptr, &preferences, &paths, &priorities};

    void addCard(const QString &name, const QString &colors, const std::optional<QString> &identity = {})
    {
        QHash<QString, QString> properties{{Mtg::Colors, colors}};
        if (identity) {
            properties.insert(Mtg::ColorIdentity, *identity);
        }
        database.addCard(CardInfo::newInstance(name, "", false, properties, {}, {}, {}, {}));
    }
};

TEST_F(DeckColorIdentityTest, MissingCardsDoNotEraseResolvedMainOrSideboardColors)
{
    addCard("Known white", "W");
    addCard("Known green", "G");
    addCard("Maybeboard black", "B");
    DeckList deck;
    deck.addCard("Missing main", DECK_ZONE_MAIN);
    deck.addCard("Known white", DECK_ZONE_MAIN);
    deck.addCard("Known green", DECK_ZONE_SIDE);
    deck.addCard("Missing side", DECK_ZONE_SIDE);
    deck.addCard("Maybeboard black", DECK_ZONE_MAYBEBOARD);
    EXPECT_EQ(getDeckColorIdentity(deck, database.query()), QString("WG"));
}

TEST_F(DeckColorIdentityTest, UnknownWhenNothingResolves)
{
    DeckList deck;
    EXPECT_TRUE(getDeckColorIdentity(deck, database.query()).isEmpty());
    deck.addCard("Missing card", DECK_ZONE_MAIN);
    EXPECT_TRUE(getDeckColorIdentity(deck, database.query()).isEmpty());
    EXPECT_TRUE(getDeckColorIdentity(deck, nullptr).isEmpty());
}

TEST_F(DeckColorIdentityTest, ResolvedColorlessRemainsDistinctFromUnknown)
{
    addCard("Colorless card", "");
    DeckList deck;
    deck.addCard("Missing card", DECK_ZONE_MAIN);
    deck.addCard("Colorless card", DECK_ZONE_SIDE);
    EXPECT_EQ(getDeckColorIdentity(deck, database.query()), QString("C"));
}

TEST_F(DeckColorIdentityTest, ExplicitIdentityOverridesColorsWithLegacyFallback)
{
    addCard("Devoid card", "", QString("R"));
    addCard("Colored activation", "", QString("u"));
    addCard("Legacy card", "G");
    addCard("Explicitly colorless", "W", QString(""));
    DeckList deck;
    for (const auto &name : {"Devoid card", "Colored activation", "Legacy card", "Explicitly colorless"}) {
        deck.addCard(name, DECK_ZONE_MAIN);
    }
    EXPECT_EQ(getDeckColorIdentity(deck, database.query()), QString("URG"));
}

TEST_F(DeckColorIdentityTest, MalformedIdentityDoesNotBecomeColorless)
{
    addCard("Malformed identity", "G", QString("CG"));
    DeckList deck;
    deck.addCard("Malformed identity", DECK_ZONE_MAIN);
    EXPECT_TRUE(getDeckColorIdentity(deck, database.query()).isEmpty());
}

TEST(ColorIdentityFilter, DistinguishesUnknownColorlessAndMalformedInput)
{
    using Filter = VisualDeckStorageSortFilterProxyModel;
    for (const auto mode : {Filter::ExactMatch, Filter::Includes, Filter::Excludes}) {
        EXPECT_TRUE(colorIdentityMatches(mode, {}, ""));
        EXPECT_FALSE(colorIdentityMatches(mode, {'C'}, ""));
        EXPECT_FALSE(colorIdentityMatches(mode, {'W'}, "CW"));
        EXPECT_FALSE(colorIdentityMatches(mode, {'W'}, "X"));
    }
    EXPECT_TRUE(colorIdentityMatches(Filter::ExactMatch, {'C'}, " c "));
    EXPECT_TRUE(colorIdentityMatches(Filter::Includes, {'C'}, "C"));
    EXPECT_FALSE(colorIdentityMatches(Filter::Excludes, {'C'}, "C"));
    EXPECT_TRUE(colorIdentityMatches(Filter::Excludes, {'W'}, "C"));
    EXPECT_FALSE(colorIdentityMatches(Filter::ExactMatch, {'C', 'W'}, "C"));
}

TEST(ColorIdentityFilter, AppliesExactIncludesAndExcludesToColoredIdentities)
{
    using Filter = VisualDeckStorageSortFilterProxyModel;
    EXPECT_TRUE(colorIdentityMatches(Filter::ExactMatch, {'W', 'U'}, " uww "));
    EXPECT_FALSE(colorIdentityMatches(Filter::ExactMatch, {'W'}, "WU"));
    EXPECT_TRUE(colorIdentityMatches(Filter::Includes, {'w'}, "WU"));
    EXPECT_FALSE(colorIdentityMatches(Filter::Includes, {'W', 'G'}, "WU"));
    EXPECT_TRUE(colorIdentityMatches(Filter::Excludes, {'B', 'R'}, "WU"));
    EXPECT_FALSE(colorIdentityMatches(Filter::Excludes, {'B', 'U'}, "WU"));
}

TEST(ServerColorIdentity, NormalizesColorsAndRejectsMixedColorlessOrMalformedInput)
{
    EXPECT_EQ(sanitizeColorIdentity(" grwuub "), QString("WUBRG"));
    EXPECT_EQ(sanitizeColorIdentity(" c "), QString("C"));
    for (const auto &input : {"", "  ", "CW", "CC", "X", "W U", "{W}", "2"}) {
        EXPECT_TRUE(sanitizeColorIdentity(input).isEmpty()) << input;
    }
}
} // namespace
