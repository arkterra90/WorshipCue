#include "PresentationSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

PresentationSettingsDialog::PresentationSettingsDialog(const QString &title,
    const PresentationSettings &settings, QWidget *parent,
    const PresentationSettings &programDefaults, bool programMode, bool bulkMode)
    : QDialog(parent), initialSettings_(settings), programMode_(programMode) {
    setObjectName(bulkMode ? QStringLiteral("bulkTransitionSettingsDialog") : programMode ? QStringLiteral("programTransitionSettingsDialog")
                              : QStringLiteral("presentationSettingsDialog"));
    setWindowTitle(bulkMode ? QStringLiteral("Transitions for Selected Items") : programMode ? QStringLiteral("Global Slide Transitions")
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
    interval_->setToolTip(QStringLiteral("Changing the interval turns on automatic advancement."));
    timing->addRow(QStringLiteral("Time per slide"), interval_);
    loop_ = new QCheckBox(QStringLiteral("Loop back to the first slide"), this);
    loop_->setObjectName(QStringLiteral("loopPresentation"));
    loop_->setChecked(settings.loop);
    loop_->setToolTip(QStringLiteral("Enabling looping turns on automatic advancement."));
    timing->addRow(QString(), loop_);
    timingLayout->addLayout(timing);
    connect(interval_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this] { autoAdvance_->setChecked(true); });
    connect(loop_, &QCheckBox::toggled, this,
            [this](bool checked) { if (checked) autoAdvance_->setChecked(true); });
    auto *timingHint = new QLabel(QStringLiteral(
        "Changing the interval or enabling looping turns on automatic advancement. Uncheck it for manual slides. "
        "Timing starts when a slide is taken live and includes its transition. "
        "Videos use the same interval. At the end, hold the last slide or loop within this presentation."), this);
    timingHint->setWordWrap(true);
    timingHint->setObjectName(QStringLiteral("hint"));
    timingLayout->addWidget(timingHint);
    timingWidget->setVisible(!programMode_ && !bulkMode);
    layout->addWidget(timingWidget);

    overrideTransition_ = new QCheckBox(bulkMode ? QStringLiteral("Override global transition for selected items") : QStringLiteral("Override global transition for this presentation"), this);
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
    const auto useCustomTransition = [this] {
        if (!programMode_) overrideTransition_->setChecked(true);
    };
    connect(transition_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, useCustomTransition);
    connect(duration_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, useCustomTransition);
    connect(overrideTransition_, &QCheckBox::toggled, this, [this, programDefaults](bool checked) {
        if (checked || programMode_) return;
        const auto inherited = PresentationSettings{}.resolvedWithDefaults(programDefaults);
        const QSignalBlocker typeBlock(transition_);
        const QSignalBlocker durationBlock(duration_);
        transition_->setCurrentIndex(transition_->findData(int(inherited.transition)));
        duration_->setValue(inherited.transitionDurationMs / 1000.0);
    });
    transition_->setToolTip(programMode_ ? QStringLiteral("Default transition for presentations that inherit global settings.")
        : QStringLiteral("Changing the transition creates an override for this presentation or selection."));
    duration_->setToolTip(programMode_ ? QStringLiteral("Cut is always instant; other transitions use this time.")
        : QStringLiteral("Changing the duration creates an override. Cut is always instant; other transitions use this time."));
    layout->addLayout(transitions);
    auto *transitionHint = new QLabel(programMode_ ? QStringLiteral("Cut is instant. Fade and slide transitions use the selected duration.")
        : QStringLiteral("Edit type or time directly to override the global transition. Uncheck override to use global settings again. Cut is always instant."), this);
    transitionHint->setWordWrap(true);
    transitionHint->setObjectName(QStringLiteral("hint"));
    layout->addWidget(transitionHint);

    auto *scope = new QLabel(bulkMode ? QStringLiteral(
        "Applies only transition type, duration, and inheritance. Timing, lyrics, Looks, and media stay as saved. "
        "Library presentations change across all their service entries; standalone media changes only in the selected service items. "
        "Mixed selections start with the first item's settings. Uncheck override to inherit global defaults. "
        "Changes apply on the next take and can be undone from the service playlist.") : programMode_ ? QStringLiteral(
        "The global default applies to all presentations that do not override it. "
        "Changes take effect on the next slide cue or Blackout, Clear Text, or Restore operation, including in a running service. "
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
    PresentationSettings result = initialSettings_;
    result.autoAdvanceMs = autoAdvance_->isChecked() ? qRound(interval_->value() * 1000) : 0;
    result.loop = loop_->isChecked();
    result.overrideTransition = programMode_ || overrideTransition_->isChecked();
    if (result.overrideTransition) {
        result.transition = SlideTransitionType(transition_->currentData().toInt());
        result.transitionDurationMs = qRound(duration_->value() * 1000);
    }
    return result;
}
