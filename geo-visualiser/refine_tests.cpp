#include "refine_tests.hpp"
#include "appearance_settings.hpp"
#include <QApplication>
#include <QMenu>
#include <QInputDialog>
#include <QKeyEvent>
#include <QTimer>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QToolButton>
#include <QLineEdit>
#include <QDir>
#include <stdexcept>
#include <iostream>

namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void key(QWidget* widget,int code) {
    QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier,QString(QChar(code).toLower()));QApplication::sendEvent(widget,&event);
}
bool equal(const inspector::Appearance& a,const inspector::Appearance& b) {
    const auto same=[](const Quantity_Color& x,const Quantity_Color& y) {return x.Red()==y.Red() && x.Green()==y.Green() && x.Blue()==y.Blue();};
    return same(a.palette.b,b.palette.b) && same(a.palette.a,b.palette.a) && same(a.palette.r,b.palette.r) && a.transparency==b.transparency;
}
bool cameraEqual(const auto& a,const auto& b) {
    return a->ProjectionType()==b->ProjectionType() && a->Eye().Distance(b->Eye())==0 &&
        a->Center().Distance(b->Center())==0 && a->Up().IsEqual(b->Up(),0) && a->Direction().IsEqual(b->Direction(),0) &&
        a->Scale()==b->Scale() && a->Aspect()==b->Aspect() && a->ZNear()==b->ZNear() && a->ZFar()==b->ZFar();
}
}
void editSessionTag(QTreeWidget* tree,QTreeWidgetItem* row,const QString& value,bool cancel,bool remove,bool typeShortcuts) {
    tree->scrollToItem(row);QApplication::processEvents();
    bool menuSeen=false,dialogSeen=false;
    QTimer driver;driver.setInterval(10);
    QObject::connect(&driver,&QTimer::timeout,[&] {
        if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());menu && !menuSeen) {
            menuSeen=true;
            auto* action=menu->findChild<QAction*>(remove?"removeTagAction":"editTagAction");
            check(action,"expected entity tag context action");menu->setActiveAction(action);key(menu,Qt::Key_Return);
        } else if(auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());dialog && !dialogSeen) {
            dialogSeen=true;driver.stop();
            if(typeShortcuts) {
                auto* editor=dialog->findChild<QLineEdit*>();editor->setFocus();editor->clear();
                for(int code:{Qt::Key_S,Qt::Key_B,Qt::Key_A,Qt::Key_R,Qt::Key_P}) key(editor,code);
                check(editor->text()=="sbarp","dialog accepts shortcut letters as normal tag text");
            } else dialog->setTextValue(value);
            if(cancel) dialog->reject();else dialog->accept();
        }
    });
    QTimer deadline;deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,[&] {
        if(auto* popup=QApplication::activePopupWidget()) popup->close();
        if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
    });
    deadline.start(5000);driver.start();
    tree->customContextMenuRequested(tree->visualItemRect(row).center());driver.stop();
    check(menuSeen && (remove || dialogSeen),"context action and tag editor exercised");
}
void refineUiChecks(inspector::Window& window,inspector::Viewport& viewport) {
    using namespace inspector;
    auto* tree=window.findChild<QTreeWidget*>("entities");
    auto* reps=window.findChild<QComboBox*>("representation");
    auto* isolate=window.findChild<QComboBox*>("component");
    auto* style=window.findChild<QComboBox*>("displayStyle");
    check(window.findChild<QToolButton*>("mainMenu") && !window.findChild<QLineEdit*>("tag") &&
          !window.findChild<QPushButton*>("saveTag"),"menu replaces persistent tag editor");
    auto* first=tree->topLevelItem(0)->child(0);auto* second=tree->topLevelItem(0)->child(1);
    tree->setCurrentItem(first);
    editSessionTag(tree,second,"invoked row");
    check(first->text(1).isEmpty() && second->text(1)=="invoked row" && tree->currentItem()==first,"tag targets invoked row, not selection");
    const auto before=viewport.camera();const auto selected=viewport.selectedId();
    editSessionTag(tree,second,"cancelled",true,false,true);
    check(second->text(1)=="invoked row" && viewport.selectedId()==selected && cameraEqual(before,viewport.camera()) && reps->currentIndex()==1,
          "cancel tag edit preserves value and typing cannot navigate");
    editSessionTag(tree,second,"",false,true);check(second->text(1).isEmpty(),"remove targets invoked row");
    bool unexpectedMenu=false;
    QTimer closeUnexpected;closeUnexpected.setInterval(10);
    QObject::connect(&closeUnexpected,&QTimer::timeout,[&] {
        if(auto* menu=QApplication::activePopupWidget()) {unexpectedMenu=true;menu->close();}
    });
    closeUnexpected.start();tree->customContextMenuRequested(tree->visualItemRect(tree->topLevelItem(0)).center());
    tree->customContextMenuRequested(QPoint(-5,-5));closeUnexpected.stop();
    check(!unexpectedMenu,"group and blank space offer no tag actions");
    reps->setCurrentIndex(2);isolate->setCurrentIndex(1);
    for(QTreeWidgetItemIterator it(tree);*it;++it) if((*it)->data(0,Qt::UserRole).toString().contains("/Face/")) {tree->setCurrentItem(*it);break;}
    auto* row=tree->currentItem();const auto id=viewport.selectedId();
    auto* settings=window.findChild<QAction*>("settingsAction");check(settings,"Settings action exists");
    for(int mode:{0,1,2}) {
        style->setCurrentIndex(mode);const auto camera=viewport.camera();const auto original=viewport.appearance();
        bool visited=false;
        QTimer::singleShot(0,[&] {
            auto* dialog=dynamic_cast<AppearanceDialog*>(QApplication::activeModalWidget());check(dialog,"Settings opens modal dialog");visited=true;
            dialog->grab().save(QDir::tempPath()+"/geo-vis-step3-settings.png");
            dialog->findChild<QSpinBox*>("BRed")->setValue(45);
            dialog->findChild<QSlider*>("transparency")->setValue(31);
            check(!equal(original,viewport.appearance()),"Settings live preview reaches viewport");
            check(style->currentIndex()==mode && isolate->currentIndex()==1 && reps->currentIndex()==2 &&
                  tree->currentItem()==row && viewport.selectedId()==id && cameraEqual(camera,viewport.camera()),"preview preserves viewer state");
            auto* spin=dialog->findChild<QSpinBox*>("ARed");spin->setFocus();
            for(int code:{Qt::Key_S,Qt::Key_B,Qt::Key_A,Qt::Key_R,Qt::Key_P}) key(spin,code);
            check(reps->currentIndex()==2 && cameraEqual(camera,viewport.camera()),"Settings typing cannot navigate");
            dialog->findChild<QPushButton*>("defaultValues")->click();
            check(equal(viewport.appearance(),Appearance{}),"Default values previews exact factory appearance");
            dialog->findChild<QSlider*>("transparency")->setValue(22);
            if(mode==0) dialog->findChild<QDialogButtonBox*>("appearanceButtons")->button(QDialogButtonBox::Cancel)->click();
            if(mode==1) key(dialog,Qt::Key_Escape);
            if(mode==2) dialog->close();
        });
        settings->trigger();check(visited && equal(viewport.appearance(),original),"Cancel/Escape/close restores exact opening appearance");
        check(viewport.selectedId()==id && cameraEqual(camera,viewport.camera()),"rollback preserves selected owner and camera");
    }
    bool helpSeen=false;
    QTimer::singleShot(0,[&] {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());check(dialog && dialog->objectName()=="helpDialog","Help opens");
        const auto text=dialog->findChild<QTextBrowser*>("helpText")->toPlainText();
        for(const auto* phrase:{"File → Open SMD","Ctrl+O","List of Elements","Properties","bottom-left status",
            "Settings","Show context","S:","B:","A:","R:","P toggles","Orientation cube","left drag","right/middle drag",
            "wheel","picks","raw source-local millimetres","at target","camera target plane","Session tags","canonical file path",
            "not SMD metadata","GEO-VIS-REFINE-04","OCCT: 8.0.1"})
            check(text.contains(phrase),phrase);
        dialog->grab().save(QDir::tempPath()+"/geo-vis-step3-help.png");
        helpSeen=true;dialog->accept();
    });
    window.findChild<QAction*>("helpAction")->trigger();check(helpSeen,"Help and About verified");
    style->setCurrentIndex(0);reps->setCurrentIndex(1);viewport.resetCamera();
    std::cout<<"PASS — menu/Help, settings rollback and state preservation, context targeting and dialog shortcuts\n";
}
