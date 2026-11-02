#include "visual_database_display_filter_save_load_widget.h"

#include "../../../client/settings/cache_settings.h"
#include "../general/layout_containers/flow_widget.h"
#include "filter_tree_model.h"
#include "visual_database_filter_display_widget.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QByteArray>
#include <QChar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIdentityProxyModel>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>
#include <libcockatrice/filters/filter_preset.h>
#include <libcockatrice/settings/paths_settings.h>
#include <qnamespace.h>

namespace
{
bool isFilterFilename(const QString &filename)
{
    return !filename.isEmpty() && filename != "." && filename != ".." && !filename.contains('/') &&
           !filename.contains('\\');
}

void showFilterError(QWidget *parent, const QString &title, const QString &message)
{
    // A parent can disappear while an error is displayed. A heap-owned, async
    // message box does not leave a stack dialog or a suspended widget method.
    auto *dialog = new QMessageBox(QMessageBox::Warning, title, message, QMessageBox::Ok, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

// Own the preset on this stack: reset observers can destroy the originating
// pane or preview dialog synchronously.
void applyFilterPreset(QPointer<FilterTreeModel> model, FilterPreset preset)
{
    if (!model) {
        return;
    }
    model->replaceSnapshot(preset.snapshot);
    if (model) {
        emit model->filtersLoaded(preset.colorMode);
    }
}

class ReadOnlyFilterPreviewModel final : public QIdentityProxyModel
{
public:
    explicit ReadOnlyFilterPreviewModel(QObject *parent) : QIdentityProxyModel(parent)
    {
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return QIdentityProxyModel::flags(index) &
               ~(Qt::ItemIsEditable | Qt::ItemIsUserCheckable |
                 Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    }

    bool setData(const QModelIndex &, const QVariant &, int = Qt::EditRole) override
    {
        return false;
    }
};

struct FilterPreviewCounts
{
    int total = 0;
    int active = 0;
};

FilterPreviewCounts countPreviewCriteria(const QAbstractItemModel *model,
                                        const QModelIndex &index,
                                        bool ancestorsEnabled)
{
    FilterPreviewCounts counts;
    const bool enabled =
        ancestorsEnabled && model->data(index, Qt::CheckStateRole).toInt() == Qt::Checked;
    const int rows = model->rowCount(index);
    if (rows == 0) {
        // Attribute -> operator -> criterion; exclude empty structural groups.
        if (index.parent().isValid() && index.parent().parent().isValid()) {
            counts.total = 1;
            counts.active = enabled ? 1 : 0;
        }
        return counts;
    }
    for (int row = 0; row < rows; ++row) {
        const FilterPreviewCounts child =
            countPreviewCriteria(model, model->index(row, 0, index), enabled);
        counts.total += child.total;
        counts.active += child.active;
    }
    return counts;
}

} // namespace

VisualDatabaseDisplayFilterSaveLoadWidget::VisualDatabaseDisplayFilterSaveLoadWidget(QWidget *parent,
                                                                                     FilterTreeModel *_filterModel)
    : QWidget(parent), filterModel(_filterModel)
{
    setMinimumWidth(300);
    setMaximumHeight(300);

    layout = new QVBoxLayout(this);
    setLayout(layout);

    // Filter search input
    searchInput = new QLineEdit(this);
    searchInput->setClearButtonEnabled(true);
    layout->addWidget(searchInput);

    connect(searchInput, &QLineEdit::textChanged, this, &VisualDatabaseDisplayFilterSaveLoadWidget::applySearchFilter);

    // File list container
    fileListWidget = new FlowWidget(this, Qt::Horizontal, Qt::ScrollBarAsNeeded, Qt::ScrollBarAsNeeded);
    layout->addWidget(fileListWidget);

    // Input for filter filename
    filenameInput = new QLineEdit(this);
    layout->addWidget(filenameInput);

    // Save button
    saveButton = new QPushButton(this);
    layout->addWidget(saveButton);
    // Disable save if empty
    saveButton->setEnabled(false);
    connect(filenameInput, &QLineEdit::textChanged, this,
            [this] { updateSaveButton(); });

    connect(saveButton, &QPushButton::clicked, this, &VisualDatabaseDisplayFilterSaveLoadWidget::saveFilter);

    if (filterModel) {
        connect(filterModel.data(), &QObject::destroyed, this, [this] { updateSaveButton(); });
    }
    refreshFilterList(); // Populate the file list on startup
    retranslateUi();
}

void VisualDatabaseDisplayFilterSaveLoadWidget::retranslateUi()
{
    searchInput->setPlaceholderText(tr("Search filter..."));
    searchInput->setAccessibleName(tr("Search saved filters"));
    saveButton->setText(tr("Save Filter"));
    saveButton->setToolTip(tr("Save all currently applied filters to a file"));
    filenameInput->setPlaceholderText(tr("Enter filename..."));
    filenameInput->setAccessibleName(tr("Saved filter filename"));
    for (auto *filterWidget : fileListWidget->findChildren<FilterDisplayWidget *>()) {
        filterWidget->retranslateUi();
    }
}

void VisualDatabaseDisplayFilterSaveLoadWidget::updateSaveButton()
{
    saveButton->setEnabled(filterModel && !overwriteDialog && !filenameInput->text().trimmed().isEmpty());
}

void VisualDatabaseDisplayFilterSaveLoadWidget::saveFilter()
{
    if (overwriteDialog) {
        overwriteDialog->raise();
        overwriteDialog->activateWindow();
        return;
    }

    const QString filename = filenameInput->text().trimmed();
    if (filename.isEmpty()) {
        return;
    }
    if (!isFilterFilename(filename)) {
        showFilterError(this, tr("Save Failed"), tr("Enter a filename without folder separators."));
        return;
    }
    if (!filterModel) {
        showFilterError(this, tr("Save Failed"), tr("The filter view is no longer available."));
        return;
    }

    const QString filePath =
        SettingsCache::instance().paths().getFiltersPath() + QDir::separator() + filename + ".json";
    const FilterPreset preset{filterModel->snapshot(), filterModel->property("visualDatabaseColorMode").toString()};
    const QByteArray data = QJsonDocument(preset.toJson()).toJson(QJsonDocument::Indented);
    if (!QFileInfo::exists(filePath)) {
        writeFilter(filename, filePath, data);
        return;
    }

    auto *dialog = new QMessageBox(
        QMessageBox::Question, tr("Replace Saved Filter"),
        tr("A saved filter named '%1' already exists. Replace it with the current filters?").arg(filename),
        QMessageBox::Save | QMessageBox::Cancel, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->button(QMessageBox::Save)->setText(tr("Replace"));
    dialog->setDefaultButton(QMessageBox::Cancel);
    dialog->setEscapeButton(QMessageBox::Cancel);
    overwriteDialog = dialog;
    updateSaveButton();

    const QPointer<VisualDatabaseDisplayFilterSaveLoadWidget> owner(this);
    connect(dialog, &QMessageBox::finished, this, [owner, filename, filePath, data](int result) {
        if (!owner) {
            return;
        }
        // Copy connection-owned values before an observer can destroy its
        // originating dialog or pane during the subsequent UI update.
        const QPointer<VisualDatabaseDisplayFilterSaveLoadWidget> targetOwner = owner;
        const QString targetFilename = filename;
        const QString targetPath = filePath;
        const QByteArray targetData = data;
        targetOwner->overwriteDialog = nullptr;
        targetOwner->updateSaveButton();
        if (targetOwner && result == QMessageBox::Save) {
            targetOwner->writeFilter(targetFilename, targetPath, targetData);
        }
    });
    connect(dialog, &QObject::destroyed, this, [owner] {
        if (owner) {
            owner->updateSaveButton();
        }
    });
    dialog->open();
}

void VisualDatabaseDisplayFilterSaveLoadWidget::writeFilter(const QString &filename,
                                                          const QString &filePath,
                                                          const QByteArray &data)
{
    QSaveFile file(filePath);
    // Preserve the explicit flush check: Qt 6.4 can otherwise report a
    // successful commit after a short write while closing the device.
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
        !file.flush() || !file.commit()) {
        showFilterError(this, tr("Save Failed"),
                        tr("Could not save filter '%1': %2").arg(filename, file.errorString()));
        return;
    }

    const QPointer<VisualDatabaseDisplayFilterSaveLoadWidget> owner(this);
    // Do not erase a different filename entered during confirmation.
    if (filenameInput->text().trimmed() == filename) {
        filenameInput->clear();
    }
    if (owner) {
        owner->refreshFilterList();
    }
}

std::optional<FilterPreset> VisualDatabaseDisplayFilterSaveLoadWidget::readFilterPreset(
    const QString &filename, const QString &errorTitle)
{
    if (!isFilterFilename(filename)) {
        showFilterError(this, errorTitle, tr("Choose a filter from the saved filter list."));
        return std::nullopt;
    }
    QString filePath = SettingsCache::instance().paths().getFiltersPath() + QDir::separator() + filename;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        showFilterError(this, errorTitle,
                        tr("Could not read filter '%1': %2").arg(filename, file.errorString()));
        return std::nullopt;
    }

    QByteArray jsonData = file.readAll();
    if (file.error() != QFile::NoError) {
        showFilterError(this, errorTitle,
                        tr("Could not read filter '%1': %2").arg(filename, file.errorString()));
        return std::nullopt;
    }
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        showFilterError(this, errorTitle,
                        tr("Filter '%1' is not a valid filter file. The current filters were kept.").arg(filename));
        return std::nullopt;
    }

    const auto preset = FilterPreset::fromJson(doc.object());
    if (!preset) {
        showFilterError(this, errorTitle,
                        tr("Filter '%1' contains an unsupported filter. The current filters were kept.").arg(filename));
        return std::nullopt;
    }
    return preset;
}

void VisualDatabaseDisplayFilterSaveLoadWidget::loadFilter(const QString &filename)
{
    const auto preset = readFilterPreset(filename, tr("Load Failed"));
    if (!preset) {
        return;
    }
    if (!filterModel) {
        showFilterError(this, tr("Load Failed"), tr("The filter view is no longer available."));
        return;
    }
    applyFilterPreset(filterModel, *preset);
}

void VisualDatabaseDisplayFilterSaveLoadWidget::applySearchFilter(const QString &text)
{
    fileListWidget->clearLayout();

    QString filter = text.trimmed();
    QStringList filtered = allFilterFiles;

    if (!filter.isEmpty()) {
        filtered = filtered.filter(filter, Qt::CaseInsensitive);
    }

    for (const QString &filename : filtered) {
        FilterDisplayWidget *filterWidget = new FilterDisplayWidget(this, filename, filterModel.data());
        fileListWidget->addWidget(filterWidget);

        connect(filterWidget, &FilterDisplayWidget::filterLoadRequested, this,
                &VisualDatabaseDisplayFilterSaveLoadWidget::loadFilter);

        connect(filterWidget, &FilterDisplayWidget::filterPreviewRequested, this,
                &VisualDatabaseDisplayFilterSaveLoadWidget::previewFilter);
        connect(filterWidget, &FilterDisplayWidget::filterDeleted, this,
                &VisualDatabaseDisplayFilterSaveLoadWidget::refreshFilterList);
    }
}

void VisualDatabaseDisplayFilterSaveLoadWidget::refreshFilterList()
{
    QDir dir(SettingsCache::instance().paths().getFiltersPath());
    allFilterFiles = dir.entryList({"*.json"}, QDir::Files, QDir::Name);

    applySearchFilter(searchInput->text());
}

QString VisualDatabaseDisplayFilterSaveLoadWidget::colorModeLabel(const QString &mode) const
{
    if (mode.isEmpty()) {
        return tr("Inferred from saved filters");
    }
    if (mode == QStringLiteral("exact")) {
        return tr("Exact match");
    }
    if (mode == QStringLiteral("includes")) {
        return tr("Includes");
    }
    if (mode == QStringLiteral("excludes")) {
        return tr("Excludes");
    }
    if (mode == QStringLiteral("include_exclude")) {
        return tr("Include / Exclude");
    }
    if (mode == QStringLiteral("identity")) {
        return tr("Color identity");
    }
    return mode;
}

void VisualDatabaseDisplayFilterSaveLoadWidget::previewFilter(const QString &filename)
{
    const auto preset = readFilterPreset(filename, tr("Preview Failed"));
    if (!preset) {
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Preview filter: %1").arg(filename));
    dialog->resize(560, 420);
    auto *dialogLayout = new QVBoxLayout(dialog);

    auto *explanation = new QLabel(
        tr("Preview only. Choose Apply to replace the current filters with this saved preset."), dialog);
    explanation->setWordWrap(true);
    dialogLayout->addWidget(explanation);

    auto *colorMode = new QLabel(tr("Color matching: %1").arg(colorModeLabel(preset->colorMode)), dialog);
    colorMode->setWordWrap(true);
    dialogLayout->addWidget(colorMode);

    auto *previewModel = new FilterTreeModel(dialog);
    previewModel->replaceSnapshot(preset->snapshot);
    auto *readOnlyModel = new ReadOnlyFilterPreviewModel(dialog);
    readOnlyModel->setSourceModel(previewModel);

    auto *tree = new QTreeView(dialog);
    tree->setModel(readOnlyModel);
    tree->setHeaderHidden(true);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree->setAccessibleName(tr("Saved filter criteria"));
    tree->expandAll();
    tree->resizeColumnToContents(0);
    dialogLayout->addWidget(tree);

    FilterPreviewCounts counts;
    for (int row = 0; row < previewModel->rowCount(); ++row) {
        const FilterPreviewCounts branch =
            countPreviewCriteria(previewModel, previewModel->index(row, 0), true);
        counts.total += branch.total;
        counts.active += branch.active;
    }
    dialogLayout->addWidget(
        new QLabel(tr("Active criteria: %1 / %2").arg(counts.active).arg(counts.total), dialog));
    if (counts.total == 0) {
        tree->hide();
        explanation->setText(tr("This preset has no criteria. Applying it clears the current criteria."));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, dialog);
    dialogLayout->addWidget(buttons);
    auto *applyButton = buttons->button(QDialogButtonBox::Apply);
    applyButton->setToolTip(tr("Apply the displayed criteria and saved color matching settings"));
    buttons->button(QDialogButtonBox::Close)->setDefault(true);

    const QPointer<FilterTreeModel> model(filterModel);
    const QPointer<QDialog> previewDialog(dialog);
    const FilterPreset previewedPreset = *preset;
    applyButton->setEnabled(bool(model));
    if (model) {
        connect(model.data(), &QObject::destroyed, dialog, [applyButton] {
            applyButton->setEnabled(false);
        });
    }
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(applyButton, &QPushButton::clicked, dialog, [model, previewDialog, previewedPreset] {
        if (!model) {
            return;
        }
        // Own both values before accept() can destroy the dialog and its
        // connection through a synchronous observer.
        const QPointer<FilterTreeModel> targetModel = model;
        const FilterPreset acceptedPreset = previewedPreset;
        if (previewDialog) {
            previewDialog->accept();
        }
        applyFilterPreset(targetModel, acceptedPreset);
    });

    tree->setFocus();
    dialog->show();
}
