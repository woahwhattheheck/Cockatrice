#ifndef DLG_MANAGE_DECK_TAGS_H
#define DLG_MANAGE_DECK_TAGS_H

#include <QDialog>
#include <QPointer>
#include <QString>

class QListWidget;
class QPushButton;
class VisualDeckStorageModel;

class DlgManageDeckTags : public QDialog
{
    Q_OBJECT

public:
    explicit DlgManageDeckTags(VisualDeckStorageModel *model, QWidget *parent = nullptr);

private slots:
    void renameTag();
    void removeTag();
    void updateActions();

private:
    void rebuildTags();
    void applyTag(const QString &oldTag, const QString &replacement);

    QPointer<VisualDeckStorageModel> model;
    const QString folderPath;
    QListWidget *tags;
    QPushButton *renameButton;
    QPushButton *removeButton;
};

#endif // DLG_MANAGE_DECK_TAGS_H
