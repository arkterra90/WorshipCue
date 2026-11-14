#pragma once
#include <QDialog>
class OutputManager;
class OutputSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit OutputSettingsDialog(OutputManager *outputs, QWidget *parent = nullptr);
};
