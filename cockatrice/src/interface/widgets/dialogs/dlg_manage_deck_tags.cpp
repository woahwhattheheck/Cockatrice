#include "dlg_manage_deck_tags.h"

#include "../../../client/settings/cache_settings.h"
#include "../visual_deck_storage/visual_deck_storage_model.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <libcockatrice/settings/visual_deck_storage_settings.h>

DlgManageDeckTags::DlgManageDeckTags(VisualDeckStorageModel *model, QWidget *parent)
    : QDialog(parent), model(model), folderPath(model->getDeckPath())
{
    setWindowTitle(tr("Manage deck tags"));
    auto *dialogLayout = new QVBoxLayout(this);
    auto *explanation =
        new QLabel(tr("Rename or remove a tag from every deck in this folder and its subfolders."), this);
    explanation->setWordWrap(true);
    dialogLayout->addWidget(explanation);
    tags = new QListWidget(this);
    dialogLayout->addWidget(tags);
    auto *buttons = new QHBoxLayout;
    renameButton = new QPushButton(tr("Rename"), this);
    removeButton = new QPushButton(tr("Remove"), this);
    buttons->addWidget(renameButton);
    buttons->addWidget(removeButton);
    buttons->addStretch();
    dialogLayout->addLayout(buttons);
    auto *closeButton = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeButton, &QDialogButtonBox::rejected, this, &QDialog::reject);
    dialogLayout->addWidget(closeButton);

    connect(renameButton, &QPushButton::clicked, this, &DlgManageDeckTags::renameTag);
    connect(removeButton, &QPushButton::clicked, this, &DlgManageDeckTags::removeTag);
    connect(tags, &QListWidget::currentRowChanged, this, &DlgManageDeckTags::updateActions);
    // A refreshed snapshot invalidates this catalogue even if the same folder
    // finishes loading before a nested rename/confirmation dialog returns.
    connect(model, &QAbstractItemModel::modelReset, this, &QDialog::reject);
    connect(model, &QObject::destroyed, this, &QDialog::reject);
    rebuildTags();
    resize(480, 520);
}

void DlgManageDeckTags::updateActions()
{
    const bool selected = tags->currentItem() != nullptr;
    renameButton->setEnabled(selected);
    removeButton->setEnabled(selected);
}

void DlgManageDeckTags::rebuildTags()
{
    QStringList all = SettingsCache::instance().visualDeckStorage().getVisualDeckStorageDefaultTagsList();
    for (int row = 0; row < model->rowCount(); ++row) {
        all.append(model->dataForRow(row).tags);
    }
    all.removeDuplicates();
    all.removeAll(QString());
    // Sort emoji-prefixed defaults by their readable name.
    const auto sortKey = [](const QString &tag) {
        qsizetype first = 0;
        while (first < tag.size() && !tag.at(first).isLetterOrNumber()) {
            ++first;
        }
        return tag.mid(first).toCaseFolded();
    };
    std::sort(all.begin(), all.end(), [&sortKey](const QString &a, const QString &b) {
        const int nameOrder = QString::compare(sortKey(a), sortKey(b));
        return nameOrder == 0 ? a < b : nameOrder < 0;
    });
    tags->clear();
    tags->addItems(all);
    updateActions();
}

void DlgManageDeckTags::renameTag()
{
    if (!tags->currentItem()) {
        return;
    }
    const QString oldTag = tags->currentItem()->text();
    QPointer<DlgManageDeckTags> self(this);
    bool accepted = false;
    const QString replacement =
        QInputDialog::getText(this, tr("Rename tag"), tr("New tag name:"), QLineEdit::Normal, oldTag, &accepted)
            .trimmed();
    if (!self || !isVisible() || !accepted || replacement == oldTag) {
        return;
    }
    if (replacement.isEmpty()) {
        QMessageBox::warning(this, tr("Rename tag"), tr("A tag name cannot be empty."));
        return;
    }
    applyTag(oldTag, replacement);
}

void DlgManageDeckTags::removeTag()
{
    if (!tags->currentItem()) {
        return;
    }
    const QString oldTag = tags->currentItem()->text();
    QPointer<DlgManageDeckTags> self(this);
    const auto answer = QMessageBox::question(
        this, tr("Remove tag"), tr("Remove '%1' from all decks in this folder and its subfolders?").arg(oldTag));
    if (self && isVisible() && answer == QMessageBox::Yes) {
        applyTag(oldTag, {});
    }
}

void DlgManageDeckTags::applyTag(const QString &oldTag, const QString &replacement)
{
    if (!model || model->getDeckPath() != folderPath || model->isScanning()) {
        return;
    }
    QStringList paths;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto &deck = model->dataForRow(row);
        if (!deck.loadSucceeded) {
            QMessageBox::warning(this, tr("Manage tags"),
                                 tr("A deck is still loading or could not be read. Refresh and try again."));
            return;
        }
        if (deck.tags.contains(oldTag)) {
            paths.append(deck.filePath);
        }
    }

    QStringList failures;
    int changed = 0;
    for (const QString &path : paths) {
        const int row = model->rowForFilePath(path);
        if (row < 0 || !model->dataForRow(row).loadSucceeded) {
            failures.append(path);
            continue;
        }
        QStringList updated = model->dataForRow(row).tags;
        for (QString &tag : updated) {
            if (tag == oldTag) {
                tag = replacement;
            }
        }
        updated.removeAll(QString());
        updated.removeDuplicates();
        if (model->setTags(row, updated)) {
            ++changed;
        } else {
            failures.append(path);
            model->reloadIfModified(row);
        }
    }
    if (failures.isEmpty()) {
        auto &settings = SettingsCache::instance().visualDeckStorage();
        QStringList defaults = settings.getVisualDeckStorageDefaultTagsList();
        for (QString &tag : defaults) {
            if (tag == oldTag) {
                tag = replacement;
            }
        }
        defaults.removeAll(QString());
        defaults.removeDuplicates();
        settings.setVisualDeckStorageDefaultTagsList(defaults);
    }
    rebuildTags();
    if (failures.isEmpty()) {
        QMessageBox::information(this, tr("Manage tags"), tr("Updated %n deck(s).", "", changed));
    } else {
        QMessageBox::warning(this, tr("Manage tags"),
                             tr("Updated %1 deck(s). These files were not changed; they may have been modified "
                                "elsewhere or could not be saved:\n\n%2")
                                 .arg(changed)
                                 .arg(failures.join('\n')));
    }
}
