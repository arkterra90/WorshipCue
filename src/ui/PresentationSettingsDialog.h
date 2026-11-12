#pragma once

#include "../core/PresentationSettings.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

class PresentationSettingsDialog : public QDialog {
    Q_OBJECT
public:
    PresentationSettingsDialog(const QString &title, const PresentationSettings &settings,
                               QWidget *parent = nullptr,
                               const PresentationSettings &programDefaults = {},
                               bool programMode = false);
    PresentationSettings settings() const;

private:
    QCheckBox *autoAdvance_;
    QDoubleSpinBox *interval_;
    QCheckBox *loop_;
    QComboBox *transition_;
    QDoubleSpinBox *duration_;
    QCheckBox *overrideTransition_;
    bool programMode_ = false;
};
