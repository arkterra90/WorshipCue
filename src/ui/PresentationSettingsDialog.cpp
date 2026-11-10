#include "PresentationSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

PresentationSettingsDialog::PresentationSettingsDialog(const QString &title,
    const PresentationSettings &settings, QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("presentationSettingsDialog"));
    setWindowTitle(QStringLiteral("Timing & Transitions"));
    setMinimumWidth(460);
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(title, this);
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    QFont font = heading->font();
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);

    autoAdvance_ = new QCheckBox(QStringLiteral("Automatically advance slides"), this);
    autoAdvance_->setObjectName(QStringLiteral("autoAdvanceEnabled"));
    autoAdvance_->setChecked(settings.autoAdvanceMs > 0);
    layout->addWidget(autoAdvance_);
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
    layout->addLayout(timing);
    connect(autoAdvance_, &QCheckBox::toggled, interval_, &QWidget::setEnabled);
    connect(autoAdvance_, &QCheckBox::toggled, loop_, &QWidget::setEnabled);
    auto *timingHint = new QLabel(QStringLiteral(
        "Timing starts when a slide is taken live and includes its transition. "
        "Videos use the same interval. At the end, hold the last slide or loop within this presentation."), this);
    timingHint->setWordWrap(true);
    timingHint->setObjectName(QStringLiteral("hint"));
    layout->addWidget(timingHint);

    auto *transitions = new QFormLayout;
    transition_ = new QComboBox(this);
    transition_->setObjectName(QStringLiteral("slideTransitionType"));
    transition_->addItem(QStringLiteral("Cut (instant)"), int(SlideTransitionType::Cut));
    transition_->addItem(QStringLiteral("Fade"), int(SlideTransitionType::Fade));
    transition_->addItem(QStringLiteral("Slide Left"), int(SlideTransitionType::SlideLeft));
    transition_->addItem(QStringLiteral("Slide Right"), int(SlideTransitionType::SlideRight));
    transition_->setCurrentIndex(transition_->findData(int(settings.transition)));
    transitions->addRow(QStringLiteral("Transition"), transition_);
    duration_ = new QDoubleSpinBox(this);
    duration_->setObjectName(QStringLiteral("slideTransitionDuration"));
    duration_->setRange(0.0, 10.0);
    duration_->setDecimals(2);
    duration_->setSingleStep(0.1);
    duration_->setSuffix(QStringLiteral(" seconds"));
    duration_->setValue(settings.transitionDurationMs / 1000.0);
    duration_->setEnabled(settings.transition != SlideTransitionType::Cut);
    transitions->addRow(QStringLiteral("Transition time"), duration_);
    connect(transition_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        duration_->setEnabled(transition_->currentData().toInt() != int(SlideTransitionType::Cut));
    });
    layout->addLayout(transitions);

    auto *scope = new QLabel(QStringLiteral(
        "Saved for this library presentation and all its service entries. "
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
    return result;
}
