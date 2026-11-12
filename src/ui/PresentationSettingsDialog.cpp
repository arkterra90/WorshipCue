#include "PresentationSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

PresentationSettingsDialog::PresentationSettingsDialog(const QString &title,
    const PresentationSettings &settings, QWidget *parent,
    const PresentationSettings &programDefaults, bool programMode)
    : QDialog(parent), programMode_(programMode) {
    setObjectName(programMode ? QStringLiteral("programTransitionSettingsDialog")
                              : QStringLiteral("presentationSettingsDialog"));
    setWindowTitle(programMode ? QStringLiteral("Global Slide Transitions")
                               : QStringLiteral("Timing & Transitions"));
    setMinimumWidth(460);
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(title, this);
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    QFont font = heading->font();
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);

    auto *timingWidget = new QWidget(this);
    auto *timingLayout = new QVBoxLayout(timingWidget);
    timingLayout->setContentsMargins(0, 0, 0, 0);
    autoAdvance_ = new QCheckBox(QStringLiteral("Automatically advance slides"), this);
    autoAdvance_->setObjectName(QStringLiteral("autoAdvanceEnabled"));
    autoAdvance_->setChecked(settings.autoAdvanceMs > 0);
    timingLayout->addWidget(autoAdvance_);
    auto *timing = new QFormLayout;
    interval_ = new QDoubleSpinBox(this);
    interval_->setObjectName(QStringLiteral("autoAdvanceInterval"));
    interval_->setRange(0.1, 3600.0);
    interval_->setDecimals(1);
    interval_->setSingleStep(0.5);
    interval_->setSuffix(QStringLiteral(" seconds"));
    interval_->setValue(settings.autoAdvanceMs > 0 ? settings.autoAdvanceMs / 1000.0 : 5.0);
    interval_->setEnabled(autoAdvance_->isChecked());
    timing->addRow(QStringLiteral("Time per slide"), interval_);
    loop_ = new QCheckBox(QStringLiteral("Loop back to the first slide"), this);
    loop_->setObjectName(QStringLiteral("loopPresentation"));
    loop_->setChecked(settings.loop);
    loop_->setEnabled(autoAdvance_->isChecked());
    timing->addRow(QString(), loop_);
    timingLayout->addLayout(timing);
    connect(autoAdvance_, &QCheckBox::toggled, interval_, &QWidget::setEnabled);
    connect(autoAdvance_, &QCheckBox::toggled, loop_, &QWidget::setEnabled);
    auto *timingHint = new QLabel(QStringLiteral(
        "Timing starts when a slide is taken live and includes its transition. "
        "Videos use the same interval. At the end, hold the last slide or loop within this presentation."), this);
    timingHint->setWordWrap(true);
    timingHint->setObjectName(QStringLiteral("hint"));
    timingLayout->addWidget(timingHint);
    timingWidget->setVisible(!programMode_);
    layout->addWidget(timingWidget);

    overrideTransition_ = new QCheckBox(QStringLiteral("Override global transition for this presentation"), this);
    overrideTransition_->setObjectName(QStringLiteral("overrideProgramTransition"));
    overrideTransition_->setChecked(settings.overrideTransition);
    overrideTransition_->setVisible(!programMode_);
    layout->addWidget(overrideTransition_);
    const PresentationSettings displayed = programMode_ ? settings : settings.resolvedWithDefaults(programDefaults);

    auto *transitions = new QFormLayout;
    transition_ = new QComboBox(this);
    transition_->setObjectName(QStringLiteral("slideTransitionType"));
    transition_->addItem(QStringLiteral("Cut (instant)"), int(SlideTransitionType::Cut));
    transition_->addItem(QStringLiteral("Fade"), int(SlideTransitionType::Fade));
    transition_->addItem(QStringLiteral("Slide Left"), int(SlideTransitionType::SlideLeft));
    transition_->addItem(QStringLiteral("Slide Right"), int(SlideTransitionType::SlideRight));
    transition_->setCurrentIndex(transition_->findData(int(displayed.transition)));
    transitions->addRow(QStringLiteral("Transition"), transition_);
    duration_ = new QDoubleSpinBox(this);
    duration_->setObjectName(QStringLiteral("slideTransitionDuration"));
    duration_->setRange(0.0, 10.0);
    duration_->setDecimals(2);
    duration_->setSingleStep(0.1);
    duration_->setSuffix(QStringLiteral(" seconds"));
    duration_->setValue(displayed.transitionDurationMs / 1000.0);
    transitions->addRow(QStringLiteral("Fade / transition time"), duration_);
    const auto updateTransitionControls = [this] {
        const bool editable = programMode_ || overrideTransition_->isChecked();
        transition_->setEnabled(editable);
        duration_->setEnabled(editable && transition_->currentData().toInt() != int(SlideTransitionType::Cut));
    };
    connect(transition_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateTransitionControls);
    connect(overrideTransition_, &QCheckBox::toggled, this, updateTransitionControls);
    updateTransitionControls();
    layout->addLayout(transitions);

    auto *scope = new QLabel(programMode_ ? QStringLiteral(
        "The global default applies to all presentations that do not override it. "
        "Changes take effect on the next slide cue, including in a running service. "
        "A presentation's explicit override takes precedence.")
        : QStringLiteral(
        "Saved for this library presentation and all its service entries. "
        "Transition type and time inherit the global setting unless overridden here. "
        "Changes apply the next time you take a slide live. "
        "Blackout and Clear Text pause auto-advance until you restore the audience."), this);
    scope->setWordWrap(true);
    scope->setObjectName(QStringLiteral("hint"));
    layout->addWidget(scope);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    actions->setObjectName(QStringLiteral("presentationSettingsActions"));
    connect(actions, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(actions);
}

PresentationSettings PresentationSettingsDialog::settings() const {
    PresentationSettings result;
    result.autoAdvanceMs = autoAdvance_->isChecked() ? qRound(interval_->value() * 1000) : 0;
    result.loop = loop_->isChecked();
    result.transition = SlideTransitionType(transition_->currentData().toInt());
    result.transitionDurationMs = qRound(duration_->value() * 1000);
    result.overrideTransition = programMode_ || overrideTransition_->isChecked();
    return result;
}
