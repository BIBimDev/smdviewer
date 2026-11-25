#include "export_tests.hpp"
#include "refine_tests.hpp"
#include "window.hpp"
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace {
using namespace inspector;
namespace fs=std::filesystem;

struct Specimen {
    const char* filename;
    int additions, reductions;
};
constexpr Specimen specimens[]={
    {"COL_PLAIN.smd",0,0},
    {"COL_SINGLE-POCKET.smd",0,1},
    {"COL_PLAIN_SINGLE-CORBEL.smd",1,0},
    {"COL_PLAIN_HALF-CORBEL.smd",1,0},
    {"COL_SINGLE-POCKET_SINGLE-CORBEL_02.smd",1,1},
};

void require(bool condition,const std::string& diagnostic) {
    if(!condition) throw std::runtime_error(diagnostic);
}
QString qtPath(const fs::path& path) { return QString::fromStdU16String(path.u16string()); }
fs::path nativePath(const QString& path) { return fs::path(path.toStdU16String()); }

void qualify(const fs::path& fixture,const fs::path& output,const Specimen& specimen,const QString& qualifier) {
    // Use relative ASCII arguments so the existing verifier also works under
    // Windows when the temporary directory or exported filename has Unicode.
    QTemporaryDir input;
    require(input.isValid(),"temporary qualification directory");
    const auto root=nativePath(input.path());
    fs::copy_file(fixture,root/"reference.smd");
    fs::copy_file(output,root/"export.json");
    QProcess verify;
    verify.setWorkingDirectory(input.path());
    verify.setProcessChannelMode(QProcess::MergedChannels);
    verify.start(qualifier,{"reference.smd",QString::number(specimen.additions),
                            QString::number(specimen.reductions),"export.json"});
    const bool finished=verify.waitForFinished(30000);
    const auto diagnostic=verify.readAll().toStdString();
    require(finished && verify.exitStatus()==QProcess::NormalExit && verify.exitCode()==0,
            std::string(specimen.filename)+": qualification failed: "+verify.errorString().toStdString()+"\n"+diagnostic);
    std::cout<<specimen.filename<<" | "<<diagnostic;
}

using ModalStep=std::function<void(QDialog&)>;

void drive(QAction& action,std::initializer_list<ModalStep> steps) {
    auto step=steps.begin();
    std::string failure;
    QTimer driver;
    driver.setInterval(20);
    QObject::connect(&driver,&QTimer::timeout,[&] {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog) return;
        driver.stop();
        try {
            require(step!=steps.end(),"unexpected modal dialog after "+std::to_string(steps.size())+
                    " step(s) of "+action.text().toStdString()+": "+dialog->windowTitle().toStdString());
            (*step++)(*dialog);
        } catch(const std::exception& error) {
            if(failure.empty()) failure=error.what();
            dialog->reject();
        }
        driver.start();
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,[&] {
        failure="timed out waiting for "+action.text().toStdString()+" dialog sequence";
        if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
    });
    deadline.start(10000);
    driver.start();
    action.trigger();
    require(failure.empty(),failure);
    require(step==steps.end(),"missing modal dialog for "+action.text().toStdString());
}

QFileDialog& fileDialog(QDialog& modal,const QString& name) {
    auto* dialog=qobject_cast<QFileDialog*>(&modal);
    require(dialog && dialog->objectName()==name,"expected file dialog: "+name.toStdString());
    return *dialog;
}

void chooseFile(QFileDialog& dialog,const fs::path& path) {
    auto* editor=dialog.findChild<QLineEdit*>("fileNameEdit");
    require(editor,"file dialog has a filename editor");
    editor->setFocus();
    editor->selectAll();
    editor->insert(qtPath(path));
    require(dialog.selectedFiles().size()==1 && nativePath(dialog.selectedFiles().front())==path,
            "dialog did not select "+pathText(path)+"; selected "+dialog.selectedFiles().join(", ").toStdString());
    QMetaObject::invokeMethod(&dialog,"accept",Qt::DirectConnection);
    require(!dialog.isVisible(),"dialog did not accept "+pathText(path)+
            " | directory="+dialog.directory().absolutePath().toStdString()+
            " | selected="+dialog.selectedFiles().join(", ").toStdString());
}

ModalStep message(const QString& title,const QString& text,QMessageBox::Icon icon,QString* diagnostic=nullptr) {
    return [=](QDialog& modal) {
        auto* box=qobject_cast<QMessageBox*>(&modal);
        require(box && box->windowTitle()==title && box->textFormat()==Qt::PlainText && box->icon()==icon,
                "expected plain-text notification: "+title.toStdString()+"; saw "+modal.windowTitle().toStdString()+
                (box ? ": "+box->text().toStdString() : ""));
        require(box->text().contains(text),"notification contains expected path or diagnostic: "+text.toStdString());
        if(diagnostic) *diagnostic=box->text();
        box->accept();
    };
}

bool sameCamera(const Graphic3d_Camera& a,const Graphic3d_Camera& b) {
    return a.ProjectionType()==b.ProjectionType() && a.Eye().Distance(b.Eye())==0 &&
        a.Center().Distance(b.Center())==0 && a.Up().IsEqual(b.Up(),0) &&
        a.Direction().IsEqual(b.Direction(),0) && a.Scale()==b.Scale() &&
        a.Aspect()==b.Aspect() && a.ZNear()==b.ZNear() && a.ZFar()==b.ZFar();
}
} // namespace

void exportAdapterChecks(const fs::path& samples,const QString& qualifier) {
    for(const auto& specimen:specimens) {
        QTemporaryDir directory;
        require(directory.isValid(),"temporary adapter export directory");
        const auto output=nativePath(directory.path())/"element.json";
        const auto fixture=samples/"Columns"/specimen.filename;
        const auto document=loadDocument(fixture);
        exportDocument(*document,output);
        require(fs::is_regular_file(output) && fs::file_size(output)>0 &&
                std::distance(fs::directory_iterator(output.parent_path()),fs::directory_iterator{})==1,
                "viewer adapter writes one standalone JSON/BREP file");
        qualify(fixture,output,specimen,qualifier);
    }
}

void exportUiChecks(const fs::path& samples,const QString& qualifier) {
    QTemporaryDir directory;
    require(directory.isValid(),"temporary project directory");
    const auto root=nativePath(directory.path());
    const auto defaultDir=root/"project"/"exports";
    const auto other=root/fs::path(u"other destination ü 柱");
    fs::create_directories(other);
    struct RestoreCwd {
        QString path=QDir::currentPath();
        ~RestoreCwd() { QDir::setCurrent(path); }
    } restoreCwd;
    require(QDir::setCurrent(qtPath(other)),"switch to unrelated working directory");
    const auto sampleDir=fs::absolute(samples.is_absolute()?samples:nativePath(restoreCwd.path)/samples);
    Window window(sampleDir,defaultDir);
    auto* action=window.findChild<QAction*>("exportAction");
    auto* open=window.findChild<QAction*>("openSmd");
    auto* menu=window.findChild<QMenu*>("fileMenu");
    auto* status=window.findChild<QLabel*>("status");
    auto* projection=window.findChild<QLabel*>("projection");
    auto* tree=window.findChild<QTreeWidget*>("entities");
    auto* details=window.findChild<QPlainTextEdit*>("details");
    auto* reps=window.findChild<QComboBox*>("representation");
    auto* isolate=window.findChild<QComboBox*>("component");
    auto* pick=window.findChild<QComboBox*>("pick");
    auto* style=window.findChild<QComboBox*>("displayStyle");
    Viewport* viewport=nullptr;
    for(auto* child:window.findChildren<QWidget*>())
        if(auto* canvas=dynamic_cast<Viewport*>(child)) viewport=canvas;
    require(action && open && menu && status && projection && tree && details && reps &&
            isolate && pick && style && viewport,"expected File menu, workspace and viewport");
    require(menu->actions().contains(action) && action->text()==QStringLiteral("Export\u2026") && !action->isEnabled(),
            "File → Export… exists and is disabled before loading");
    drive(*action,{});
    require(!fs::exists(defaultDir),"disabled export creates no directory");

    window.show();
    QApplication::processEvents();
    require(viewport->ready() && viewport->error().empty() && action->isEnabled(),
            "plain document loads and enables export: "+viewport->error());
    require(!fs::exists(defaultDir),"startup creates no export directory");
    reps->setCurrentIndex(static_cast<int>(Representation::Extensions));
    require(action->isEnabled() && isolate->count()==1,"an empty A collection remains exportable");
    reps->setCurrentIndex(static_cast<int>(Representation::Reductions));
    require(action->isEnabled() && isolate->count()==1,"an empty R collection remains exportable");

    const auto checkSave=[&](QDialog& modal,const fs::path& proposed,const fs::path& start) -> QFileDialog& {
        auto& dialog=fileDialog(modal,"exportGeometryDialog");
        require(dialog.windowTitle()=="Export geometry" && dialog.acceptMode()==QFileDialog::AcceptSave &&
                dialog.nameFilters()==QStringList{"GEO-Export JSON (*.json)"} &&
                dialog.testOption(QFileDialog::DontConfirmOverwrite),
                "save dialog has the expected title, filter and overwrite behavior");
        require(nativePath(dialog.directory().absolutePath())==start &&
                dialog.selectedFiles().size()==1 && nativePath(dialog.selectedFiles().front()).filename()==proposed,
                "save dialog proposes "+pathText(start/proposed)+"; selected "+dialog.selectedFiles().join(", ").toStdString());
        return dialog;
    };

    drive(*action,{[&](QDialog& modal) {
        auto& dialog=checkSave(modal,"COL_PLAIN.json",defaultDir);
        require(fs::is_directory(defaultDir) && !fs::exists(other/"exports"),"default directory created on demand outside cwd");
        dialog.reject();
    }});
    require(fs::is_empty(defaultDir) && action->isEnabled(),"cancelling export leaves the active document and no file");

    const auto& specimen=specimens[4];
    const auto reference=sampleDir/"Columns"/specimen.filename;
    const auto source=root/fs::path(u"active sample ü 柱.final.smd");
    auto proposed=source.filename();
    proposed.replace_extension(".json");
    fs::copy_file(reference,source);
    const auto bad=root/"unsupported input.smd";
    std::ofstream(bad)<<"unsupported SMD";
    const auto openStep=[&](const fs::path& file) -> ModalStep {
        return [&,file](QDialog& modal) { chooseFile(fileDialog(modal,"openSmdDialog"),file); };
    };
    drive(*open,{openStep(source)});
    require(action->isEnabled() && window.windowTitle().endsWith(qtPath(source.filename())),"combined Unicode specimen is active");

    reps->setCurrentIndex(static_cast<int>(Representation::Extensions));
    require(action->isEnabled() && isolate->count()==2,"combined specimen has one displayed extension");
    isolate->setCurrentIndex(1);
    style->setCurrentIndex(2);
    viewport->orient(V3d_XposYnegZpos);
    viewport->toggleProjection();
    auto appearance=viewport->appearance();
    appearance.showContext=false;
    appearance.transparency=0.43;
    viewport->setAppearance(appearance);
    auto* row=tree->topLevelItem(0)->child(0);
    tree->setCurrentItem(row);
    const auto selected=viewport->selectedId();
    const QString tag="retained export test tag";
    require(!selected.empty() && editSessionTag(tree,row,tag).isEmpty(),"isolated component has a selected and tagged entity");
    const auto camera=viewport->camera();
    const auto state=[&] {
        const auto current=viewport->appearance();
        return std::tuple(window.windowTitle(),status->text(),status->toolTip(),projection->text(),details->toPlainText(),
                          reps->currentIndex(),isolate->currentIndex(),pick->currentIndex(),style->currentIndex(),
                          tree->currentItem(),viewport->selectedId(),current.showContext,current.transparency);
    };
    const auto before=state();
    QSettings preferences(QSettings::IniFormat,QSettings::UserScope,"BIBimDev","GEO-Visualiser");
    const auto savedKeys=preferences.allKeys();
    const auto verifyState=[&] {
        require(action->isEnabled() && state()==before && sameCamera(*camera,*viewport->camera()),
                "viewer preserves title, active path, load status, representation, isolation, selection, Properties, camera and appearance");
        require(editSessionTag(tree,row,{},true)==tag && preferences.allKeys()==savedKeys,
                "viewer preserves session tag and settings");
    };

    drive(*open,{openStep(bad),message("Open SMD failed",qtPath(bad),QMessageBox::Warning)});
    verifyState();
    // The original on-disk path is unavailable during all subsequent exports.
    // Success must come from the active document's already-computed bodies.
    fs::remove(source);

    const auto existing=other/"existing.json";
    std::ofstream(existing)<<"keep this content";
    const auto output=other/fs::path(u"chosen output ü 柱.json");
    drive(*action,{[&](QDialog& modal) {
        chooseFile(checkSave(modal,proposed,defaultDir),existing);
    },message("Export destination already exists",QDir::toNativeSeparators(qtPath(existing)),QMessageBox::Warning),
    [&](QDialog& modal) {
        chooseFile(fileDialog(modal,"exportGeometryDialog"),output);
    },message("Geometry exported",QDir::toNativeSeparators(qtPath(output)),QMessageBox::Information)});
    std::ifstream retained(existing);
    std::string contents;
    std::getline(retained,contents);
    require(contents=="keep this content" && fs::is_regular_file(output),"existing file preserved and alternate export created");
    qualify(reference,output,specimen,qualifier);
    verifyState();

    const auto removedDir=root/"removed directory";
    fs::create_directory(removedDir);
    const auto unavailable=removedDir/"unavailable.json";
    QString failure;
    drive(*action,{[&](QDialog& modal) {
        auto& dialog=checkSave(modal,proposed,defaultDir);
        QObject::connect(&dialog,&QDialog::accepted,&dialog,[&] {
            std::error_code ignored;
            fs::remove(removedDir,ignored);
        });
        chooseFile(dialog,unavailable);
    },message("Export failed",QDir::toNativeSeparators(qtPath(unavailable)),QMessageBox::Critical,&failure)});
    require(!fs::exists(removedDir) && failure.section('\n',1).size()>0,
            "write failure identifies destination and includes an exporter diagnostic");
    verifyState();

    fs::remove(defaultDir);
    std::ofstream(defaultDir)<<"blocks directory creation";
    drive(*action,{message("Default export directory unavailable",QDir::toNativeSeparators(qtPath(defaultDir)),QMessageBox::Warning),
                   [&](QDialog& modal) { checkSave(modal,proposed,source.parent_path()).reject(); }});
    require(fs::is_regular_file(defaultDir),"failed default directory creation preserves existing contents");
    verifyState();
    fs::remove(defaultDir);

    const auto stale=other/"stale.json";
    drive(*action,{[&](QDialog& modal) {
        auto& dialog=checkSave(modal,proposed,defaultDir);
        viewport->failed("Injected fatal viewport error during export dialog");
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        require(!action->isEnabled() && window.windowTitle()=="GEO-Visualiser" && tree->topLevelItemCount()==0,
                "fatal cleanup clears the active document and disables export");
        chooseFile(dialog,stale);
    }});
    require(!fs::exists(stale) && fs::is_empty(defaultDir),"a stale dialog cannot export the cleared document");
    drive(*action,{});
    std::cout<<"PASS — File export availability, default and alternate destinations, cancellation, no overwrite, "
                "failure diagnostics, state preservation, isolated-component export and stale-dialog cleanup\n";
}
