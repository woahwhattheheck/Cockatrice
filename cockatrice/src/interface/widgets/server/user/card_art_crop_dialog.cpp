#include "card_art_crop_dialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

CardArtCropSelection::CardArtCropSelection(const QPixmap &uprightImage, QWidget *parent)
    : QWidget(parent), image(uprightImage)
{
    setMinimumSize(280, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(tr("Artwork crop selection"));
    setCursor(Qt::CrossCursor);
}

QSize CardArtCropSelection::sizeHint() const
{
    return {420, 320};
}

void CardArtCropSelection::setCrop(const QRectF &value)
{
    crop = value;
    update();
}

QRectF CardArtCropSelection::imageRect() const
{
    if (image.isNull()) {
        return {};
    }
    const QSizeF available = QSizeF(size()) - QSizeF(8, 8);
    QSizeF fitted = image.size();
    fitted.scale(available, Qt::KeepAspectRatio);
    return {QPointF((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted};
}

QPointF CardArtCropSelection::normalizedPosition(const QPointF &position) const
{
    const QRectF bounds = imageRect();
    return {qBound(0.0, (position.x() - bounds.left()) / bounds.width(), 1.0),
            qBound(0.0, (position.y() - bounds.top()) / bounds.height(), 1.0)};
}

void CardArtCropSelection::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), palette().brush(QPalette::Window));
    const QRectF bounds = imageRect();
    if (bounds.isEmpty()) {
        return;
    }
    painter.drawPixmap(bounds, image, image.rect());
    const QRectF selected(bounds.left() + crop.x() * bounds.width(), bounds.top() + crop.y() * bounds.height(),
                          crop.width() * bounds.width(), crop.height() * bounds.height());
    QPainterPath outside;
    outside.addRect(bounds);
    outside.addRect(selected);
    outside.setFillRule(Qt::OddEvenFill);
    painter.fillPath(outside, QColor(0, 0, 0, 150));
    painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(selected);
}

void CardArtCropSelection::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !imageRect().contains(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragStart = normalizedPosition(event->position());
    dragging = true;
    event->accept();
}

void CardArtCropSelection::updateSelection(const QPointF &position)
{
    const QRectF next = QRectF(dragStart, normalizedPosition(position)).normalized();
    if (next.width() * image.width() < 1 || next.height() * image.height() < 1) {
        return;
    }
    setCrop(next);
    emit cropEdited(next);
}

void CardArtCropSelection::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    updateSelection(event->position());
    event->accept();
}

void CardArtCropSelection::mouseReleaseEvent(QMouseEvent *event)
{
    if (dragging && event->button() == Qt::LeftButton) {
        updateSelection(event->position());
        dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

CardArtCropDialog::CardArtCropDialog(const QPixmap &uprightImage,
                                     const QRectF &initialCrop,
                                     const QRectF &automaticCrop,
                                     QWidget *parent)
    : QDialog(parent), selection(new CardArtCropSelection(uprightImage, this)), currentCrop(initialCrop)
{
    setWindowTitle(tr("Choose artwork crop"));
    auto *layout = new QVBoxLayout(this);
    auto *instructions = new QLabel(tr("Drag a rectangle around the artwork, or adjust the crop below."));
    instructions->setWordWrap(true);
    layout->addWidget(instructions);
    layout->addWidget(selection, 1);
    const auto makeControl = [this](bool position) {
        auto *spin = new QDoubleSpinBox(this);
        spin->setDecimals(2);
        spin->setRange(position ? 0.0 : 0.01, position ? 99.99 : 100.0);
        spin->setSuffix(tr(" %"));
        return spin;
    };
    left = makeControl(true);
    top = makeControl(true);
    width = makeControl(false);
    height = makeControl(false);
    auto *form = new QFormLayout;
    form->addRow(tr("Left:"), left);
    form->addRow(tr("Top:"), top);
    form->addRow(tr("Width:"), width);
    form->addRow(tr("Height:"), height);
    layout->addLayout(form);

    const QRectF displayed = initialCrop.isEmpty() ? automaticCrop : initialCrop;
    selection->setCrop(displayed);
    updateControls(displayed);
    connect(selection, &CardArtCropSelection::cropEdited, this, &CardArtCropDialog::applyCrop);
    for (auto *spin : {left, top, width, height}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] {
            const double x = left->value() / 100;
            const double y = top->value() / 100;
            applyCrop({x, y, qMin(width->value() / 100, 1 - x), qMin(height->value() / 100, 1 - y)});
        });
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *automatic = buttons->addButton(tr("Automatic crop"), QDialogButtonBox::ResetRole);
    auto *fullImage = buttons->addButton(tr("Full image"), QDialogButtonBox::ActionRole);
    connect(automatic, &QPushButton::clicked, this, [this] {
        currentCrop = {};
        accept();
    });
    connect(fullImage, &QPushButton::clicked, this, [this] { applyCrop({0, 0, 1, 1}); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(480, 580);
}

void CardArtCropDialog::applyCrop(const QRectF &value)
{
    currentCrop = value;
    selection->setCrop(value);
    updateControls(value);
}

void CardArtCropDialog::updateControls(const QRectF &value)
{
    const QSignalBlocker blockLeft(left), blockTop(top), blockWidth(width), blockHeight(height);
    left->setValue(value.x() * 100);
    top->setValue(value.y() * 100);
    width->setMaximum(100 - left->value());
    height->setMaximum(100 - top->value());
    width->setValue(value.width() * 100);
    height->setValue(value.height() * 100);
}

QRectF CardArtCropDialog::selectedCrop() const
{
    return currentCrop;
}
