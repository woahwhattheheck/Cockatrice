#include "visual_database_display_filter_toolbar_widget.h"

#include "../../../filters/filter_tree_model.h"
#include "../../pixel_map_generator.h"
#include "../deck_editor/card_database_view.h"
#include "../quick_settings/settings_button_widget.h"
#include "card_database_display_model.h"
#include "visual_database_display_filter_save_load_widget.h"
#include "visual_database_display_format_legality_filter_widget.h"
#include "visual_database_display_main_type_filter_widget.h"
#include "visual_database_display_name_filter_widget.h"
#include "visual_database_display_set_filter_widget.h"
#include "visual_database_display_sub_type_filter_widget.h"
#include "visual_database_display_tag_filter_widget.h"
#include "visual_database_display_widget.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QPixmap>
#include <QResizeEvent>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QStringLiteral>
#include <QVariant>
#include <QWidget>
#include <QtGlobal>
#include <libcockatrice/utility/qt_utils.h>
#include <qnamespace.h>

class DeckListModel;

namespace
{
bool hasCurrentFilterCriteria(const QAbstractItemModel *model, const QModelIndex &index)
{
    const int rows = model->rowCount(index);
    if (rows == 0) {
        // Empty attribute and operator groups are not criteria.
        return index.parent().isValid() && index.parent().parent().isValid();
    }
    for (int row = 0; row < rows; ++row) {
        if (hasCurrentFilterCriteria(model, model->index(row, 0, index))) {
            return true;
        }
    }
    return false;
}

class CurrentFilterSearchProxyModel final : public QSortFilterProxyModel
{
public:
    explicit CurrentFilterSearchProxyModel(QObject *parent) : QSortFilterProxyModel(parent)
    {
        setFilterRole(Qt::DisplayRole);
        setDynamicSortFilter(true);
        setRecursiveFilteringEnabled(true);
    }

    void setSearchText(const QString &text)
    {
        const QString nextText = text.trimmed();
        if (searchText != nextText) {
            searchText = nextText;
            invalidateFilter();
        }
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        const QAbstractItemModel *model = sourceModel();
        if (!model) {
            return false;
        }
        const QModelIndex candidate = model->index(sourceRow, 0, sourceParent);
        if (!hasCurrentFilterCriteria(model, candidate)) {
            return false;
        }
        if (searchText.isEmpty()) {
            return true;
        }
        // Matching groups expose their criteria; matching criteria retain
        // the ancestor checkboxes that determine their effective state.
        for (QModelIndex ancestor = candidate; ancestor.isValid(); ancestor = ancestor.parent()) {
            if (labelMatches(ancestor)) {
                return true;
            }
        }
        return descendantMatches(candidate);
    }

private:
    QString searchText;

    bool labelMatches(const QModelIndex &index) const
    {
        return sourceModel()->data(index, Qt::DisplayRole).toString().contains(searchText, Qt::CaseInsensitive);
    }

    bool descendantMatches(const QModelIndex &parent) const
    {
        const QAbstractItemModel *model = sourceModel();
        for (int row = 0; row < model->rowCount(parent); ++row) {
            const QModelIndex child = model->index(row, 0, parent);
            if (hasCurrentFilterCriteria(model, child) && (labelMatches(child) || descendantMatches(child))) {
                return true;
            }
        }
        return false;
    }
};

struct CurrentFilterCriterionCounts
{
    int total = 0;
    int active = 0;
};

CurrentFilterCriterionCounts countCurrentFilterCriteria(const QAbstractItemModel *model,
                                                       const QModelIndex &index,
                                                       bool ancestorsEnabled)
{
    CurrentFilterCriterionCounts counts;
    const bool enabled = ancestorsEnabled && model->data(index, Qt::CheckStateRole).toInt() == Qt::Checked;
    const int rows = model->rowCount(index);
    if (rows == 0) {
        if (index.parent().isValid() && index.parent().parent().isValid()) {
            counts.total = 1;
            counts.active = enabled ? 1 : 0;
        }
        return counts;
    }
    for (int row = 0; row < rows; ++row) {
        const auto child = countCurrentFilterCriteria(model, model->index(row, 0, index), enabled);
        counts.total += child.total;
        counts.active += child.active;
    }
    return counts;
}
} // namespace

VisualDatabaseDisplayFilterToolbarWidget::VisualDatabaseDisplayFilterToolbarWidget(VisualDatabaseDisplayWidget *_parent,
                                                                                   DeckListModel *deckListModel)
    : FlowWidget(_parent, Qt::Horizontal, Qt::ScrollBarAlwaysOff, Qt::ScrollBarAlwaysOff),
      visualDatabaseDisplay(_parent), deckListModel(deckListModel)
{
    connect(this, &VisualDatabaseDisplayFilterToolbarWidget::searchModelChanged, visualDatabaseDisplay,
            &VisualDatabaseDisplayWidget::onSearchModelChanged);

    sortGroupBox = new QGroupBox(this);
    filterGroupBox = new QGroupBox(this);

    auto scalePixmap = [](const QString &fileName) { return QIcon(QPixmap(fileName)).pixmap({20, 20}); };

    sortByLabel = new QLabel(this);
    sortByLabel->setPixmap(scalePixmap("theme:icons/sort_arrow_down"));

    filterByLabel = new QLabel(this);
    filterByLabel->setPixmap(scalePixmap("theme:icons/filter"));

    sortColumnCombo = new QComboBox(this);
    sortColumnCombo->setSizeAdjustPolicy(QComboBox::SizeAdjustPolicy::AdjustToContents);
    sortOrderCombo = new QComboBox(this);
    sortOrderCombo->setSizeAdjustPolicy(QComboBox::SizeAdjustPolicy::AdjustToContents);

    sortOrderCombo->addItem("Ascending", Qt::AscendingOrder);
    sortOrderCombo->addItem("Descending", Qt::DescendingOrder);
    sortOrderCombo->view()->setMinimumWidth(sortOrderCombo->view()->sizeHintForColumn(0));
    sortOrderCombo->adjustSize();

    // Populate columns dynamically from the model
    for (int i = 0; i < visualDatabaseDisplay->getDatabaseDisplayModel()->columnCount(); ++i) {
        QString header = visualDatabaseDisplay->getDatabaseDisplayModel()->headerData(i, Qt::Horizontal).toString();
        sortColumnCombo->addItem(header, i);
    }

    sortColumnCombo->view()->setMinimumWidth(sortColumnCombo->view()->sizeHintForColumn(0));
    sortColumnCombo->adjustSize();

    connect(sortColumnCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        int column = sortColumnCombo->currentData().toInt();
        Qt::SortOrder order = static_cast<Qt::SortOrder>(sortOrderCombo->currentData().toInt());
        visualDatabaseDisplay->getDatabaseView()->sortByColumn(column, order);

        emit searchModelChanged();
    });

    connect(sortOrderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        int column = sortColumnCombo->currentData().toInt();
        Qt::SortOrder order = static_cast<Qt::SortOrder>(sortOrderCombo->currentData().toInt());
        visualDatabaseDisplay->getDatabaseView()->sortByColumn(column, order);

        emit searchModelChanged();
    });

    quickFilterSaveLoadWidget = new SettingsButtonWidget(this);
    quickFilterSaveLoadWidget->setButtonIcon(themePixmap(QStringLiteral("icons/floppy_disk")));

    quickFilterCurrentWidget = new SettingsButtonWidget(this);
    quickFilterCurrentWidget->setButtonIcon(themePixmap(QStringLiteral("icons/filter")));

    quickFilterNameWidget = new SettingsButtonWidget(this);
    quickFilterNameWidget->setButtonIcon(themePixmap(QStringLiteral("icons/pen_to_square")));

    quickFilterMainTypeWidget = new SettingsButtonWidget(this);
    quickFilterMainTypeWidget->setButtonIcon(themePixmap(QStringLiteral("icons/circle_half_stroke")));

    quickFilterSubTypeWidget = new SettingsButtonWidget(this);
    quickFilterSubTypeWidget->setButtonIcon(themePixmap(QStringLiteral("icons/dragon")));

    quickFilterSetWidget = new SettingsButtonWidget(this);
    quickFilterSetWidget->setButtonIcon(themePixmap(QStringLiteral("icons/scroll")));

    quickFilterFormatLegalityWidget = new SettingsButtonWidget(this);
    quickFilterFormatLegalityWidget->setButtonIcon(themePixmap(QStringLiteral("icons/scale_balanced")));

    quickFilterTagWidget = new SettingsButtonWidget(this);
    quickFilterTagWidget->setButtonIcon(themePixmap(QStringLiteral("icons/tags")));

    retranslateUi();
}

void VisualDatabaseDisplayFilterToolbarWidget::initialize()
{
    auto sortLayout = QtUtils::makeLayout<QHBoxLayout>(sortGroupBox, 0);
    sortLayout->setAlignment(Qt::AlignLeft);

    auto filterLayout = QtUtils::makeLayout<QHBoxLayout>(filterGroupBox, 2);
    filterLayout->setAlignment(Qt::AlignLeft);

    // create settings widgets
    auto filterModel = visualDatabaseDisplay->getFilterModel();
    initializeCurrentFilters(filterModel);

    saveLoadWidget = new VisualDatabaseDisplayFilterSaveLoadWidget(this, filterModel);
    nameFilterWidget = new VisualDatabaseDisplayNameFilterWidget(this, filterModel, deckListModel);
    mainTypeFilterWidget = new VisualDatabaseDisplayMainTypeFilterWidget(this, filterModel);
    formatLegalityWidget = new VisualDatabaseDisplayFormatLegalityFilterWidget(this, filterModel);
    subTypeFilterWidget = new VisualDatabaseDisplaySubTypeFilterWidget(this, filterModel);
    setFilterWidget = new VisualDatabaseDisplaySetFilterWidget(this, filterModel);
    tagFilterWidget = new VisualDatabaseDisplayTagFilterWidget(this, filterModel);

    quickFilterSaveLoadWidget->addSettingsWidget(saveLoadWidget);
    quickFilterNameWidget->addSettingsWidget(nameFilterWidget);
    quickFilterMainTypeWidget->addSettingsWidget(mainTypeFilterWidget);
    quickFilterSubTypeWidget->addSettingsWidget(subTypeFilterWidget);
    quickFilterSetWidget->addSettingsWidget(setFilterWidget);
    quickFilterFormatLegalityWidget->addSettingsWidget(formatLegalityWidget);
    quickFilterTagWidget->addSettingsWidget(tagFilterWidget);

    // fill groupbox layouts
    sortLayout->addWidget(sortByLabel);
    sortLayout->addWidget(sortColumnCombo);
    sortLayout->addWidget(sortOrderCombo);

    filterLayout->addWidget(filterByLabel);
    filterLayout->addWidget(quickFilterCurrentWidget);
    filterLayout->addWidget(quickFilterNameWidget);
    filterLayout->addWidget(quickFilterMainTypeWidget);
    filterLayout->addWidget(quickFilterSubTypeWidget);
    filterLayout->addWidget(quickFilterSetWidget);
    filterLayout->addWidget(quickFilterFormatLegalityWidget);
    filterLayout->addWidget(quickFilterTagWidget);

    // put everything into main layout
    addWidget(sortGroupBox);
    addWidget(filterGroupBox);
    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
    addWidget(spacer);
    addWidget(quickFilterSaveLoadWidget);

    // Force a layout pass so sizeHint() is accurate
    layout()->activate();
    fullWidthHint = sizeHint().width();
}

void VisualDatabaseDisplayFilterToolbarWidget::retranslateUi()
{
    sortByLabel->setToolTip(tr("Sort by"));
    filterByLabel->setToolTip(tr("Filter by"));

    quickFilterSaveLoadWidget->setToolTip(tr("Save and load filters"));
    quickFilterNameWidget->setToolTip(tr("Filter by exact card name"));
    quickFilterMainTypeWidget->setToolTip(tr("Filter by card main-type"));
    quickFilterSubTypeWidget->setToolTip(tr("Filter by card sub-type"));
    quickFilterSetWidget->setToolTip(tr("Filter by set"));
    quickFilterFormatLegalityWidget->setToolTip(tr("Filter by format legality"));
    quickFilterTagWidget->setToolTip(tr("Filter by card tag"));

    quickFilterSaveLoadWidget->setButtonText(tr("Save/Load"));
    quickFilterNameWidget->setButtonText(tr("Name"));
    quickFilterMainTypeWidget->setButtonText(tr("Main Type"));
    quickFilterSubTypeWidget->setButtonText(tr("Sub Type"));
    quickFilterSetWidget->setButtonText(tr("Sets"));
    quickFilterFormatLegalityWidget->setButtonText(tr("Formats"));
    quickFilterTagWidget->setButtonText(tr("Tags"));
    retranslateCurrentFilters();
}


void VisualDatabaseDisplayFilterToolbarWidget::initializeCurrentFilters(FilterTreeModel *filterModel)
{
    currentFiltersPanel = new QWidget(this);
    currentFiltersPanel->setMinimumWidth(320);
    auto panelLayout = new QVBoxLayout(currentFiltersPanel);
    currentFiltersExplanation = new QLabel(currentFiltersPanel);
    currentFiltersExplanation->setWordWrap(true);
    panelLayout->addWidget(currentFiltersExplanation);
    currentFiltersSearch = new QLineEdit(currentFiltersPanel);
    currentFiltersSearch->setClearButtonEnabled(true);
    panelLayout->addWidget(currentFiltersSearch);

    auto controlsLayout = new QHBoxLayout;
    currentFiltersExpandButton = new QToolButton(currentFiltersPanel);
    currentFiltersCollapseButton = new QToolButton(currentFiltersPanel);
    currentFiltersPauseButton = new QToolButton(currentFiltersPanel);
    currentFiltersResumeButton = new QToolButton(currentFiltersPanel);
    controlsLayout->addWidget(currentFiltersExpandButton);
    controlsLayout->addWidget(currentFiltersCollapseButton);
    controlsLayout->addStretch();
    controlsLayout->addWidget(currentFiltersPauseButton);
    controlsLayout->addWidget(currentFiltersResumeButton);
    panelLayout->addLayout(controlsLayout);

    auto proxy = new CurrentFilterSearchProxyModel(currentFiltersPanel);
    proxy->setSourceModel(filterModel);
    currentFiltersProxy = proxy;
    currentFiltersTree = new QTreeView(currentFiltersPanel);
    currentFiltersTree->setModel(proxy);
    currentFiltersTree->setHeaderHidden(true);
    currentFiltersTree->setUniformRowHeights(true);
    currentFiltersTree->setRootIsDecorated(true);
    currentFiltersTree->setSelectionMode(QAbstractItemView::SingleSelection);
    currentFiltersTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    currentFiltersTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    currentFiltersTree->setMinimumHeight(180);
    panelLayout->addWidget(currentFiltersTree);
    currentFiltersEmptyLabel = new QLabel(currentFiltersPanel);
    currentFiltersEmptyLabel->setWordWrap(true);
    panelLayout->addWidget(currentFiltersEmptyLabel);
    currentFiltersCountLabel = new QLabel(currentFiltersPanel);
    panelLayout->addWidget(currentFiltersCountLabel);

    connect(currentFiltersSearch, &QLineEdit::textChanged, this, [this, proxy](const QString &text) {
        proxy->setSearchText(text);
        scheduleCurrentFiltersRefresh(true);
    });
    connect(currentFiltersExpandButton, &QToolButton::clicked, currentFiltersTree, &QTreeView::expandAll);
    connect(currentFiltersCollapseButton, &QToolButton::clicked, currentFiltersTree, &QTreeView::collapseAll);
    connect(currentFiltersPauseButton, &QToolButton::clicked, this, [this] { setCurrentFiltersPaused(true); });
    connect(currentFiltersResumeButton, &QToolButton::clicked, this, [this] { setCurrentFiltersPaused(false); });
    connect(filterModel, &QAbstractItemModel::dataChanged, this, [this] { scheduleCurrentFiltersRefresh(false); });
    connect(filterModel, &QAbstractItemModel::layoutChanged, this, [this] { scheduleCurrentFiltersRefresh(true); });
    connect(filterModel, &QAbstractItemModel::modelReset, this, [this] { scheduleCurrentFiltersRefresh(true); });
    connect(filterModel, &QAbstractItemModel::rowsInserted, this, [this] { scheduleCurrentFiltersRefresh(true); });
    connect(filterModel, &QAbstractItemModel::rowsRemoved, this, [this] { scheduleCurrentFiltersRefresh(false); });
    connect(filterModel, &QAbstractItemModel::rowsMoved, this, [this] { scheduleCurrentFiltersRefresh(false); });
    connect(filterModel, &QObject::destroyed, this, [this] { scheduleCurrentFiltersRefresh(false); });
    quickFilterCurrentWidget->addSettingsWidget(currentFiltersPanel);
    retranslateCurrentFilters();
    refreshCurrentFilters(true);
}

void VisualDatabaseDisplayFilterToolbarWidget::retranslateCurrentFilters()
{
    quickFilterCurrentWidget->setButtonText(tr("Current filters"));
    quickFilterCurrentWidget->setToolTip(tr("View, search, and enable or disable current filter criteria"));
    if (!currentFiltersPanel) {
        return;
    }
    currentFiltersExplanation->setText(
        tr("Unchecking a group pauses its criteria and preserves the checkboxes below it."));
    currentFiltersSearch->setPlaceholderText(tr("Search current filters"));
    currentFiltersSearch->setAccessibleName(tr("Search current filters"));
    currentFiltersTree->setAccessibleName(tr("Current filter criteria"));
    currentFiltersExpandButton->setText(tr("Expand"));
    currentFiltersExpandButton->setToolTip(tr("Expand all visible filter groups"));
    currentFiltersCollapseButton->setText(tr("Collapse"));
    currentFiltersCollapseButton->setToolTip(tr("Collapse all visible filter groups"));
    currentFiltersPauseButton->setText(tr("Pause groups"));
    currentFiltersPauseButton->setToolTip(
        tr("Pause all current top-level groups, including groups hidden by search. Child checkboxes are preserved."));
    currentFiltersResumeButton->setText(tr("Resume groups"));
    currentFiltersResumeButton->setToolTip(
        tr("Enable all current top-level groups, including groups hidden by search. "
           "Individually disabled criteria and subgroups stay disabled."));
    refreshCurrentFilters(false);
}

void VisualDatabaseDisplayFilterToolbarWidget::scheduleCurrentFiltersRefresh(bool expandTree)
{
    currentFiltersExpandPending = currentFiltersExpandPending || expandTree;
    if (currentFiltersRefreshPending) {
        return;
    }
    currentFiltersRefreshPending = true;
    QTimer::singleShot(0, this, [this] {
        currentFiltersRefreshPending = false;
        const bool expandTree = currentFiltersExpandPending;
        currentFiltersExpandPending = false;
        refreshCurrentFilters(expandTree);
    });
}

void VisualDatabaseDisplayFilterToolbarWidget::refreshCurrentFilters(bool expandTree)
{
    if (!currentFiltersPanel) {
        return;
    }
    const QAbstractItemModel *model = currentFiltersProxy->sourceModel();
    CurrentFilterCriterionCounts counts;
    int enabledParents = 0;
    int pausedParents = 0;
    if (model) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex parent = model->index(row, 0);
            const auto branch = countCurrentFilterCriteria(model, parent, true);
            counts.total += branch.total;
            counts.active += branch.active;
            if (branch.total > 0) {
                if (model->data(parent, Qt::CheckStateRole).toInt() == Qt::Checked) {
                    ++enabledParents;
                } else {
                    ++pausedParents;
                }
            }
        }
    }
    currentFiltersCountLabel->setText(tr("Active criteria: %1 / %2").arg(counts.active).arg(counts.total));
    const bool visible = counts.total > 0 && currentFiltersProxy->rowCount() > 0;
    currentFiltersTree->setVisible(visible);
    currentFiltersEmptyLabel->setVisible(!visible);
    currentFiltersEmptyLabel->setText(counts.total == 0
        ? tr("No current filters. Add criteria using the filter buttons.")
        : tr("No criteria match your search."));
    currentFiltersExpandButton->setEnabled(visible);
    currentFiltersCollapseButton->setEnabled(visible);
    currentFiltersPauseButton->setEnabled(enabledParents > 0);
    currentFiltersResumeButton->setEnabled(pausedParents > 0);
    if (expandTree && visible) {
        currentFiltersTree->expandAll();
    }
}

void VisualDatabaseDisplayFilterToolbarWidget::setCurrentFiltersPaused(bool paused)
{
    QPointer<QAbstractItemModel> model = currentFiltersProxy->sourceModel();
    QPointer<VisualDatabaseDisplayFilterToolbarWidget> toolbar(this);
    if (!model) {
        return;
    }

    // Layout-only mutations can retain a persistent root index after deleting
    // its node. Stop this command if an observer restructures the source.
    bool structureChanged = false;
    QObject changeObserver;
    const auto onStructureChanged = [&structureChanged] { structureChanged = true; };
    connect(model.data(), &QAbstractItemModel::layoutAboutToBeChanged, &changeObserver, onStructureChanged);
    connect(model.data(), &QAbstractItemModel::modelAboutToBeReset, &changeObserver, onStructureChanged);
    connect(model.data(), &QAbstractItemModel::rowsAboutToBeInserted, &changeObserver, onStructureChanged);
    connect(model.data(), &QAbstractItemModel::rowsAboutToBeRemoved, &changeObserver, onStructureChanged);
    connect(model.data(), &QAbstractItemModel::rowsAboutToBeMoved, &changeObserver, onStructureChanged);

    QList<QPersistentModelIndex> parents;
    const int rows = model->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QModelIndex parent = model->index(row, 0);
        if (hasCurrentFilterCriteria(model.data(), parent)) {
            parents.append(QPersistentModelIndex(parent));
        }
    }
    const Qt::CheckState state = paused ? Qt::Unchecked : Qt::Checked;
    for (const QPersistentModelIndex &parent : parents) {
        if (!toolbar || !model || structureChanged) {
            return;
        }
        if (currentFiltersProxy->sourceModel() != model.data()) {
            return;
        }
        if (parent.isValid() && model->data(parent, Qt::CheckStateRole).toInt() != state) {
            model->setData(parent, state, Qt::CheckStateRole);
        }
    }
    if (toolbar) {
        scheduleCurrentFiltersRefresh(false);
    }
}

void VisualDatabaseDisplayFilterToolbarWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateCompactMode(event->size().width());
}

void VisualDatabaseDisplayFilterToolbarWidget::updateCompactMode(int availableWidth)
{
    const bool compact = availableWidth < fullWidthHint;

    const QList<SettingsButtonWidget *> filterButtons = {
        quickFilterSaveLoadWidget, quickFilterNameWidget,           quickFilterMainTypeWidget, quickFilterSubTypeWidget,
        quickFilterSetWidget,      quickFilterFormatLegalityWidget, quickFilterTagWidget, quickFilterCurrentWidget,
    };

    for (auto *btn : filterButtons) {
        if (btn->isCompact() != compact) { // only act on transitions
            btn->setCompact(compact);
        }
    }
}
