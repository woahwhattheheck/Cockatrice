#include "interface/widgets/cards/card_art_utils.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <gtest/gtest.h>
#include <libcockatrice/card/card_info.h>
#include <libcockatrice/card/printing/exact_card.h>
#include <limits>

namespace
{
ExactCard makeCard(bool landscape)
{
    CardInfo::UiAttributes attributes;
    attributes.landscapeOrientation = landscape;
    return ExactCard(CardInfo::newInstance("Artwork", "", false, {}, {}, {}, {}, attributes));
}

QPixmap landscapeCardWithFrame()
{
    // Normalized landscape frame: the title reaches 14% down the upright
    // image, and the Battle type line starts at 56%. Artwork has two colors
    // so the test also detects a wrong or missing rotation.
    QImage upright(1400, 1000, QImage::Format_RGB32);
    upright.fill(Qt::red);
    QPainter painter(&upright);
    painter.fillRect(QRect(70, 140, 1260, 420), Qt::green);
    painter.fillRect(QRect(700, 140, 630, 420), Qt::blue);
    painter.end();
    QTransform storedOrientation;
    storedOrientation.rotate(-90);
    return QPixmap::fromImage(upright.transformed(storedOrientation));
}

TEST(CardArtCrop, CustomLandscapeCropUsesTheChosenArtworkAndRotatesUpright)
{
    const QImage crop =
        CardArtUtils::cropCardArt(landscapeCardWithFrame(), makeCard(true), QRectF(0.1, 0.2, 0.8, 0.3)).toImage();
    EXPECT_EQ(crop.size(), QSize(1120, 300));
    ASSERT_GT(crop.width(), crop.height());
    ASSERT_FALSE(crop.isNull());
    for (int y = 0; y < crop.height(); ++y) {
        for (int x = 0; x < crop.width(); ++x) {
            EXPECT_EQ(x < crop.width() / 2 ? QColor(Qt::green) : QColor(Qt::blue), crop.pixelColor(x, y));
        }
    }
}

TEST(CardArtCrop, PortraitCropRetainsItsExistingArtworkBounds)
{
    QImage source(745, 1040, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            source.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
        }
    }
    const QPixmap input = QPixmap::fromImage(source);
    const QImage crop = CardArtUtils::cropCardArt(input, makeCard(false)).toImage();
    EXPECT_EQ(source.copy(QRect(52, 114, 641, 458)), crop);
}

TEST(CardArtCrop, AutomaticLandscapeCropRetainsExistingSavedBannerFraming)
{
    const QPixmap source = landscapeCardWithFrame();
    const QPixmap upright = CardArtUtils::rotateSidewaysLayoutArt(source, makeCard(true));
    EXPECT_EQ(upright.copy(QRect(98, 50, 1204, 530)).toImage(),
              CardArtUtils::cropCardArt(source, makeCard(true)).toImage());
}

TEST(CardArtCrop, CustomBoundsCoverAnyFrameAndInvalidBoundsKeepTheAutomaticCrop)
{
    EXPECT_EQ(CardArtUtils::cardArtCropRect({1000, 800}, false, {0.4, 0.25, 0.3, 0.5}), QRect(400, 200, 300, 400));
    EXPECT_EQ(CardArtUtils::cardArtCropRect({1000, 800}, true, {0, 0, 1, 1}), QRect(0, 0, 1000, 800));
    const QRect automatic = CardArtUtils::cardArtCropRect({1000, 800}, false);
    for (const QRectF &invalid : {QRectF(-0.1, 0, 0.5, 0.5), QRectF(0.5, 0.5, 0.6, 0.5), QRectF(0, 0, 0, 1),
                                  QRectF(0, 0, std::numeric_limits<double>::infinity(), 1),
                                  QRectF(std::numeric_limits<double>::quiet_NaN(), 0, 1, 1)}) {
        EXPECT_EQ(CardArtUtils::cardArtCropRect({1000, 800}, false, invalid), automatic);
    }
    EXPECT_EQ(CardArtUtils::cardArtCropRect({1, 1}, false, {0.8, 0.8, 0.1, 0.1}), QRect(0, 0, 1, 1));
}

TEST(CardArtCrop, DevicePixelRatioDoesNotChangeTheArtworkPixels)
{
    QPixmap source = landscapeCardWithFrame();
    const QPixmap normal = CardArtUtils::cropCardArt(source, makeCard(true), {0.1, 0.2, 0.8, 0.3});
    source.setDevicePixelRatio(2.0);
    const QPixmap highDpi = CardArtUtils::cropCardArt(source, makeCard(true), {0.1, 0.2, 0.8, 0.3});
    EXPECT_EQ(normal.size(), highDpi.size());
    EXPECT_EQ(2.0, highDpi.devicePixelRatio());
    QImage normalPixels = normal.toImage();
    QImage highDpiPixels = highDpi.toImage();
    normalPixels.setDevicePixelRatio(1.0);
    highDpiPixels.setDevicePixelRatio(1.0);
    EXPECT_EQ(normalPixels, highDpiPixels);
}

TEST(CardArtCrop, EmptyAndSmallImagesStayWithinTheirBounds)
{
    EXPECT_TRUE(CardArtUtils::cropCardArt({}, makeCard(true)).isNull());
    QPixmap small(1, 1);
    small.fill(Qt::green);
    EXPECT_EQ(small.toImage(), CardArtUtils::cropCardArt(small, makeCard(true)).toImage());
    EXPECT_EQ(small.toImage(), CardArtUtils::cropCardArt(small, makeCard(false)).toImage());
}
} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
