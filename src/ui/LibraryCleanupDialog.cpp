#include "LibraryCleanupDialog.h"
#include "../data/LibraryRepository.h"
#include <QApplication>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString date(qint64 value) {
    return value>0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch(value).toLocalTime().date(),QLocale::ShortFormat) : QStringLiteral("Unknown");
}
struct ScanResult { QVector<CleanupItem> items; QString error; int days = 0; };
QString bytes(qint64 value) { return QLocale().formattedDataSize(value); }
QString kindName(CleanupKind kind) {
    switch(kind){case CleanupKind::Presentation:return "Presentation";case CleanupKind::MediaAsset:return "Media asset";case CleanupKind::File:return "Managed file";}
    return {};
}
QLabel *label(QVBoxLayout *layout,const QString &text,const char *name="") {
    auto *l=new QLabel(text);l->setObjectName(QString::fromLatin1(name));l->setWordWrap(true);layout->addWidget(l);return l;
}
QPushButton *button(QHBoxLayout *layout,const QString &text,const char *name) {
    auto *b=new QPushButton(text);b->setObjectName(QString::fromLatin1(name));layout->addWidget(b);return b;
}
QTableWidget *table(QVBoxLayout *layout,const QStringList &headings,const char *name) {
    auto *t=new QTableWidget(0,headings.size());t->setObjectName(QString::fromLatin1(name));t->setHorizontalHeaderLabels(headings);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);t->setSelectionBehavior(QAbstractItemView::SelectRows);t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->horizontalHeader()->setStretchLastSection(true);t->verticalHeader()->hide();layout->addWidget(t,1);return t;
}
bool review(QWidget *parent,const CleanupPlan &plan,bool permanent) {
    QDialog d(parent);d.setObjectName(permanent?"cleanupPurgePreview":"cleanupArchivePreview");d.setWindowTitle(permanent?"Review Permanent Removal":"Review Move to Recovery");d.resize(940,650);
    auto *layout=new QVBoxLayout(&d);
    label(layout,permanent?"Permanently delete the listed library records and removable files. This cannot be undone. Files still referenced by another item or saved Look will be retained.":"Move these items out of the library into Recovery. Their records, MIDI assignments, and files are retained so you can restore them. This step frees no disk space.");
    label(layout,QStringLiteral("%1 items · estimated recoverable file storage: %2%3").arg(plan.items.size()).arg(bytes(plan.recoverableBytes),permanent?QString():QStringLiteral(" after permanent deletion")),"cleanupReviewSummary");
    auto *items=table(layout,{"Type","Item"},"cleanupReviewItems");items->setRowCount(plan.items.size());items->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    for(int r=0;r<plan.items.size();++r){items->setItem(r,0,new QTableWidgetItem(kindName(plan.items.at(r).key.kind)));items->setItem(r,1,new QTableWidgetItem(plan.items.at(r).name));}
    auto *files=table(layout,{"Managed file","Size","Disposition"},"cleanupReviewFiles");files->setRowCount(plan.files.size());files->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    for(int r=0;r<plan.files.size();++r){const auto &f=plan.files.at(r);files->setItem(r,0,new QTableWidgetItem(f.path));files->setItem(r,1,new QTableWidgetItem(bytes(f.bytes)));files->setItem(r,2,new QTableWidgetItem(f.removable?(permanent?"Permanently remove":"Retain until permanent removal"):f.reason));files->item(r,2)->setToolTip(files->item(r,2)->text());}
    label(layout,"Estimates count each managed path once, exclude shared/protected files and database space, and may differ from actual filesystem allocation. References and file sizes are checked again before the operation.");
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&d);auto *confirm=buttons->addButton(permanent?"Permanently Delete":"Move to Recovery",QDialogButtonBox::AcceptRole);confirm->setObjectName("confirmCleanup");confirm->setDefault(false);buttons->button(QDialogButtonBox::Cancel)->setDefault(true);layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    return d.exec()==QDialog::Accepted;
}
}
void editLibraryCleanup(QWidget *parent,LibraryRepository &repo,std::function<CleanupProtection()> protection) {
    QDialog d(parent);d.setObjectName("libraryCleanupDialog");d.setWindowTitle("Library Cleanup / Storage");d.resize(1120,730);
    auto *layout=new QVBoxLayout(&d);label(layout,"Find content with recorded use older than your chosen period. Saved services, presentation references, live snapshots, unsafe paths, and unknown usage dates are protected. Moving to Recovery is reversible; disk space is freed only after a separate permanent-delete review.");
    auto *tabs=new QTabWidget(&d);tabs->setObjectName("cleanupTabs");layout->addWidget(tabs,1);
    auto *find=new QWidget;auto *fl=new QVBoxLayout(find);tabs->addTab(find,"Find Unused Content");auto *controls=new QHBoxLayout;fl->addLayout(controls);
    controls->addWidget(new QLabel("Unused for more than"));auto *days=new QSpinBox;days->setObjectName("cleanupDays");days->setRange(1,36500);days->setValue(90);days->setSuffix(" days");controls->addWidget(days);
    auto *scan=button(controls,"Find Content","cleanupScan");auto *showProtected=new QCheckBox("Show protected / unknown-age items");showProtected->setObjectName("cleanupShowProtected");showProtected->setChecked(true);controls->addWidget(showProtected);controls->addStretch();
    auto *candidates=table(fl,{"Select","Type","Item","Created","Imported","Modified","Last used","Eligibility / protection"},"cleanupCandidates");candidates->setColumnWidth(0,55);candidates->setColumnWidth(2,200);
    auto *selectRow=new QHBoxLayout;fl->addLayout(selectRow);auto *all=button(selectRow,"Select All Eligible","cleanupSelectAll");auto *none=button(selectRow,"Clear Selection","cleanupSelectNone");selectRow->addStretch();auto *archive=button(selectRow,"Review Move to Recovery…","cleanupArchive");archive->setEnabled(false);
    auto *summary=label(fl,"Choose a period and Find Content. Viewing or selecting a row does not count as use.","cleanupSummary");
    auto *recovery=new QWidget;auto *rl=new QVBoxLayout(recovery);tabs->addTab(recovery,"Recovery");label(rl,"Restore a whole batch with its original identities and assignments, or review it for permanent removal. Files remain at their original managed paths until permanent removal. Pending removals can be retried after resolving file permissions.");
    auto *batches=table(rl,{"Moved to Recovery","Items","State","Batch"},"cleanupBatches");auto *recoveryRow=new QHBoxLayout;rl->addLayout(recoveryRow);auto *restore=button(recoveryRow,"Restore Selected Batch","cleanupRestore");auto *purge=button(recoveryRow,"Review Permanent Removal…","cleanupPurge");auto *retry=button(recoveryRow,"Refresh / Retry Pending Removal","cleanupRetry");
    auto *error=label(layout,QString(),"cleanupError");
    QVector<CleanupItem> found;QVector<CleanupBatch> saved;int scannedDays=0;
    auto keys=[&]{QVector<CleanupKey> selected;for(int r=0;r<candidates->rowCount();++r)if(candidates->item(r,0)->checkState()==Qt::Checked)selected.append(found.at(candidates->item(r,0)->data(Qt::UserRole).toInt()).key);return selected;};
    auto loadBatches=[&]{saved=repo.cleanupBatches();if(!repo.lastError().isEmpty())error->setText(repo.lastError());batches->setRowCount(saved.size());for(int r=0;r<saved.size();++r){const auto &b=saved.at(r);const QStringList columns{date(b.createdAt),QString::number(b.itemCount),b.state=="purged"?"Permanent removal committed; files pending":b.state,b.id};for(int c=0;c<columns.size();++c)batches->setItem(r,c,new QTableWidgetItem(columns.at(c)));}restore->setEnabled(!saved.isEmpty());purge->setEnabled(!saved.isEmpty());};
    auto fill=[&]{const QSignalBlocker block(candidates);candidates->setRowCount(0);int eligible=0;for(int i=0;i<found.size();++i){const auto &item=found.at(i);if(item.eligible)++eligible;if(!item.eligible&&!showProtected->isChecked())continue;const int r=candidates->rowCount();candidates->insertRow(r);auto *check=new QTableWidgetItem;check->setData(Qt::UserRole,i);check->setFlags(Qt::ItemIsSelectable|(item.eligible?Qt::ItemIsEnabled|Qt::ItemIsUserCheckable:Qt::NoItemFlags));check->setCheckState(Qt::Unchecked);candidates->setItem(r,0,check);const QStringList values{kindName(item.key.kind),item.name,date(item.dates.createdAt),date(item.dates.importedAt),date(item.dates.modifiedAt),date(item.dates.lastUsedAt),item.reason};for(int c=0;c<values.size();++c){auto *cell=new QTableWidgetItem(values.at(c));cell->setToolTip(values.at(c));candidates->setItem(r,c+1,cell);}}summary->setText(QStringLiteral("%1 eligible · %2 protected · no items selected").arg(eligible).arg(found.size()-eligible));archive->setEnabled(false);};
    auto *watcher=new QFutureWatcher<ScanResult>(&d);
    auto finishScan=[&](const ScanResult &result){found=result.items;scannedDays=result.days;scan->setEnabled(true);candidates->setEnabled(true);retry->setEnabled(true);days->setEnabled(true);showProtected->setEnabled(true);all->setEnabled(true);find->unsetCursor();if(!result.error.isEmpty())error->setText(result.error);fill();loadBatches();};
    QObject::connect(watcher,&QFutureWatcher<ScanResult>::finished,&d,[&]{finishScan(watcher->result());});
    QObject::connect(&d,&QDialog::finished,&d,[&]{if(watcher->isRunning())watcher->waitForFinished();});
    auto runScan=[&]{
        if(watcher->isRunning())return;
        error->clear();archive->setEnabled(false);loadBatches();
        const auto period=days->value();const auto guards=protection();const auto path=repo.databasePath();
        if(path==":memory:"){const auto items=repo.cleanupCandidates(period,guards);finishScan({items,repo.lastError(),period});return;}
        scan->setEnabled(false);candidates->setEnabled(false);restore->setEnabled(false);purge->setEnabled(false);retry->setEnabled(false);days->setEnabled(false);showProtected->setEnabled(false);all->setEnabled(false);find->setCursor(Qt::WaitCursor);summary->setText("Scanning the library… Live output continues.");
        watcher->setFuture(QtConcurrent::run([path,period,guards]{LibraryRepository worker;QString message;if(!worker.open(&message,path))return ScanResult{{},message,period};const auto items=worker.cleanupCandidates(period,guards);return ScanResult{items,worker.lastError(),period};}));
    };
    QTimer selectionTimer;selectionTimer.setSingleShot(true);selectionTimer.setInterval(80);QObject::connect(&selectionTimer,&QTimer::timeout,&d,[&]{const auto selected=keys();archive->setEnabled(!watcher->isRunning()&&!selected.isEmpty()&&scannedDays==days->value());if(selected.isEmpty()||watcher->isRunning())return;QVector<CleanupItem> selectedItems;for(int r=0;r<candidates->rowCount();++r)if(candidates->item(r,0)->checkState()==Qt::Checked)selectedItems.append(found.at(candidates->item(r,0)->data(Qt::UserRole).toInt()));const auto plan=repo.estimateCleanup(selectedItems,protection());if(!repo.lastError().isEmpty()){error->setText(repo.lastError());archive->setEnabled(false);return;}summary->setText(QStringLiteral("%1 selected · estimated recoverable file storage: %2 after permanent removal · Recovery frees 0 bytes").arg(selected.size()).arg(bytes(plan.recoverableBytes)));});
    QObject::connect(candidates,&QTableWidget::itemChanged,&d,[&]{selectionTimer.start();});QObject::connect(scan,&QPushButton::clicked,&d,runScan);QObject::connect(showProtected,&QCheckBox::toggled,&d,[&]{fill();});
    QObject::connect(days,&QSpinBox::valueChanged,&d,[&]{archive->setEnabled(false);summary->setText("Period changed. Find Content again before selecting items.");});
    QObject::connect(all,&QPushButton::clicked,&d,[&]{const QSignalBlocker block(candidates);for(int r=0;r<candidates->rowCount();++r)if(found.at(candidates->item(r,0)->data(Qt::UserRole).toInt()).eligible)candidates->item(r,0)->setCheckState(Qt::Checked);selectionTimer.start();});
    QObject::connect(none,&QPushButton::clicked,&d,[&]{const QSignalBlocker block(candidates);for(int r=0;r<candidates->rowCount();++r)candidates->item(r,0)->setCheckState(Qt::Unchecked);archive->setEnabled(false);summary->setText("No items selected.");});
    QObject::connect(archive,&QPushButton::clicked,&d,[&]{if(scannedDays!=days->value())return;const auto selected=keys();const auto plan=repo.previewCleanup(selected,scannedDays,protection());if(!repo.lastError().isEmpty()){error->setText(repo.lastError());return;}if(!review(&d,plan,false))return;QString message;if(repo.archiveCleanup(selected,scannedDays,protection(),&message).isEmpty()){error->setText(message);return;}runScan();tabs->setCurrentIndex(1);});
    QObject::connect(restore,&QPushButton::clicked,&d,[&]{int r=batches->currentRow();if(r<0||r>=saved.size())return;QString message;if(!repo.restoreCleanup(saved.at(r).id,&message)){error->setText(message);return;}runScan();});
    QObject::connect(purge,&QPushButton::clicked,&d,[&]{int r=batches->currentRow();if(r<0||r>=saved.size())return;const auto id=saved.at(r).id;const auto plan=repo.previewCleanupPurge(id,protection());if(!repo.lastError().isEmpty()){error->setText(repo.lastError());return;}if(!review(&d,plan,true))return;QString message;if(!repo.purgeCleanup(id,plan,protection(),&message)){error->setText(message);loadBatches();return;}runScan();});
    QObject::connect(retry,&QPushButton::clicked,&d,[&]{QString message;if(!repo.recoverCleanup(&message)){error->setText(message);return;}runScan();});
    loadBatches();auto *close=new QDialogButtonBox(QDialogButtonBox::Close,&d);layout->addWidget(close);QObject::connect(close,&QDialogButtonBox::rejected,&d,&QDialog::accept);d.exec();
}
