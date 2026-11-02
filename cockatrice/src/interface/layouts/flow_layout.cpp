/**
 * @file flow_layout.cpp
 * @brief A layout that wraps child widgets into rows or columns.
 */

#include "flow_layout.h"

#include <QLayoutItem>
#include <QMargins>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QWidget>
#include <QWidgetItem>
#include <QtGlobal>

namespace
{
QSize preferredSize(const QLayoutItem *item)
{
    return item->sizeHint().expandedTo(item->minimumSize()).boundedTo(item->maximumSize());
}

int itemHeight(const QLayoutItem *item, int width)
{
    const int heightForWidth = item->hasHeightForWidth() ? item->heightForWidth(width) : -1;
    const int height = heightForWidth >= 0 ? heightForWidth : preferredSize(item).height();
    return qBound(item->minimumSize().height(), qMax(0, height), item->maximumSize().height());
}
} // namespace

FlowLayout::FlowLayout(QWidget *parent) : FlowLayout(parent, Qt::Horizontal)
{
}

FlowLayout::FlowLayout(QWidget *parent,
                       const Qt::Orientation flowDirection,
                       const int margin,
                       const int hSpacing,
                       const int vSpacing)
    : QLayout(parent), flowDirection(flowDirection), horizontalMargin(hSpacing), verticalMargin(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    QLayoutItem *item;
    while ((item = FlowLayout::takeAt(0))) {
        delete item;
    }
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return (flowDirection == Qt::Horizontal) ? Qt::Horizontal : Qt::Vertical;
}

bool FlowLayout::hasHeightForWidth() const
{
    return flowDirection == Qt::Horizontal;
}

int FlowLayout::heightForWidth(const int width) const
{
    if (!hasHeightForWidth()) {
        return -1;
    }
    if (cachedHeightForWidth >= 0 && cachedHeightForWidthWidth == width) {
        return cachedHeightForWidth;
    }

    const QMargins margins = contentsMargins();
    const int availableWidth = qMax(0, width - margins.left() - margins.right());
    const auto rows = rowsForWidth(availableWidth);
    int height = margins.top() + margins.bottom();
    for (int i = 0; i < rows.size(); ++i) {
        if (i > 0) {
            height += verticalSpacing();
        }
        height += rowHeight(rows.at(i), rowWidths(rows.at(i), availableWidth));
    }
    cachedHeightForWidthWidth = width;
    cachedHeightForWidth = height;
    return height;
}

void FlowLayout::invalidate()
{
    cachedHeightForWidthWidth = -1;
    cachedHeightForWidth = -1;
    QLayout::invalidate();
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);

    const QMargins margins = contentsMargins();
    const int x = rect.x() + margins.left();
    const int y = rect.y() + margins.top();
    if (flowDirection == Qt::Horizontal) {
        layoutAllRows(x, y, qMax(0, rect.width() - margins.left() - margins.right()));
    } else {
        layoutAllColumns(x, y, qMax(0, rect.height() - margins.top() - margins.bottom()));
    }
}

QSize FlowLayout::sizeHint() const
{
    const QSize contentSize =
        (flowDirection == Qt::Horizontal) ? calculateSizeHintHorizontal() : calculateSizeHintVertical();
    const QMargins margins = contentsMargins();
    return contentSize + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QSize FlowLayout::minimumSize() const
{
    const QSize contentSize =
        (flowDirection == Qt::Horizontal) ? calculateMinimumSizeHorizontal() : calculateMinimumSizeVertical();
    const QMargins margins = contentsMargins();
    return contentSize + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QVector<QVector<QLayoutItem *>> FlowLayout::rowsForWidth(int availableWidth) const
{
    QVector<QVector<QLayoutItem *>> rows;
    QVector<QLayoutItem *> row;
    int usedWidth = 0;
    for (QLayoutItem *item : items) {
        if (!item || item->isEmpty()) {
            continue;
        }
        const int width = preferredSize(item).width();
        if (!row.isEmpty() && usedWidth + horizontalSpacing() + width > availableWidth) {
            rows.append(row);
            row.clear();
            usedWidth = 0;
        }
        if (!row.isEmpty()) {
            usedWidth += horizontalSpacing();
        }
        row.append(item);
        usedWidth += width;
    }
    if (!row.isEmpty()) {
        rows.append(row);
    }
    return rows;
}

QVector<int> FlowLayout::rowWidths(const QVector<QLayoutItem *> &row, int availableWidth) const
{
    QVector<int> widths;
    int usedWidth = qMax(0, static_cast<int>(row.size()) - 1) * horizontalSpacing();
    for (const QLayoutItem *item : row) {
        const int width = preferredSize(item).width();
        widths.append(width);
        usedWidth += width;
    }

    // A single wide control can shrink to its minimum and report a taller
    // height-for-width. Multiple controls wrap using the same preferred sizes
    // in both the measurement and placement passes.
    if (row.size() == 1 && usedWidth > availableWidth) {
        widths[0] = qMax(row[0]->minimumSize().width(), availableWidth);
        return widths;
    }

    int remaining = qMax(0, availableWidth - usedWidth);
    while (remaining > 0) {
        int expandingCount = 0;
        for (int i = 0; i < row.size(); ++i) {
            if (row[i]->expandingDirections().testFlag(Qt::Horizontal) &&
                widths[i] < row[i]->maximumSize().width()) {
                ++expandingCount;
            }
        }
        if (expandingCount == 0) {
            break;
        }
        const int share = qMax(1, remaining / expandingCount);
        for (int i = 0; i < row.size() && remaining > 0; ++i) {
            if (!row[i]->expandingDirections().testFlag(Qt::Horizontal)) {
                continue;
            }
            const int growth = qMin(qMin(share, remaining), row[i]->maximumSize().width() - widths[i]);
            widths[i] += growth;
            remaining -= growth;
        }
    }
    return widths;
}

int FlowLayout::rowHeight(const QVector<QLayoutItem *> &row, const QVector<int> &widths) const
{
    int height = 0;
    for (int i = 0; i < row.size(); ++i) {
        height = qMax(height, itemHeight(row[i], widths[i]));
    }
    return height;
}

int FlowLayout::layoutAllRows(const int originX, const int originY, const int availableWidth)
{
    const auto rows = rowsForWidth(availableWidth);
    int y = originY;
    for (int i = 0; i < rows.size(); ++i) {
        if (i > 0) {
            y += verticalSpacing();
        }
        const auto &row = rows.at(i);
        layoutSingleRow(row, originX, y, availableWidth);
        y += rowHeight(row, rowWidths(row, availableWidth));
    }
    return y;
}

void FlowLayout::layoutSingleRow(const QVector<QLayoutItem *> &rowItems, int x, const int y, const int availableWidth)
{
    const auto widths = rowWidths(rowItems, availableWidth);
    for (int i = 0; i < rowItems.size(); ++i) {
        QLayoutItem *item = rowItems[i];
        item->setGeometry(QRect(x, y, widths[i], itemHeight(item, widths[i])));
        x += widths[i] + horizontalSpacing();
    }
}

int FlowLayout::layoutAllColumns(const int originX, const int originY, const int availableHeight)
{
    QVector<QLayoutItem *> column;
    int usedHeight = 0;
    int x = originX;
    int columnWidth = 0;
    for (QLayoutItem *item : items) {
        if (!item || item->isEmpty()) {
            continue;
        }
        const QSize size = preferredSize(item);
        if (!column.isEmpty() && usedHeight + verticalSpacing() + size.height() > availableHeight) {
            layoutSingleColumn(column, x, originY);
            column.clear();
            x += columnWidth + horizontalSpacing();
            usedHeight = 0;
            columnWidth = 0;
        }
        if (!column.isEmpty()) {
            usedHeight += verticalSpacing();
        }
        column.append(item);
        usedHeight += size.height();
        columnWidth = qMax(columnWidth, size.width());
    }
    layoutSingleColumn(column, x, originY);
    return x + columnWidth;
}

void FlowLayout::layoutSingleColumn(const QVector<QLayoutItem *> &colItems, const int x, int y)
{
    for (QLayoutItem *item : colItems) {
        const QSize size = preferredSize(item);
        item->setGeometry(QRect(QPoint(x, y), size));
        y += size.height() + verticalSpacing();
    }
}

QSize FlowLayout::calculateSizeHintHorizontal() const
{
    int width = 0;
    int height = 0;
    bool first = true;
    for (const QLayoutItem *item : items) {
        if (!item || item->isEmpty()) {
            continue;
        }
        const QSize size = preferredSize(item);
        if (!first) {
            width += horizontalSpacing();
        }
        first = false;
        width += size.width();
        height = qMax(height, itemHeight(item, size.width()));
    }
    return QSize(width, height);
}

QSize FlowLayout::calculateMinimumSizeHorizontal() const
{
    QSize size(0, 0);
    for (const QLayoutItem *item : items) {
        if (item && !item->isEmpty()) {
            size = size.expandedTo(item->minimumSize());
        }
    }
    return size;
}

QSize FlowLayout::calculateSizeHintVertical() const
{
    int width = 0;
    int height = 0;
    bool first = true;
    for (const QLayoutItem *item : items) {
        if (!item || item->isEmpty()) {
            continue;
        }
        const QSize size = preferredSize(item);
        if (!first) {
            height += verticalSpacing();
        }
        first = false;
        height += size.height();
        width = qMax(width, size.width());
    }
    return QSize(width, height);
}

QSize FlowLayout::calculateMinimumSizeVertical() const
{
    return calculateMinimumSizeHorizontal();
}

void FlowLayout::addItem(QLayoutItem *item)
{
    if (item) {
        items.append(item);
        invalidate();
    }
}

void FlowLayout::insertWidgetAtIndex(QWidget *toInsert, int index)
{
    if (!toInsert) {
        return;
    }
    addChildWidget(toInsert);
    const int bounded = qBound(0, index, static_cast<int>(items.size()));
    items.insert(bounded, new QWidgetItem(toInsert));
    invalidate();
}

int FlowLayout::count() const
{
    return static_cast<int>(items.size());
}

QLayoutItem *FlowLayout::itemAt(const int index) const
{
    return (index >= 0 && index < items.size()) ? items.value(index) : nullptr;
}

QLayoutItem *FlowLayout::takeAt(const int index)
{
    if (index < 0 || index >= items.size()) {
        return nullptr;
    }
    QLayoutItem *item = items.takeAt(index);
    invalidate();
    return item;
}

int FlowLayout::horizontalSpacing() const
{
    return (horizontalMargin >= 0) ? horizontalMargin : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    return (verticalMargin >= 0) ? verticalMargin : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::smartSpacing(const QStyle::PixelMetric pm) const
{
    QObject *p = parent();
    if (!p) {
        return 0;
    }
    if (p->isWidgetType()) {
        const auto *pw = static_cast<QWidget *>(p);
        return qMax(0, pw->style()->pixelMetric(pm, nullptr, pw));
    }
    return qMax(0, static_cast<QLayout *>(p)->spacing());
}
