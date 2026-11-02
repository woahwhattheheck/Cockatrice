/**
 * @file flow_widget.cpp
 * @brief Implementation of FlowWidget — a QWidget hosting a FlowLayout inside an
 *        optional QScrollArea.
 */

#include "flow_widget.h"

#include "../../../layouts/flow_layout.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLayoutItem>
#include <QList>
#include <QMessageLogger>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QWidget>
#include <libcockatrice/utility/qt_utils.h>

/**
 * @brief Constructs a FlowWidget.
 *
 * When both scroll policies are Qt::ScrollBarAlwaysOff the scroll area is
 * omitted entirely and the container is placed directly in the main layout.
 *
 * @param parent            Parent widget.
 * @param _flowDirection    Qt::Horizontal for row-wrapping, Qt::Vertical for column-wrapping.
 * @param horizontalPolicy  Horizontal scroll-bar policy.
 * @param verticalPolicy    Vertical scroll-bar policy.
 */
FlowWidget::FlowWidget(QWidget *parent,
                       const Qt::Orientation _flowDirection,
                       const Qt::ScrollBarPolicy horizontalPolicy,
                       const Qt::ScrollBarPolicy verticalPolicy)
    : QWidget(parent), scrollArea(nullptr), flowDirection(_flowDirection)

{
    // Top-level size policy
    // Horizontal flow: expand horizontally, let height be determined by wrapping.
    // Vertical flow:   expand vertically, let width be determined by wrapping.
    if (flowDirection == Qt::Horizontal) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    } else {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }

    mainLayout = QtUtils::makeLayout<QHBoxLayout>(this);
    setLayout(mainLayout);

    const bool useScrollArea = (horizontalPolicy != Qt::ScrollBarAlwaysOff || verticalPolicy != Qt::ScrollBarAlwaysOff);

    // Scroll area (optional)
    if (useScrollArea) {
        scrollArea = QtUtils::makeScrollArea(this, verticalPolicy, horizontalPolicy);
        scrollArea->setMinimumSize(0, 0);
        scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else {
        scrollArea = nullptr;
    }

    // Container widget (holds the FlowLayout)
    container = new QWidget(useScrollArea ? static_cast<QWidget *>(scrollArea) : this);

    // The container should be willing to grow in both axes; its actual size is
    // governed by the FlowLayout's sizeHint / heightForWidth, not by a fixed policy.
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    container->setMinimumSize(0, 0);

    flowLayout = new FlowLayout(container, flowDirection);
    container->setLayout(flowLayout);

    if (useScrollArea) {
        scrollArea->setWidget(container);
        mainLayout->addWidget(scrollArea);
    } else {
        mainLayout->addWidget(container);
    }
}

/**
 * @brief Adds a widget to the flow layout within the FlowWidget.
 *
 * Plain widgets are not filtered for arrow keys: intercepting them would steal
 * Up/Down/Left/Right from controls that use them (combo boxes, spin boxes
 * etc.). Widgets that want keyboard navigation between flow items must be
 * added via addNavigableWidget instead.
 *
 * @param widget_to_add The widget to add to the flow layout.
 */
void FlowWidget::addWidget(QWidget *widget_to_add)
{
    if (!widget_to_add) {
        return;
    }
    removeNavigation(widget_to_add);
    flowLayout->addWidget(widget_to_add);
}

/**
 * @brief Adds a widget and routes its arrow keys to FlowWidget focus navigation.
 *
 * The widget is filtered for arrow-key events so keyboard navigation between
 * the flow items keeps working even when the flow sits inside a QScrollArea,
 * which swallows arrow keys before they can bubble up. Only widgets added
 * through this method are affected; anything that needs its own arrow keys
 * should use plain addWidget.
 *
 * @param widget_to_add The widget to add to the flow layout.
 */
void FlowWidget::addNavigableWidget(QWidget *widget_to_add)
{
    if (!widget_to_add) {
        return;
    }
    flowLayout->addWidget(widget_to_add);
    if (!navigationConnections.contains(widget_to_add)) {
        navigationConnections.insert(widget_to_add, connect(widget_to_add, &QObject::destroyed, this,
                                                           [this, widget_to_add] {
                                                               navigationConnections.remove(widget_to_add);
                                                           }));
    }
    widget_to_add->installEventFilter(this);
}

void FlowWidget::insertWidgetAtIndex(QWidget *toInsert, int index)
{
    if (!toInsert) {
        return;
    }
    removeNavigation(toInsert);
    flowLayout->insertWidgetAtIndex(toInsert, index);
    update();
}

void FlowWidget::removeWidget(QWidget *widgetToRemove)
{
    if (!widgetToRemove) {
        return;
    }
    removeNavigation(widgetToRemove);
    flowLayout->removeWidget(widgetToRemove);
}

void FlowWidget::removeNavigation(QWidget *widget)
{
    const auto it = navigationConnections.find(widget);
    if (it == navigationConnections.end()) {
        return;
    }
    widget->removeEventFilter(this);
    disconnect(it.value());
    navigationConnections.erase(it);
}

/**
 * @brief Removes all widgets from the flow layout and deletes them.
 *
 * If the layout pointer has somehow been lost it is recreated before returning.
 */
void FlowWidget::clearLayout()
{
    for (QWidget *widget : navigationConnections.keys()) {
        removeNavigation(widget);
    }
    if (flowLayout) {
        QtUtils::clearLayoutRec(flowLayout);
    } else {
        // Defensive fallback: recreate the layout if it was deleted externally.
        flowLayout = new FlowLayout(container, flowDirection);
        container->setLayout(flowLayout);
    }
}

/**
 * @brief Marks the flow layout as dirty so Qt recomputes item positions.
 *
 * We do NOT call adjustSize() or activate() here:
 *  - adjustSize() would freeze geometry by calling setFixedSize internally.
 *  - activate() called inside a resize event can cause synchronous re-entrancy.
 * Qt automatically calls setGeometry on the layout after a resize, so simply
 * invalidating is sufficient.
 */
void FlowWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    qCDebug(FlowWidgetSizeLog) << "resizeEvent:" << event->size();

    if (flowLayout) {
        flowLayout->invalidate();
    }
}

void FlowWidget::setSpacing(int hSpacing, int vSpacing)
{
    flowLayout->setHorizontalMargin(hSpacing);
    flowLayout->setVerticalMargin(vSpacing);
    flowLayout->invalidate();
}

/**
 * @brief Sets every child widget's minimum size to the largest sizeHint in the layout.
 *
 * Useful for toolbars or button bars where all items should be the same size.
 */
void FlowWidget::setMinimumSizeToMaxSizeHint()
{
    QSize maxSize(0, 0);

    // Iterate over all widgets in the flow layout to find the maximum sizeHint
    for (int i = 0; i < flowLayout->count(); ++i) {
        QLayoutItem *item = flowLayout->itemAt(i);
        if (item && item->widget()) {
            maxSize = maxSize.expandedTo(item->widget()->sizeHint());
        }
    }

    // Set the minimum size for all widgets to the max sizeHint
    for (int i = 0; i < flowLayout->count(); ++i) {
        QLayoutItem *item = flowLayout->itemAt(i);
        if (item && item->widget()) {
            item->widget()->setMinimumSize(maxSize);
        }
    }
}

QLayoutItem *FlowWidget::itemAt(int index) const
{
    return flowLayout->itemAt(index);
}

bool FlowWidget::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || !navigationConnections.contains(widget)) {
        return QWidget::eventFilter(watched, event);
    }

    // Direct reparenting can remove an item without calling removeWidget().
    // The old flow must stop intercepting its events in its new owner.
    if (event->type() == QEvent::ParentChange && widget->parentWidget() != container) {
        removeNavigation(widget);
        return QWidget::eventFilter(watched, event);
    }

    // Only the opted-in item itself handles navigation. An editor nested inside
    // it keeps its own arrow keys, including any events that bubble to a parent.
    if (event->type() == QEvent::KeyPress && widget->hasFocus() && flowLayout->indexOf(widget) >= 0 &&
        moveFocus(static_cast<QKeyEvent *>(event), widget)) {
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

bool FlowWidget::moveFocus(QKeyEvent *event, QWidget *source)
{
    if (event->modifiers() != Qt::NoModifier) {
        return false;
    }
    const bool moveForward = event->key() == Qt::Key_Right || event->key() == Qt::Key_Down;
    const bool moveBackward = event->key() == Qt::Key_Left || event->key() == Qt::Key_Up;
    if (!moveForward && !moveBackward) {
        return false;
    }

    QList<QWidget *> focusableItems;
    for (int i = 0; i < flowLayout->count(); ++i) {
        QLayoutItem *layoutItem = flowLayout->itemAt(i);
        QWidget *item = layoutItem ? layoutItem->widget() : nullptr;
        if (item && navigationConnections.contains(item) && item->isEnabled() && item->isVisibleTo(container) &&
            (item->focusPolicy() & Qt::TabFocus)) {
            focusableItems.append(item);
        }
    }

    const int currentIndex = focusableItems.indexOf(source);
    if (currentIndex < 0 || focusableItems.size() < 2) {
        return false;
    }
    const int delta = moveForward ? 1 : -1;
    const int nextIndex = (currentIndex + delta + focusableItems.size()) % focusableItems.size();
    QPointer<QWidget> target(focusableItems.at(nextIndex));
    QPointer<FlowWidget> guard(this);
    target->setFocus(moveForward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    // Focus callbacks may destroy either widget.
    if (guard && target && scrollArea) {
        scrollArea->ensureWidgetVisible(target);
    }
    event->accept();
    return true;
}

int FlowWidget::count() const
{
    return flowLayout->count();
}
