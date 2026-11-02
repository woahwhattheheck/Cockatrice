#include "visual_database_filter_display_widget.h"

#include "../../../client/settings/cache_settings.h"

#include <QChar>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <libcockatrice/settings/paths_settings.h>
#include <qnamespace.h>

FilterDisplayWidget::FilterDisplayWidget(QWidget *parent, const QString &filename, FilterTreeModel *_filterModel)
    : QWidget(parent), filterFilename(filename), filterModel(_filterModel)
{
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    // Create layout
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Filter button (displays filename)
    filterButton = new QPushButton(filterFilename, this);
    layout->addWidget(filterButton);

    previewButton = new QPushButton(this);
    layout->addWidget(previewButton);

    // Create close button (inside the filter button)
    closeButton = new QPushButton("x", this);
    closeButton->setFixedSize(20, 20); // Small square close button
    closeButton->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(closeButton);

    // Connect the filter button for loading the filter
    connect(filterButton, &QPushButton::clicked, this, &FilterDisplayWidget::loadFilter);

    // Connect the close button for deleting the filter
    connect(closeButton, &QPushButton::clicked, this, &FilterDisplayWidget::deleteFilter);
    connect(previewButton, &QPushButton::clicked, this, &FilterDisplayWidget::previewFilter);
    retranslateUi();
}

QSize FilterDisplayWidget::sizeHint() const
{
    // Include translated action labels and native button margins.
    return QWidget::sizeHint();
}

void FilterDisplayWidget::retranslateUi()
{
    filterButton->setToolTip(tr("Apply saved filter '%1'").arg(filterFilename));
    filterButton->setAccessibleName(tr("Apply saved filter %1").arg(filterFilename));
    previewButton->setText(tr("Preview"));
    previewButton->setToolTip(tr("Preview '%1' without changing the current filters").arg(filterFilename));
    previewButton->setAccessibleName(tr("Preview saved filter %1").arg(filterFilename));
    closeButton->setToolTip(tr("Delete saved filter '%1'").arg(filterFilename));
    closeButton->setAccessibleName(tr("Delete saved filter %1").arg(filterFilename));
}

void FilterDisplayWidget::previewFilter()
{
    const QString filename = filterFilename;
    emit filterPreviewRequested(filename);
}

void FilterDisplayWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPointer<FilterDisplayWidget> owner(this);
        emit filterClicked();
        if (!owner) {
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void FilterDisplayWidget::loadFilter()
{
    // Trigger the loading of the filter
    const QString filename = filterFilename;
    emit filterLoadRequested(filename);
}

void FilterDisplayWidget::deleteFilter()
{
    // Capture the target before opening the dialog. Refreshing/closing the pane
    // destroys this receiver and disconnects the pending confirmation safely.
    const QString filename = filterFilename;
    const QString filePath = SettingsCache::instance().paths().getFiltersPath() + QDir::separator() + filename;
    auto *dialog = new QMessageBox(QMessageBox::Question, tr("Confirm Delete"),
                                   tr("Are you sure you want to delete the filter '%1'?").arg(filename),
                                   QMessageBox::Yes | QMessageBox::No, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setDefaultButton(QMessageBox::No);
    dialog->setEscapeButton(QMessageBox::No);
    const QPointer<FilterDisplayWidget> owner(this);
    connect(dialog, &QMessageBox::finished, this, [owner, filename, filePath](int result) {
        if (!owner || result != QMessageBox::Yes) {
            return;
        }
        const QPointer<FilterDisplayWidget> targetOwner = owner;
        const QString targetFilename = filename;
        const QString targetPath = filePath;
        QFile file(targetPath);
        if (file.remove()) {
            targetOwner->hide();
            if (!targetOwner) {
                return;
            }
            targetOwner->deleteLater();
            // A synchronous listener may rebuild or destroy the entire pane.
            // Do not access this widget after emitting the notification.
            emit targetOwner->filterDeleted(targetFilename);
        } else {
            auto *error = new QMessageBox(QMessageBox::Warning, tr("Delete Failed"),
                                          tr("Failed to delete filter '%1': %2").arg(targetFilename, file.errorString()),
                                          QMessageBox::Ok, targetOwner.data());
            error->setAttribute(Qt::WA_DeleteOnClose);
            error->open();
        }
    });
    dialog->open();
}
