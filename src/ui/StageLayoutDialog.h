#pragma once
#include <QDialog>
class OutputManager;
class StageLayoutDialog : public QDialog {
    Q_OBJECT
public:
    explicit StageLayoutDialog(OutputManager *outputs, QWidget *parent = nullptr);
};
class StageControlsDialog : public QDialog {
    Q_OBJECT
public:
    explicit StageControlsDialog(OutputManager *outputs, QWidget *parent = nullptr);
};
