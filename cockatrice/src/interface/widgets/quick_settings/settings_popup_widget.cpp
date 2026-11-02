#include "settings_popup_widget.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QMargins>
#include <QPainter>
#include <QRect>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

SettingsPopupWidget::SettingsPopupWidget(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    containerWidget = new QWidget;
    containerLayout = new QVBoxLayout(containerWidget);
    containerLayout->setSizeConstraint(QLayout::SetNoConstraint);
    containerWidget->installEventFilter(this);
    scrollArea = new QScrollArea(this);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // Install once: the scroll area owns this container for the popup lifetime.
    scrollArea->setWidget(containerWidget);
    layout->addWidget(scrollArea);
}

void SettingsPopupWidget::addSettingsWidget(QWidget *toAdd) const
{
    containerLayout->addWidget(toAdd);
}

void SettingsPopupWidget::removeSettingsWidget(QWidget *toRemove) const
{
    containerLayout->removeWidget(toRemove);
    toRemove->hide();
    toRemove->deleteLater();
}

void SettingsPopupWidget::scheduleSizeAdjustment()
{
    if (!isVisible() || sizeAdjustmentPending) {
        return;
    }
    sizeAdjustmentPending = true;
    QTimer::singleShot(0, this, [this] {
        sizeAdjustmentPending = false;
        if (isVisible()) {
            adjustSizeToFitScreen();
        }
    });
}

bool SettingsPopupWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == containerWidget && event->type() == QEvent::LayoutRequest) {
        scheduleSizeAdjustment();
    }
    return QWidget::eventFilter(watched, event);
}

void SettingsPopupWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    scheduleSizeAdjustment();
}

void SettingsPopupWidget::adjustSizeToFitScreen(QWidget *anchor)
{
    if (anchor) {
        anchorWidget = anchor;
    }
    if (adjustingSize) {
        return;
    }
    QScopedValueRollback<bool> sizingGuard(adjustingSize, true);
    const QPoint screenPoint = anchorWidget
        ? anchorWidget->mapToGlobal(anchorWidget->rect().center()) : frameGeometry().center();
    QScreen *screen = QApplication::screenAt(screenPoint);
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }
    const QRect available = screen->availableGeometry();
    if (available.isEmpty()) {
        return;
    }
    containerLayout->activate();
    const QSize minimumContentSize = containerLayout->minimumSize();
    const QSize preferredContentSize =
        containerLayout->sizeHint().expandedTo(minimumContentSize).expandedTo(QSize(1, 1));
    const QMargins margins = layout->contentsMargins();
    const int outerWidth = margins.left() + margins.right();
    const int outerHeight = margins.top() + margins.bottom();
    const int maximumWidth = available.width();
    const int maximumHeight = qMax(1, available.height() / 2);
    const int scrollBarExtent = scrollArea->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, scrollArea);
    const auto heightForContentWidth = [this, &minimumContentSize, &preferredContentSize](int width) {
        int height = preferredContentSize.height();
        if (containerLayout->hasHeightForWidth()) {
            const int wrappedHeight = containerLayout->heightForWidth(width);
            if (wrappedHeight >= 0) {
                height = wrappedHeight;
            }
        }
        return qMax(1, qMax(minimumContentSize.height(), height));
    };

    int popupWidth = 1;
    int popupHeight = 1;
    int contentHeight = 1;
    bool needsVerticalScrollBar = false;
    for (int pass = 0; pass < 3; ++pass) {
        const int verticalExtent = needsVerticalScrollBar ? scrollBarExtent : 0;
        popupWidth = qMin(maximumWidth, preferredContentSize.width() + outerWidth + verticalExtent);
        const int viewportWidth = qMax(1, popupWidth - outerWidth - verticalExtent);
        const bool needsHorizontalScrollBar = minimumContentSize.width() > viewportWidth;
        contentHeight = heightForContentWidth(qMax(viewportWidth, minimumContentSize.width()));
        const int preferredHeight = contentHeight + outerHeight + (needsHorizontalScrollBar ? scrollBarExtent : 0);
        popupHeight = qMin(maximumHeight, preferredHeight);
        const bool nextNeedsVerticalScrollBar = preferredHeight > maximumHeight;
        if (nextNeedsVerticalScrollBar == needsVerticalScrollBar) {
            break;
        }
        needsVerticalScrollBar = nextNeedsVerticalScrollBar;
    }
    // Recompute from layout hints, not the previous widget minimum, so the
    // same embedded controls survive both growth and later shrinkage.
    const QSize containerMinimum(qMax(0, minimumContentSize.width()), contentHeight);
    if (containerWidget->minimumSize() != containerMinimum) {
        containerWidget->setMinimumSize(containerMinimum);
    }
    const QSize popupSize(popupWidth, popupHeight);
    if (size() != popupSize) {
        resize(popupSize);
    }
    layout->activate();
    QPoint targetPosition = pos();
    if (anchorWidget) {
        const QPoint anchorTopLeft = anchorWidget->mapToGlobal(QPoint(0, 0));
        const int anchorRight = anchorTopLeft.x() + anchorWidget->width();
        const int anchorBottom = anchorTopLeft.y() + anchorWidget->height();
        int x = anchorTopLeft.x();
        if (x + popupWidth > available.x() + available.width()) {
            x = anchorRight - popupWidth;
        }
        int y = anchorBottom;
        if (y + popupHeight > available.y() + available.height()) {
            y = anchorTopLeft.y() - popupHeight;
        }
        targetPosition = QPoint(x, y);
    }
    targetPosition.setX(
        qBound(available.left(), targetPosition.x(), available.x() + available.width() - popupWidth));
    targetPosition.setY(
        qBound(available.top(), targetPosition.y(), available.y() + available.height() - popupHeight));
    if (pos() != targetPosition) {
        move(targetPosition);
    }
}

void SettingsPopupWidget::focusOutEvent(QFocusEvent *event)
{
    if (!isAncestorOf(QApplication::focusWidget())) {
        close();
    }
    QWidget::focusOutEvent(event);
}

void SettingsPopupWidget::closeEvent(QCloseEvent *event)
{
    emit aboutToClose();
    QWidget::closeEvent(event);
}

void SettingsPopupWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setPen(Qt::gray);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    QWidget::paintEvent(event);
}
