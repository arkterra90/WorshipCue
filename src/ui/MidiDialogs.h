#pragma once
#include "../data/LibraryRepository.h"
class QWidget;
class MidiEngine;
void editMidiSettings(QWidget *, MidiEngine &, LibraryRepository &);
bool editMidiBinding(QWidget *, MidiEngine &, LibraryRepository &, MidiBinding binding = {});
void editMidiSlide(QWidget *, MidiEngine &, LibraryRepository &, MidiOwnerType, qint64, const MidiBinding &);
