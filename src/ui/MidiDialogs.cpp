#include "MidiDialogs.h"
#include "../midi/MidiEngine.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
namespace {
QSpinBox *spin(QWidget *parent, int lo, int hi, int value, const char *name = "") {
    auto *s = new QSpinBox(parent); s->setRange(lo,hi); s->setValue(value); s->setObjectName(QString::fromLatin1(name)); return s;
}
QComboBox *types(QWidget *parent, MidiMessageType type) {
    auto *c = new QComboBox(parent);
    for (auto t : {MidiMessageType::NoteOn, MidiMessageType::NoteOff, MidiMessageType::ControlChange}) c->addItem(MidiMessage::typeName(t), int(t));
    c->setCurrentIndex(c->findData(int(type))); return c;
}
QComboBox *ports(QWidget *parent, const MidiEngine &engine, bool input, const QString &selected, bool any = false) {
    auto *c = new QComboBox(parent);
    if (any) c->addItem("Any enabled input", "*");
    for (const auto &p : engine.ports()) if (p.input == input) c->addItem(p.name + (p.available ? "" : " (disconnected)"), p.id);
    if (!selected.isEmpty() && c->findData(selected) < 0) c->addItem(selected + " (saved / disconnected)",selected);
    if(c->findData(selected)>=0)c->setCurrentIndex(c->findData(selected));
    return c;
}
QLabel *hint(QVBoxLayout *layout, const QString &text) {
    auto *label = new QLabel(text); label->setWordWrap(true); layout->addWidget(label); return label;
}
QPushButton *button(QHBoxLayout *row, const QString &text, const char *name = "") {
    auto *b = new QPushButton(text); b->setObjectName(QString::fromLatin1(name)); row->addWidget(b); return b;
}
void refreshBindings(MidiEngine &engine, LibraryRepository &repo) {
    const auto bindings = repo.midiBindings(); QString error = repo.lastError();
    if (!error.isEmpty()) { engine.setBindings({}); engine.record(error); }
    else if (!engine.setBindings(bindings,&error)) engine.record(error);
}
}
bool editMidiBinding(QWidget *parent, MidiEngine &engine, LibraryRepository &repo, MidiBinding binding) {
    QDialog d(parent); d.setWindowTitle(binding.id ? "Edit MIDI Input Mapping" : "Add MIDI Input Mapping"); d.setObjectName("midiBindingDialog"); d.resize(610,600);
    const MidiBinding original = binding;
    auto *layout = new QVBoxLayout(&d); auto *form = new QFormLayout; layout->addLayout(form);
    auto *name = new QLineEdit(binding.name,&d); name->setMaxLength(100); name->setObjectName("midiBindingName"); form->addRow("Name",name);
    auto *enabled = new QCheckBox("Enabled",&d); enabled->setChecked(binding.enabled); form->addRow(enabled);
    auto *source = ports(&d,engine,true,binding.trigger.source,true); source->setObjectName("midiTriggerSource"); form->addRow("Input source",source);
    auto *channel = spin(&d,0,16,binding.trigger.channel,"midiTriggerChannel"); channel->setSpecialValueText("Any enabled channel"); form->addRow("Channel",channel);
    auto *type = types(&d,binding.trigger.type); type->setObjectName("midiTriggerType"); form->addRow("Message",type);
    auto *number = spin(&d,0,127,binding.trigger.number,"midiTriggerNumber"); form->addRow("Note / controller number",number);
    auto *value = spin(&d,-1,127,binding.trigger.value,"midiTriggerValue"); value->setSpecialValueText("Any value"); form->addRow("Velocity / controller value",value);
    auto *learn = new QPushButton("Learn next input message",&d); learn->setObjectName("midiLearn"); form->addRow(learn);
    bool learning = false;
    QObject::connect(learn,&QPushButton::clicked,&d,[&] { learning=true; learn->setText("Listening… (enable the source in Devices first)"); });
    QObject::connect(&engine,&MidiEngine::messageReceived,&d,[&](const QString &id,const MidiMessage &m) {
        if (!learning) return; learning=false;
        if(source->findData(id)<0)source->addItem(id,id); source->setCurrentIndex(source->findData(id));
        channel->setValue(m.channel); type->setCurrentIndex(type->findData(int(m.type))); number->setValue(m.number); value->setValue(m.value); learn->setText("Learn next input message");
    });
    auto *command = new QComboBox(&d); command->setObjectName("midiBindingCommand");
    for(int i=0;i<=int(MidiCommand::ReleaseNotes);++i)command->addItem(MidiBinding::commandName(MidiCommand(i)),i);
    command->setCurrentIndex(command->findData(int(binding.command))); form->addRow("Action",command);
    auto *target = new QComboBox(&d); target->setObjectName("midiBindingTarget");
    auto addTarget = [&](const QString &text,int song,int entry,int asset) { target->addItem(text,QVariantMap{{"song",song},{"entry",entry},{"asset",asset}}); };
    for(const auto &song:repo.songs())addTarget("Library: "+song.title,song.id,0,0);
    for(const auto &asset:repo.mediaAssets())addTarget("Media: "+asset.media.name,0,0,asset.id);
    for(const auto &service:repo.services())for(const auto &entry:repo.service(service.id)) {
        const auto song=repo.entrySource(entry.id); addTarget(service.name+" / "+song.title+QStringLiteral(" [item %1]").arg(entry.id),song.id,entry.id,song.mediaAssetId);
    }
    for(int i=0;i<target->count();++i){const auto t=target->itemData(i).toMap(); if(t["song"].toInt()==binding.songId&&t["entry"].toInt()==binding.entryId&&t["asset"].toInt()==binding.assetId){target->setCurrentIndex(i);break;}}
    form->addRow("Saved target",target);
    auto *page = new QComboBox(&d); page->setObjectName("midiBindingPage"); form->addRow("Source page",page);
    auto *occurrence = spin(&d,1,10000,binding.occurrence+1,"midiBindingOccurrence"); form->addRow("Occurrence of that page",occurrence);
    auto updatePages=[&]{page->clear(); const auto t=target->currentData().toMap(); const auto song=t["entry"].toInt()?repo.entrySource(t["entry"].toInt()):repo.song(t["song"].toInt());
        if(t["asset"].toInt())page->addItem("Media cue",qint64(0));
        else for(int i=0;i<song.slides.size();++i)page->addItem(QStringLiteral("%1 — %2").arg(i+1).arg(song.structure.isEmpty()?song.slides.at(i).simplified().left(65):song.structure.slideLabel(i)),song.slideIds.value(i));
    };
    updatePages(); if(page->findData(binding.slideId)>=0)page->setCurrentIndex(page->findData(binding.slideId));
    QObject::connect(target,&QComboBox::currentIndexChanged,&d,[&]{updatePages();});
    auto updateFields=[&]{const auto c=MidiCommand(command->currentData().toInt()); const bool targeted=c==MidiCommand::TakeSlide||c==MidiCommand::TakePresentation||c==MidiCommand::SelectPresentation; target->setEnabled(targeted); page->setEnabled(c==MidiCommand::TakeSlide); occurrence->setEnabled(c==MidiCommand::TakeSlide);}; updateFields();
    QObject::connect(command,&QComboBox::currentIndexChanged,&d,updateFields);
    hint(layout,"Slide targets follow the source page through reordering. Occurrence 1 is its first appearance in the saved arrangement. Presentation commands require a song or text presentation. MIDI control runs only in Show mode with dialogs closed.");
    auto *error = hint(layout,QString()); error->setObjectName("midiBindingError");
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&d);layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&d,[&]{
        MidiBinding draft; draft.id=binding.id; draft.name=name->text().trimmed(); draft.enabled=enabled->isChecked();
        draft.trigger={source->currentData().toString(),channel->value(),MidiMessageType(type->currentData().toInt()),number->value(),value->value()};draft.command=MidiCommand(command->currentData().toInt());
        if(draft.command==MidiCommand::TakeSlide||draft.command==MidiCommand::TakePresentation||draft.command==MidiCommand::SelectPresentation){const auto t=target->currentData().toMap();draft.songId=t["song"].toInt();draft.entryId=t["entry"].toInt();draft.assetId=t["asset"].toInt();}
        if(draft.command==MidiCommand::TakeSlide){draft.slideId=page->currentData().toLongLong();draft.occurrence=occurrence->value()-1;}
        QString message;if(!repo.saveMidiBinding(draft,&message,binding.id?&original:nullptr)){error->setText(message);return;} refreshBindings(engine,repo); d.accept();
    });
    return d.exec()==QDialog::Accepted;
}
void editMidiSlide(QWidget *parent,MidiEngine &engine,LibraryRepository &repo,MidiOwnerType owner,qint64 id,const MidiBinding &target) {
    const auto original=repo.midiActions(owner,id); if(!repo.lastError().isEmpty()){engine.record(repo.lastError());return;}
    auto actions=original;
    QDialog d(parent);d.setObjectName("midiSlideDialog");d.setWindowTitle("Slide MIDI Actions");d.resize(900,490);auto *layout=new QVBoxLayout(&d);
    hint(layout,owner==MidiOwnerType::Slide?"These output commands belong to this source page and run each time it is taken live, including repeated arrangements. Changes apply on the next take. New split or duplicated pages start without commands.":owner==MidiOwnerType::Entry?"Commands for this media service item run after the Media Library asset’s default commands. Changes apply on the next take.":"Default commands for this media asset run whenever it is taken live. Service items can add their own commands.");
    auto *table=new QTableWidget(0,6,&d);table->setObjectName("midiActionsTable");table->verticalHeader()->setDefaultSectionSize(40);table->setHorizontalHeaderLabels({"On","Destination","Channel","Message","Number","Value"});table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);table->setSelectionBehavior(QAbstractItemView::SelectRows);layout->addWidget(table);
    auto read=[&]{QVector<MidiOutputAction> result;for(int i=0;i<table->rowCount();++i){MidiOutputAction a;a.enabled=qobject_cast<QCheckBox*>(table->cellWidget(i,0))->isChecked();a.destination=qobject_cast<QComboBox*>(table->cellWidget(i,1))->currentData().toString();a.message={MidiMessageType(qobject_cast<QComboBox*>(table->cellWidget(i,3))->currentData().toInt()),qobject_cast<QSpinBox*>(table->cellWidget(i,2))->value(),qobject_cast<QSpinBox*>(table->cellWidget(i,4))->value(),qobject_cast<QSpinBox*>(table->cellWidget(i,5))->value()};result.append(a);}return result;};
    auto fill=[&]{table->setRowCount(0);for(const auto &a:actions){int row=table->rowCount();table->insertRow(row);auto *on=new QCheckBox;on->setChecked(a.enabled);table->setCellWidget(row,0,on);table->setCellWidget(row,1,ports(table,engine,false,a.destination));table->setCellWidget(row,2,spin(table,1,16,a.message.channel));table->setCellWidget(row,3,types(table,a.message.type));table->setCellWidget(row,4,spin(table,0,127,a.message.number));table->setCellWidget(row,5,spin(table,0,127,a.message.value));}};fill();
    auto *row=new QHBoxLayout;layout->addLayout(row);
    QObject::connect(button(row,"Add","midiAddAction"),&QPushButton::clicked,&d,[&]{actions=read();if(actions.size()>=64)return;MidiOutputAction a;for(const auto &p:engine.ports())if(!p.input){a.destination=p.id;a.message.channel=engine.outputChannel(p.id);break;}actions.append(a);fill();table->setCurrentCell(table->rowCount()-1,1);});
    QObject::connect(button(row,"Remove"),&QPushButton::clicked,&d,[&]{int r=table->currentRow();actions=read();if(r>=0){actions.removeAt(r);fill();}});
    for(int delta:{-1,1})QObject::connect(button(row,delta<0?"Earlier":"Later"),&QPushButton::clicked,&d,[&,delta]{int r=table->currentRow();actions=read();if(r>=0&&r+delta>=0&&r+delta<actions.size()){actions.move(r,r+delta);fill();table->setCurrentCell(r+delta,1);}});
    QObject::connect(button(row,"Assign Input Trigger…","midiAssignTrigger"),&QPushButton::clicked,&d,[&]{editMidiBinding(&d,engine,repo,target);});
    auto *error=hint(layout,QString());error->setObjectName("midiActionsError");
    hint(layout,"Input mappings are saved separately when you press Save in the mapping editor. Output commands send only to enabled, connected destinations; disconnected cues are skipped without replay.");
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&d);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&d,[&]{QString message;if(!repo.setMidiActions(owner,id,original,read(),&message)){error->setText(message);return;}d.accept();});d.exec();
}
void editMidiSettings(QWidget *parent,MidiEngine &engine,LibraryRepository &repo) {
    QDialog d(parent);d.setObjectName("midiSettingsDialog");d.setWindowTitle("MIDI Devices / Mappings");d.resize(960,700);auto *layout=new QVBoxLayout(&d);auto *tabs=new QTabWidget(&d);layout->addWidget(tabs);
    auto *devices=new QWidget;auto *dl=new QVBoxLayout(devices);tabs->addTab(devices,"Devices");
    const auto initial=engine.settings();
    auto *enabled=new QCheckBox("Enable MIDI input and output",devices);enabled->setObjectName("midiEnabled");enabled->setChecked(initial.enabled);dl->addWidget(enabled);
    auto *automatic=new QCheckBox("Automatically reconnect saved devices",devices);automatic->setChecked(initial.autoReconnect);dl->addWidget(automatic);
    auto *virtualPorts=new QCheckBox("Publish WorshipCue virtual Input / Output ports (macOS)",devices);virtualPorts->setEnabled(engine.supportsVirtualPorts());virtualPorts->setChecked(initial.virtualPorts);dl->addWidget(virtualPorts);
    auto *form=new QFormLayout;dl->addLayout(form);auto *echo=spin(devices,0,5000,initial.echoWindowMs);auto *interval=spin(devices,0,5000,initial.minimumIntervalMs);echo->setSuffix(" ms");interval->setSuffix(" ms");form->addRow("Suppress returned output messages",echo);form->addRow("Minimum interval per input mapping",interval);
    auto *table=new QTableWidget(0,5,devices);table->setObjectName("midiDevicesTable");table->verticalHeader()->setDefaultSectionSize(40);table->setHorizontalHeaderLabels({"Enabled","Direction","Device","Channel","Status"});table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);dl->addWidget(table);
    auto collect=[&]{MidiSettings s;s.enabled=enabled->isChecked();s.autoReconnect=automatic->isChecked();s.virtualPorts=virtualPorts->isChecked();s.echoWindowMs=echo->value();s.minimumIntervalMs=interval->value();for(int r=0;r<table->rowCount();++r){MidiDeviceSetting setting;setting.id=table->item(r,2)->data(Qt::UserRole).toString();setting.enabled=qobject_cast<QCheckBox*>(table->cellWidget(r,0))->isChecked();setting.channel=qobject_cast<QSpinBox*>(table->cellWidget(r,3))->value();(table->item(r,1)->data(Qt::UserRole).toBool()?s.inputs:s.outputs).append(setting);}return s;};
    auto fill=[&](const MidiSettings &s){table->setRowCount(0);QVector<MidiPort> all=engine.ports();for(bool input:{true,false})for(const auto &saved:input?s.inputs:s.outputs){bool found=false;for(const auto &p:all)if(p.input==input&&p.id==saved.id)found=true;if(!found)all.append({saved.id,saved.id,input,false,"Saved device is disconnected"});}
        for(const auto &p:all){int r=table->rowCount();table->insertRow(r);MidiDeviceSetting setting{p.id,false,p.input?0:1};for(const auto &saved:p.input?s.inputs:s.outputs)if(saved.id==p.id)setting=saved;auto *on=new QCheckBox;on->setChecked(setting.enabled);table->setCellWidget(r,0,on);auto *direction=new QTableWidgetItem(p.input?"Input":"Output");direction->setData(Qt::UserRole,p.input);direction->setFlags(direction->flags()&~Qt::ItemIsEditable);table->setItem(r,1,direction);auto *name=new QTableWidgetItem(p.name);name->setData(Qt::UserRole,p.id);name->setToolTip(p.id+"\n"+p.detail);name->setFlags(name->flags()&~Qt::ItemIsEditable);table->setItem(r,2,name);auto *channel=spin(table,p.input?0:1,16,setting.channel);if(p.input)channel->setSpecialValueText("Any");table->setCellWidget(r,3,channel);auto *status=new QTableWidgetItem(engine.portStatus(p.id,p.input));status->setFlags(status->flags()&~Qt::ItemIsEditable);table->setItem(r,4,status);}};
    engine.refreshPorts();fill(initial);
    auto *row=new QHBoxLayout;dl->addLayout(row);auto *error=hint(dl,QString());error->setObjectName("midiSettingsError");
    QObject::connect(button(row,"Refresh Devices","midiRefresh"),&QPushButton::clicked,&d,[&]{const auto s=collect();engine.refreshPorts();fill(s);});
    QObject::connect(button(row,"Apply Device Settings","midiApply"),&QPushButton::clicked,&d,[&]{QString message;if(!engine.setSettings(collect(),&message)){error->setText(message);return;}error->setText("Device settings saved. Inputs remain monitored while this dialog is open; control resumes when it closes.");fill(engine.settings());});
    QObject::connect(button(row,"Release WorshipCue Notes"),&QPushButton::clicked,&engine,&MidiEngine::releaseNotes);
    hint(dl,"Input channels filter reception. Output channels provide the default for new slide commands; each command saves its own channel. Repeated held notes and unchanged CC values are ignored. A release or changed value rearms the trigger. A returned message with the same channel and number is suppressed during the echo window (0 disables it).");
    hint(dl,engine.setupHelp());
    auto *links=hint(dl,"<a href=\"https://support.renewedvision.com/hc/en-us/articles/4412376777491-Devices-MIDI-System-Setup\">OS virtual / network MIDI setup reference</a>");links->setOpenExternalLinks(true);
    auto *mappingPage=new QWidget;auto *ml=new QVBoxLayout(mappingPage);tabs->addTab(mappingPage,"Input Mappings");hint(ml,"One enabled mapping per overlapping input trigger. Disabled mappings may overlap. Targets use stable source pages; service targets use their saved order. Changes save immediately.");auto *maps=new QTableWidget(0,5,mappingPage);maps->setObjectName("midiMappingsTable");maps->setHorizontalHeaderLabels({"On","Name","Input","Action","Target"});maps->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);maps->setSelectionBehavior(QAbstractItemView::SelectRows);maps->setEditTriggers(QAbstractItemView::NoEditTriggers);ml->addWidget(maps);
    QVector<MidiBinding> bindings;
    auto loadMaps=[&]{bindings=repo.midiBindings();maps->setRowCount(bindings.size());for(int r=0;r<bindings.size();++r){const auto &b=bindings.at(r);const auto t=b.trigger;QString input=QStringLiteral("%1 / ch %2 / %3 %4 = %5").arg(t.source).arg(t.channel?QString::number(t.channel):"Any").arg(MidiMessage::typeName(t.type)).arg(t.number).arg(t.value<0?"Any":QString::number(t.value));QString target;if(b.songId)target=repo.song(b.songId).title;if(b.assetId)target=repo.mediaAsset(b.assetId).media.name;if(b.entryId)target+=QStringLiteral(" / item %1").arg(b.entryId);if(b.slideId)target+=QStringLiteral(" / page ID %1 / occurrence %2").arg(b.slideId).arg(b.occurrence+1);const QStringList values={b.enabled?"Yes":"No",b.name,input,MidiBinding::commandName(b.command),target};for(int c=0;c<values.size();++c)maps->setItem(r,c,new QTableWidgetItem(values.at(c)));}};loadMaps();auto *mr=new QHBoxLayout;ml->addLayout(mr);
    QObject::connect(button(mr,"Add Mapping…","midiAddMapping"),&QPushButton::clicked,&d,[&]{editMidiBinding(&d,engine,repo);loadMaps();});
    auto edit=[&]{int r=maps->currentRow();if(r>=0&&r<bindings.size()){editMidiBinding(&d,engine,repo,bindings.at(r));loadMaps();}};QObject::connect(button(mr,"Edit…"),&QPushButton::clicked,&d,edit);QObject::connect(maps,&QTableWidget::cellDoubleClicked,&d,[&](int,int){edit();});
    auto *mapError=hint(ml,QString());QObject::connect(button(mr,"Remove"),&QPushButton::clicked,&d,[&]{int r=maps->currentRow();if(r<0||r>=bindings.size())return;QString message;if(!repo.deleteMidiBinding(bindings.at(r).id,&message)){mapError->setText(message);return;}refreshBindings(engine,repo);loadMaps();});
    auto *monitor=new QWidget;auto *al=new QVBoxLayout(monitor);tabs->addTab(monitor,"Activity");hint(al,"Receive, transmit, ignored input, mapping and device errors. Latest 500 events. No input commands execute while a dialog is open.");auto *activity=new QPlainTextEdit(monitor);activity->setObjectName("midiActivity");activity->setReadOnly(true);al->addWidget(activity);QString previous;
    QTimer timer;timer.setInterval(150);QObject::connect(&timer,&QTimer::timeout,&d,[&]{const auto text=engine.activity().join('\n');if(text!=previous){previous=text;activity->setPlainText(text);activity->moveCursor(QTextCursor::End);}for(int r=0;r<table->rowCount();++r)table->item(r,4)->setText(engine.portStatus(table->item(r,2)->data(Qt::UserRole).toString(),table->item(r,1)->data(Qt::UserRole).toBool()));});timer.start();
    auto *close=new QDialogButtonBox(QDialogButtonBox::Close,&d);layout->addWidget(close);QObject::connect(close,&QDialogButtonBox::rejected,&d,&QDialog::accept);d.exec();
}
