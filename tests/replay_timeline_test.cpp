#include "client/settings/cache_settings.h"
#include "interface/widgets/replay/replay_manager.h"
#include "interface/widgets/replay/replay_timeline_widget.h"
#include "libcockatrice/protocol/pb/game_event_container.pb.h"
#include "libcockatrice/protocol/pb/game_replay.pb.h"
#include "libcockatrice/settings/interface_settings.h"

#include <QApplication>
#include <QImage>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <gtest/gtest.h>

namespace
{
QImage renderTimeline(ReplayTimelineWidget &widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::white);
    widget.render(&image);
    return image;
}

class CountingTimeline : public ReplayTimelineWidget
{
public:
    int paintCount = 0;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        ++paintCount;
        ReplayTimelineWidget::paintEvent(event);
    }
};
} // namespace

TEST(ReplayTimelineTest, EmptyAndZeroDurationReplaysRender)
{
    ReplayTimelineWidget widget;
    widget.resize(401, 50);
    renderTimeline(widget);
    widget.setTimeline({});
    renderTimeline(widget);
    widget.setTimeline({0});
    widget.setCurrentTime(1000);
    renderTimeline(widget);
}

TEST(ReplayTimelineTest, ReloadClearsThePreviousScaleAndDuration)
{
    ReplayTimelineWidget reused;
    ReplayTimelineWidget fresh;
    reused.resize(401, 50);
    fresh.resize(401, 50);
    reused.setTimeline({0, 100, 200, 300, 400, 5000, 10000});
    reused.setCurrentTime(9000);
    reused.setTimeline({0, 5000});
    fresh.setTimeline({0, 5000});
    EXPECT_EQ(renderTimeline(reused), renderTimeline(fresh));

    reused.setTimeline({});
    fresh.setTimeline({});
    EXPECT_EQ(renderTimeline(reused), renderTimeline(fresh));
    QSignalSpy clicked(&reused, &ReplayTimelineWidget::timeClicked);
    QTest::mouseClick(&reused, Qt::LeftButton, Qt::NoModifier, QPoint(400, 25));
    ASSERT_EQ(clicked.size(), 1);
    EXPECT_EQ(clicked.first().first().toInt(), 0);
}

TEST(ReplayTimelineTest, FinalBinContributesToTheHistogramScale)
{
    ReplayTimelineWidget widget;
    widget.resize(401, 50);
    widget.setTimeline({0, 5000, 5100, 5200, 5300});
    const auto image = renderTimeline(widget);
    // The first bin is one quarter of the final bin. Its upper half remains empty.
    EXPECT_NE(image.pixelColor(100, 20), QColor(Qt::black));
    EXPECT_EQ(image.pixelColor(100, 45), QColor(Qt::black));
    EXPECT_EQ(image.pixelColor(300, 20), QColor(Qt::black));
}

TEST(ReplayTimelineTest, ClicksReachBothEndpoints)
{
    ReplayTimelineWidget widget;
    widget.resize(401, 50);
    widget.setTimeline({0, 333, 666});
    QSignalSpy clicked(&widget, &ReplayTimelineWidget::timeClicked);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(0, 25));
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(400, 25));
    ASSERT_EQ(clicked.size(), 2);
    EXPECT_EQ(clicked[0].first().toInt(), 0);
    EXPECT_EQ(clicked[1].first().toInt(), 666);
}

TEST(ReplayTimelineTest, SubsecondProgressRepaintsWhenItsPixelChanges)
{
    CountingTimeline widget;
    widget.resize(401, 50);
    widget.setTimeline({0, 333, 666});
    widget.show();
    QApplication::processEvents();
    const int before = widget.paintCount;
    widget.setCurrentTime(333);
    QApplication::processEvents();
    EXPECT_GT(widget.paintCount, before);
    const auto image = renderTimeline(widget);
    EXPECT_GT(image.pixelColor(100, 25).green(), image.pixelColor(300, 25).green());
}

TEST(ReplayTimelineTest, EndClickAppliesTheFinalEventThroughTheManager)
{
    auto *replay = new GameReplay;
    for (const int second : {0, 1, 1}) {
        replay->add_event_list()->set_seconds_elapsed(second);
    }
    ReplayManager manager(nullptr, replay);
    ReplayTimelineWidget widget;
    widget.resize(401, 50);
    widget.setTimeline(manager.getReplayTimeline());
    QObject::connect(&widget, &ReplayTimelineWidget::timeClicked, &manager, &ReplayManager::setTime);
    QObject::connect(&manager, &ReplayManager::timeChanged, &widget, &ReplayTimelineWidget::setCurrentTime);
    int applied = 0;
    QObject::connect(&manager, &ReplayManager::eventReplayed, [&applied] { ++applied; });
    QObject::connect(&manager, &ReplayManager::rewound, [&applied] { applied = 0; });
    QSignalSpy finished(&manager, &ReplayManager::replayFinished);
    QSignalSpy position(&manager, &ReplayManager::timeChanged);

    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(400, 25));
    EXPECT_EQ(applied, 3);
    ASSERT_EQ(position.size(), 1);
    EXPECT_EQ(position.last().first().toInt(), 1500);
    EXPECT_EQ(finished.size(), 1);

    manager.setTime(999);
    EXPECT_EQ(applied, 1);
    EXPECT_EQ(position.last().first().toInt(), 800);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(400, 25));
    EXPECT_EQ(applied, 3);
    EXPECT_EQ(finished.size(), 2);
}

TEST(ReplayTimelineTest, ZeroDurationClickAppliesItsOnlyEvent)
{
    auto *replay = new GameReplay;
    replay->add_event_list()->set_seconds_elapsed(0);
    ReplayManager manager(nullptr, replay);
    ReplayTimelineWidget widget;
    widget.resize(401, 50);
    widget.setTimeline(manager.getReplayTimeline());
    QObject::connect(&widget, &ReplayTimelineWidget::timeClicked, &manager, &ReplayManager::setTime);
    int applied = 0;
    QObject::connect(&manager, &ReplayManager::eventReplayed, [&applied] { ++applied; });
    QSignalSpy finished(&manager, &ReplayManager::replayFinished);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(400, 25));
    EXPECT_EQ(applied, 1);
    EXPECT_EQ(finished.size(), 1);
}

TEST(ReplayTimelineTest, SelectsTheExactEventWhenMillisecondTimesMatch)
{
    auto *replay = new GameReplay;
    for (int i = 0; i < 2001; ++i) {
        replay->add_event_list()->set_seconds_elapsed(1);
    }
    ReplayManager manager(nullptr, replay);
    ASSERT_EQ(manager.getReplayTimeline()[0], manager.getReplayTimeline()[1]);
    int applied = 0;
    QObject::connect(&manager, &ReplayManager::eventReplayed, [&applied] { ++applied; });
    QObject::connect(&manager, &ReplayManager::rewound, [&applied] { applied = 0; });

    manager.setTimeToEvent(1);
    EXPECT_EQ(applied, 2);
    manager.setTimeToEvent(0);
    EXPECT_EQ(applied, 1);
    manager.setTimeToEvent(1);
    EXPECT_EQ(applied, 2);
}

TEST(ReplayTimelineTest, LaterSeekSupersedesBufferedRewind)
{
    auto *replay = new GameReplay;
    for (const int second : {0, 1, 2, 3}) {
        replay->add_event_list()->set_seconds_elapsed(second);
    }
    ReplayManager manager(nullptr, replay);
    int applied = 0;
    QObject::connect(&manager, &ReplayManager::eventReplayed, [&applied] { ++applied; });
    QObject::connect(&manager, &ReplayManager::rewound, [&applied] { applied = 0; });
    QSignalSpy rewound(&manager, &ReplayManager::rewound);

    manager.setTime(2400);
    ASSERT_EQ(applied, 3);
    manager.skipByAmount(-2000);
    manager.setTimeToEvent(3);
    EXPECT_EQ(applied, 4);
    QTest::qWait(SettingsCache::instance().userInterface().getRewindBufferingMs() + 50);
    EXPECT_EQ(applied, 4);
    EXPECT_EQ(rewound.size(), 0);
}

TEST(ReplayTimelineTest, PlaybackTicksPreserveBufferedRewindUntilItExpires)
{
    auto *replay = new GameReplay;
    for (const int second : {0, 1, 2, 3}) {
        replay->add_event_list()->set_seconds_elapsed(second);
    }
    ReplayManager manager(nullptr, replay);
    int applied = 0;
    QObject::connect(&manager, &ReplayManager::eventReplayed, [&applied] { ++applied; });
    QObject::connect(&manager, &ReplayManager::rewound, [&applied] { applied = 0; });
    QSignalSpy rewound(&manager, &ReplayManager::rewound);
    QSignalSpy position(&manager, &ReplayManager::timeChanged);

    manager.setTime(2400);
    manager.skipByAmount(-2000);
    ASSERT_EQ(position.last().first().toInt(), 400);
    const auto notifications = position.size();
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(QMetaObject::invokeMethod(&manager, "replayTimerTimeout", Qt::DirectConnection));
    }
    EXPECT_EQ(applied, 3);
    EXPECT_EQ(rewound.size(), 0);
    EXPECT_EQ(position.size(), notifications);

    QTest::qWait(SettingsCache::instance().userInterface().getRewindBufferingMs() + 50);
    EXPECT_EQ(rewound.size(), 1);
    EXPECT_EQ(applied, 1);
    ASSERT_TRUE(QMetaObject::invokeMethod(&manager, "replayTimerTimeout", Qt::DirectConnection));
    EXPECT_EQ(position.last().first().toInt(), 600);
    EXPECT_EQ(applied, 1);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    SettingsCache::instance().userInterface().setRewindBufferingMs(100);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
