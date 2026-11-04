#ifndef COCKATRICE_CARD_ART_CROP_DIALOG_H
#define COCKATRICE_CARD_ART_CROP_DIALOG_H

#include <QDialog>
#include <QPixmap>
#include <QRectF>
#include <QWidget>

class QDoubleSpinBox;

class CardArtCropSelection : public QWidget
{
    Q_OBJECT
public:
    explicit CardArtCropSelection(const QPixmap &uprightImage, QWidget *parent = nullptr);
    void setCrop(const QRectF &crop);
    QSize sizeHint() const override;

signals:
    void cropEdited(const QRectF &crop);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRectF imageRect() const;
    QPointF normalizedPosition(const QPointF &position) const;
    void updateSelection(const QPointF &position);
    QPixmap image;
    QRectF crop;
    QPointF dragStart;
    bool dragging = false;
};

class CardArtCropDialog : public QDialog
{
    Q_OBJECT
public:
    CardArtCropDialog(const QPixmap &uprightImage,
                      const QRectF &initialCrop,
                      const QRectF &automaticCrop,
                      QWidget *parent = nullptr);
    QRectF selectedCrop() const;

private:
    void applyCrop(const QRectF &crop);
    void updateControls(const QRectF &crop);
    CardArtCropSelection *selection;
    QDoubleSpinBox *left, *top, *width, *height;
    QRectF currentCrop;
};

#endif
