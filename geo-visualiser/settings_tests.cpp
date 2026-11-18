#include "appearance_settings.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QProcess>
#include <QFile>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace inspector;
namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
bool colorEqual(const Quantity_Color& a,const Quantity_Color& b) { return a.Red()==b.Red() && a.Green()==b.Green() && a.Blue()==b.Blue(); }
bool equal(const Appearance& a,const Appearance& b) {
    return colorEqual(a.palette.b,b.palette.b) && colorEqual(a.palette.a,b.palette.a) && colorEqual(a.palette.r,b.palette.r) && a.transparency==b.transparency;
}
Appearance custom() {
    Appearance a;
    a.palette.b=Quantity_Color(Quantity_Color::Convert_sRGB_To_LinearRGB(100/255.0),a.palette.b.Green(),a.palette.b.Blue(),Quantity_TOC_RGB);
    a.transparency=0.31;return a;
}
QByteArray bytes(const QString& path) { QFile file(path);if(!file.open(QIODevice::ReadOnly)) return {};return file.readAll(); }
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        if(argc==3 && QString(argv[1])=="--read") {
            QSettings read(QString::fromLocal8Bit(argv[2]),QSettings::IniFormat);
            check(equal(loadAppearance(read),custom()),"fresh process restores exact saved linear colours and transparency");return 0;
        }
        QTemporaryDir directory;check(directory.isValid(),"isolated settings directory");
        const auto path=directory.filePath("appearance.ini");QSettings store(path,QSettings::IniFormat);
        check(equal(loadAppearance(store),Appearance{}),"missing settings use exact factory palette");
        Appearance live;int previews=0;
        const auto apply=[&](const Appearance& a) {live=a;++previews;};
        {
            AppearanceDialog dialog(live,store,apply);
            check(previews==0 && equal(live,Appearance{}),"opening dialog never quantizes factory colours");
            for(const auto* name:{"BRed","BGreen","BBlue","ARed","AGreen","ABlue","RRed","RGreen","RBlue"}) {
                auto* spin=dialog.findChild<QSpinBox*>(name);check(spin && spin->minimum()==0 && spin->maximum()==255,"sRGB channel range");
            }
            check(dialog.findChild<QSpinBox*>("BRed")->value()==199,"factory linear 0.57 is displayed as sRGB 199");
            check(!dialog.findChild<QLabel*>("BSwatch")->styleSheet().isEmpty(),"colour swatch exists");
            dialog.findChild<QSpinBox*>("BRed")->setValue(100);
            check(std::abs(live.palette.b.Red()-Quantity_Color::Convert_sRGB_To_LinearRGB(100/255.0))<1e-7,"UI sRGB converted to linear RGB");
            check(live.palette.b.Green()==factoryPalette.b.Green() && live.palette.b.Blue()==factoryPalette.b.Blue(),"unchanged channels retain exact precision");
            dialog.findChild<QSlider*>("transparency")->setValue(0);check(live.transparency==0,"zero percent is opaque");
            dialog.findChild<QSlider*>("transparency")->setValue(100);check(live.transparency==1,"100 percent transparency");
            dialog.findChild<QPushButton*>("defaultValues")->click();
            check(equal(live,Appearance{}) && live.transparency==0.72,"defaults preview exact factory values");
            check(!QFile::exists(path),"preview/defaults do not save");
            dialog.reject();check(equal(live,Appearance{}),"cancel restores exact factory snapshot");
        }
        {
            AppearanceDialog dialog(live,store,apply);
            dialog.findChild<QSpinBox*>("BRed")->setValue(100);
            dialog.findChild<QSlider*>("transparency")->setValue(31);
            dialog.findChild<QDialogButtonBox*>("appearanceButtons")->button(QDialogButtonBox::Ok)->click();
            check(dialog.result()==QDialog::Accepted && equal(live,custom()),"OK retains preview");
        }
        check(equal(loadAppearance(store),custom()),"OK persists exact values");
        check(store.allKeys().size()==4,"only B/A/R and transparency are stored, no tags or document state");
        QProcess child;child.start(QCoreApplication::applicationFilePath(),{"--read",path});
        check(child.waitForFinished(10000) && child.exitStatus()==QProcess::NormalExit && child.exitCode()==0,"preferences survive fresh process restart");
        const auto saved=bytes(path);
        for(int mode:{0,1,2}) {
            const auto original=live;
            AppearanceDialog dialog(live,store,apply);dialog.show();QApplication::processEvents();
            dialog.findChild<QPushButton*>("defaultValues")->click();
            check(equal(live,Appearance{}),"defaults preview from custom preferences");
            check(bytes(path)==saved,"defaults leaves stored preferences untouched");
            dialog.findChild<QSpinBox*>("AGreen")->setValue(17);
            if(mode==0) dialog.findChild<QDialogButtonBox*>("appearanceButtons")->button(QDialogButtonBox::Cancel)->click();
            if(mode==1) {QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&dialog,&escape);}
            if(mode==2) dialog.close();
            check(equal(live,original) && bytes(path)==saved,"Cancel/Escape/close restores exact snapshot without saving");
        }
        {
            AppearanceDialog dialog(live,store,apply);dialog.accept();
            check(equal(loadAppearance(store),custom()),"opening and accepting without edits never rounds saved values");
        }
        // Invalid preferences cannot inject unsupported channels or opacity into AIS.
        store.setValue("appearance/v1/BLinear",QVariantList{-1,0.5,0.5});
        store.setValue("appearance/v1/ALinear",QVariantList{"nan",0,0});
        store.setValue("appearance/v1/RLinear",QVariantList{0,0});
        store.setValue("appearance/v1/transparency",1.2);
        check(equal(loadAppearance(store),Appearance{}),"invalid settings fall back to factory values");
        std::cout<<"PASS — exact defaults, sRGB boundary, preview, rollback, isolated persistence and restart, malformed settings\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
