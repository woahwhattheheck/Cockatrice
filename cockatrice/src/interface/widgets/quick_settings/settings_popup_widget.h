/**
 * @file settings_popup_widget.h
 * @ingroup Widgets
 * @ingroup Settings
 */
//! \todo Document this file.

#ifndef SETTINGS_POPUP_WIDGET_H
#define SETTINGS_POPUP_WIDGET_H

#include <QPointer>
#include <QWidget>
#include <qtmetamacros.h>

class QScrollArea;
class QVBoxLayout;

class SettingsPopupWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPopupWidget(QWidget *parent = nullptr);
    void addSettingsWidget(QWidget *toAdd) const;
    void removeSettingsWidget(QWidget *toRemove) const;
    void adjustSizeToFitScreen(QWidget *anchor = nullptr);

signals:
    void aboutToClose();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

    QVBoxLayout *layout;
    QVBoxLayout *containerLayout;
    QScrollArea *scrollArea = nullptr;
    QWidget *containerWidget;

private:
    QPointer<QWidget> anchorWidget;
    bool sizeAdjustmentPending = false;
    bool adjustingSize = false;
    void scheduleSizeAdjustment();
};

#endif // SETTINGSPOPUP_H
