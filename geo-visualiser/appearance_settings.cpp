#include "appearance_settings.hpp"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QMessageBox>
#include <algorithm>
#include <cmath>

namespace inspector {
namespace {
Quantity_Color& color(Appearance& a,int i) { return i==0?a.palette.b:i==1?a.palette.a:a.palette.r; }
const char* names[]={"B","A","R"};
constexpr auto group="appearance/v1";
std::array<double,3> linear(const Quantity_Color& c) { return {c.Red(),c.Green(),c.Blue()}; }
int srgb(double c) { return std::clamp(int(std::lround(255*Quantity_Color::Convert_LinearRGB_To_sRGB(c))),0,255); }
}
Appearance loadAppearance(QSettings& settings) {
    Appearance result;
    settings.beginGroup(group);
    for(int i=0;i<3;++i) {
        const auto values=settings.value(QString(names[i])+"Linear").toList();
        if(values.size()!=3) continue;
        std::array<double,3> rgb{};
        bool valid=true;
        for(int j=0;j<3;++j) {
            bool ok=false;rgb[j]=values[j].toDouble(&ok);
            valid &= ok && std::isfinite(rgb[j]) && rgb[j]>=0 && rgb[j]<=1;
        }
        if(valid) color(result,i)=Quantity_Color(rgb[0],rgb[1],rgb[2],Quantity_TOC_RGB);
    }
    bool ok=false;
    const double transparency=settings.value("transparency",factoryTransparency).toDouble(&ok);
    if(ok && std::isfinite(transparency) && transparency>=0 && transparency<=1) result.transparency=transparency;
    settings.endGroup();
    return result;
}
bool saveAppearance(QSettings& settings,const Appearance& appearance) {
    auto copy=appearance;
    settings.beginGroup(group);
    for(int i=0;i<3;++i) {
        const auto rgb=linear(color(copy,i));
        settings.setValue(QString(names[i])+"Linear",QVariantList{rgb[0],rgb[1],rgb[2]});
    }
    settings.setValue("transparency",appearance.transparency);
    settings.endGroup();settings.sync();
    return settings.status()==QSettings::NoError;
}
AppearanceDialog::AppearanceDialog(const Appearance& initial,QSettings& settings,
    std::function<void(const Appearance&)> apply,QWidget* parent)
    : QDialog(parent),original_(initial),preview_(initial),settings_(settings),apply_(std::move(apply)) {
    setObjectName("appearanceSettings");setWindowTitle("Settings — Appearance");setModal(true);
    auto* layout=new QVBoxLayout(this);
    layout->addWidget(new QLabel("Surface colours — sRGB channels (0–255)",this));
    auto* grid=new QGridLayout;layout->addLayout(grid);
    for(int j=0;j<3;++j) grid->addWidget(new QLabel(QStringList{"Red","Green","Blue"}[j]),0,j+1);
    for(int i=0;i<3;++i) {
        grid->addWidget(new QLabel(QStringList{"B — principal candidate","A — extensions","R — reductions"}[i]),i+1,0);
        for(int j=0;j<3;++j) {
            auto* spin=channels_[i][j]=new QSpinBox;
            spin->setRange(0,255);spin->setObjectName(QString("%1%2").arg(names[i]).arg(QStringList{"Red","Green","Blue"}[j]));
            spin->setAccessibleName(QString("%1 %2 sRGB").arg(names[i]).arg(QStringList{"red","green","blue"}[j]));
            grid->addWidget(spin,i+1,j+1);
            connect(spin,&QSpinBox::valueChanged,this,[this,i,j](int value) {
                // Convert only the edited channel. Preserve untouched factory precision.
                auto rgb=linear(color(preview_,i));
                rgb[j]=Quantity_Color::Convert_sRGB_To_LinearRGB(value/255.0);
                color(preview_,i)=Quantity_Color(rgb[0],rgb[1],rgb[2],Quantity_TOC_RGB);
                swatch(i);apply_(preview_);
            });
        }
        swatches_[i]=new QLabel;swatches_[i]->setFixedSize(56,26);
        swatches_[i]->setObjectName(QString(names[i])+"Swatch");grid->addWidget(swatches_[i],i+1,4);
    }
    layout->addWidget(new QLabel("Transparency for Transparent display style (0% = opaque)",this));
    transparency_=new QSlider(Qt::Horizontal);transparency_->setRange(0,100);
    transparency_->setObjectName("transparency");transparency_->setAccessibleName("Transparency percent");
    layout->addWidget(transparency_);percent_=new QLabel;layout->addWidget(percent_);
    connect(transparency_,&QSlider::valueChanged,this,[this](int value) {
        preview_.transparency=value/100.0;percent_->setText(QString::number(value)+"%");apply_(preview_);
    });
    auto* note=new QLabel("Changes preview in the current display style. OK saves appearance locally.\nSession tags and document state are never saved here.");
    note->setWordWrap(true);layout->addWidget(note);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    buttons->setObjectName("appearanceButtons");
    auto* defaults=buttons->addButton("Default values",QDialogButtonBox::ResetRole);
    defaults->setObjectName("defaultValues");
    connect(defaults,&QPushButton::clicked,this,[this] { preview_=Appearance{};refresh();apply_(preview_); });
    connect(buttons,&QDialogButtonBox::accepted,this,&AppearanceDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,this,&AppearanceDialog::reject);
    layout->addWidget(buttons);refresh();
}
void AppearanceDialog::swatch(int i) {
    const auto rgb=linear(color(preview_,i));
    swatches_[i]->setStyleSheet(QString("background-color: rgb(%1,%2,%3); border: 1px solid gray;").arg(srgb(rgb[0])).arg(srgb(rgb[1])).arg(srgb(rgb[2])));
}
void AppearanceDialog::refresh() {
    for(int i=0;i<3;++i) {
        const auto rgb=linear(color(preview_,i));
        for(int j=0;j<3;++j) { QSignalBlocker block(channels_[i][j]);channels_[i][j]->setValue(srgb(rgb[j])); }
        swatch(i);
    }
    QSignalBlocker block(transparency_);
    transparency_->setValue(int(std::lround(preview_.transparency*100)));
    percent_->setText(QString::number(transparency_->value())+"%");
}
void AppearanceDialog::accept() {
    if(!saveAppearance(settings_,preview_)) {
        QMessageBox::warning(this,"Settings","Could not save local appearance preferences.");return;
    }
    QDialog::accept();
}
void AppearanceDialog::reject() { apply_(original_);QDialog::reject(); }
}
