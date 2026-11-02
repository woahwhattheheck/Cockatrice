#include "interface/layouts/flow_layout.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QWidget>
#include <gtest/gtest.h>

namespace
{
class SizedWidget : public QWidget
{
public:
    SizedWidget(QSize preferred, QSize minimum, bool expanding = false, bool wrapping = false)
        : preferred(preferred), wrapping(wrapping)
    {
        setMinimumSize(minimum);
        QSizePolicy policy(expanding ? QSizePolicy::Expanding : QSizePolicy::Preferred, QSizePolicy::Preferred);
        policy.setHeightForWidth(wrapping);
        setSizePolicy(policy);
    }

    QSize sizeHint() const override
    {
        return preferred;
    }

    int heightForWidth(int width) const override
    {
        return wrapping ? (width < 100 ? 60 : 20) : -1;
    }

private:
    QSize preferred;
    bool wrapping;
};

TEST(FlowLayoutTest, MeasurementMatchesPlacementAtEveryWrapBoundary)
{
    QWidget host;
    auto *layout = new FlowLayout(&host, Qt::Horizontal, 0, 7, 9);
    layout->setContentsMargins(3, 5, 11, 13);
    auto *first = new SizedWidget(QSize(80, 20), QSize(30, 20));
    auto *second = new SizedWidget(QSize(120, 30), QSize(40, 30));
    auto *third = new SizedWidget(QSize(60, 25), QSize(25, 25));
    layout->addWidget(first);
    layout->addWidget(second);
    layout->addWidget(third);

    for (int width = 54; width <= 310; ++width) {
        const int height = layout->heightForWidth(width);
        layout->setGeometry(QRect(17, 23, width, height));
        const int bottom = qMax(qMax(first->geometry().bottom(), second->geometry().bottom()),
                               third->geometry().bottom());
        EXPECT_EQ(bottom + 1 + 13, 23 + height) << "width=" << width;
        EXPECT_GE(first->x(), 17 + 3);
        EXPECT_GE(first->y(), 23 + 5);
    }
    EXPECT_EQ(layout->sizeHint(), QSize(288, 48));
}

TEST(FlowLayoutTest, ExpandingControlsKeepPreferredWidthsAndRedistributeCappedSurplus)
{
    QWidget host;
    auto *layout = new FlowLayout(&host, Qt::Horizontal, 0, 10, 0);
    auto *small = new SizedWidget(QSize(60, 20), QSize(20, 20), true);
    auto *large = new SizedWidget(QSize(160, 20), QSize(100, 20), true);
    small->setMaximumWidth(80);
    layout->addWidget(small);
    layout->addWidget(large);
    layout->setGeometry(QRect(0, 0, 330, layout->heightForWidth(330)));

    EXPECT_EQ(small->width(), 80);
    EXPECT_EQ(large->width(), 240);
    EXPECT_EQ(large->x(), 90);
    EXPECT_EQ(large->geometry().right(), 329);
}

TEST(FlowLayoutTest, WrappedChildHeightAndHiddenItemsAffectBothPasses)
{
    QWidget host;
    auto *layout = new FlowLayout(&host, Qt::Horizontal, 4, 5, 8);
    auto *label = new SizedWidget(QSize(140, 20), QSize(20, 20), false, true);
    auto *hidden = new SizedWidget(QSize(200, 200), QSize(200, 200));
    layout->addWidget(label);
    layout->addWidget(hidden);
    hidden->hide();

    EXPECT_EQ(layout->heightForWidth(88), 68);
    layout->setGeometry(QRect(0, 0, 88, 68));
    EXPECT_EQ(label->geometry(), QRect(4, 4, 80, 60));
    EXPECT_EQ(layout->heightForWidth(148), 28);
}

TEST(FlowLayoutTest, NestedLayoutItemsReceiveTheirGeometry)
{
    QWidget host;
    auto *layout = new FlowLayout(&host, Qt::Horizontal, 6, 4, 4);
    auto *nested = new QHBoxLayout;
    nested->setParent(layout);
    nested->setContentsMargins(0, 0, 0, 0);
    auto *child = new SizedWidget(QSize(90, 25), QSize(30, 25));
    child->setParent(&host);
    nested->addWidget(child);
    layout->addItem(nested);
    layout->setGeometry(QRect(0, 0, 200, layout->heightForWidth(200)));
    EXPECT_EQ(nested->geometry(), QRect(6, 6, 90, 25));
    EXPECT_EQ(child->geometry(), QRect(6, 6, 90, 25));
}
} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
