/**
 * @file visual_database_display_filter_save_load_widget.h
 * @ingroup VisualCardDatabaseWidgets
 */
//! \todo Document this file.

#ifndef VISUAL_DATABASE_DISPLAY_FILTER_SAVE_LOAD_WIDGET_H
#define VISUAL_DATABASE_DISPLAY_FILTER_SAVE_LOAD_WIDGET_H

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <libcockatrice/filters/filter_preset.h>
#include <optional>
#include <qtmetamacros.h>

class FilterTreeModel;
class FlowWidget;
class QLineEdit;
class QMessageBox;
class QPushButton;
class QVBoxLayout;

class VisualDatabaseDisplayFilterSaveLoadWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VisualDatabaseDisplayFilterSaveLoadWidget(QWidget *parent, FilterTreeModel *filterModel);

    void saveFilter();
    void loadFilter(const QString &filename);
    void previewFilter(const QString &filename);
    void applySearchFilter(const QString &text);
    void refreshFilterList();

public slots:
    void retranslateUi();

private:
    QPointer<FilterTreeModel> filterModel;

    QVBoxLayout *layout;
    QLineEdit *searchInput;
    FlowWidget *fileListWidget;
    QLineEdit *filenameInput;
    QPushButton *saveButton;
    QStringList allFilterFiles;

    QPointer<QMessageBox> overwriteDialog;

    void updateSaveButton();
    void writeFilter(const QString &filename, const QString &filePath, const QByteArray &data);
    std::optional<FilterPreset> readFilterPreset(const QString &filename, const QString &errorTitle);
    QString colorModeLabel(const QString &mode) const;
};

#endif // VISUAL_DATABASE_DISPLAY_FILTER_SAVE_LOAD_WIDGET_H
