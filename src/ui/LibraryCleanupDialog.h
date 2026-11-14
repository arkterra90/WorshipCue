#pragma once
#include "../data/LibraryCleanup.h"
#include <functional>
class QWidget;
class LibraryRepository;
void editLibraryCleanup(QWidget *, LibraryRepository &, std::function<CleanupProtection()> protection);
