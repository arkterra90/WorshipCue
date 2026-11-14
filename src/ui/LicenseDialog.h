#pragma once
#include <QDialog>
class LicenseManager;
class LicenseDialog : public QDialog {
    Q_OBJECT
  public:
    explicit LicenseDialog(LicenseManager *license, QWidget *parent = nullptr);
};
