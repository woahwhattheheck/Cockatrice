#include "settings_button_widget.h"

#include "../../pixel_map_generator.h"
#include "settings_popup_widget.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QSizePolicy>
#include <QStringLiteral>
#include <QToolButton>
#include <libcockatrice/utility/qt_utils.h>
#include <qnamespace.h>

SettingsButtonWidget::SettingsButtonWidget(QWidget *parent)
    : QWidget(parent), button(new QToolButton(this)), popup(new SettingsPopupWidget(nullptr))
{
    button->setIcon(themePixmap(QStringLiteral("icons/cogwheel")));
    button->setCheckable(true);
    button->setFixedSize(32, 32);
    connect(button, &QToolButton::clicked, this, &SettingsButtonWidget::togglePopup);
    connect(popup, &SettingsPopupWidget::aboutToClose, this, &SettingsButtonWidget::onPopupClosed);

    layout = QtUtils::makeLayout<QHBoxLayout>(this);
    layout->addWidget(button);
    setLayout(layout);
}

SettingsButtonWidget::~SettingsButtonWidget()
{
    // We don't parent the popup because it might lead to better behavior on certain window managers.
    // So we have to manually delete it
    popup->deleteLater();
}

void SettingsButtonWidget::addSettingsWidget(QWidget *toAdd) const
{
    popup->addSettingsWidget(toAdd);
}

void SettingsButtonWidget::removeSettingsWidget(QWidget *toRemove) const
{
    popup->removeSettingsWidget(toRemove);
}

void SettingsButtonWidget::setButtonIcon(QPixmap iconMap)
{
    button->setIcon(iconMap);
}

void SettingsButtonWidget::setButtonText(const QString &text)
{
    buttonText = text;

    button->setMinimumSize(QSize(0, 0));
    button->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setText(text);
    button->setFixedHeight(32);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    button->setMinimumWidth(32); // icon-only fallback minimum
    setCompact(compact);
}

void SettingsButtonWidget::setCompact(bool _compact)
{
    compact = _compact;
    if (compact) {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setFixedWidth(32);
    } else {
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setText(buttonText);
        button->setMinimumWidth(32);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
}

void SettingsButtonWidget::togglePopup()
{
    if (popup->isVisible()) {
        popup->close();
        return;
    }
    popup->adjustSizeToFitScreen(button);
    popup->show();
    popup->setFocus();
    button->setChecked(true);
}

void SettingsButtonWidget::onPopupClosed() const
{
    button->setChecked(false); // Ensure button unchecks when popup closes
}

void SettingsButtonWidget::mousePressEvent(QMouseEvent *event)
{
    if (popup->isVisible() && !popup->geometry().contains(event->globalPosition().toPoint())) {
        popup->close();
    }
    QWidget::mousePressEvent(event);
}
