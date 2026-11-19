#pragma once
#include "window.hpp"
#include <functional>
QString editSessionTag(QTreeWidget*,QTreeWidgetItem*,const QString&,bool cancel=false,bool remove=false,bool typeShortcuts=false,
                       const std::function<void()>& beforeAccept={});
void refineUiChecks(inspector::Window&,inspector::Viewport&);
