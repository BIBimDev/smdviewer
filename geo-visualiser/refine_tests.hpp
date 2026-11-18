#pragma once
#include "window.hpp"
void editSessionTag(QTreeWidget*,QTreeWidgetItem*,const QString&,bool cancel=false,bool remove=false,bool typeShortcuts=false);
void refineUiChecks(inspector::Window&,inspector::Viewport&);
