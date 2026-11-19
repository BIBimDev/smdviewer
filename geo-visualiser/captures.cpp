// Repeatable native framebuffer evidence. No source geometry or camera policy
// is changed; the pocket camera is fitted to the existing R display copy.
#include "viewport.hpp"
#include <QApplication>
#include <QImage>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace inspector {
struct CaptureProbe {
    static auto& view(Viewport& v) { return v.view_; }
};
}
int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: smd-inspector-captures <samples> <output-prefix>");
        QApplication app(argc,argv);
        const std::filesystem::path samples=argv[1];
        const std::string prefix=argv[2];
        const auto doc=inspector::loadDocument(samples/"Columns/COL_SINGLE-POCKET.smd");
        auto solid=inspector::prepareDisplay(*doc,inspector::Representation::Solid);
        auto reduction=inspector::prepareDisplay(*doc,inspector::Representation::Reductions);
        inspector::Viewport v;v.resize(900,800);v.show();app.processEvents();
        v.showDisplay(&solid);v.resetCamera();v.orient(V3d_XnegYnegZpos);v.fit();
        const auto capture=[&](const std::string& suffix) {
            v.redraw();
            const auto file=prefix+suffix+".png";
            if(!v.capture(file)) throw std::runtime_error("capture failed: "+file);
            const QImage image(QString::fromStdString(file));
            const int extent=qRound(210*v.devicePixelRatioF());
            image.copy(0,image.height()-extent,extent,extent).save(QString::fromStdString(prefix+suffix+"-cube.png"));
            // Fixed logical-pixel pocket detail, exported without resampling.
            image.copy(QRect(qRound(220*v.devicePixelRatioF()),qRound(155*v.devicePixelRatioF()),
                             qRound(235*v.devicePixelRatioF()),qRound(150*v.devicePixelRatioF())))
                .save(QString::fromStdString(prefix+suffix+"-detail.png"));
        };
        capture("-cube-O");v.toggleProjection();capture("-cube-P");v.resetCamera();
        v.showDisplay(&reduction);v.orient(V3d_XnegYnegZpos);v.fit();v.showDisplay(&solid,-1,false);
        GProp_GProps props;BRepGProp::VolumeProperties(reduction.components.front().display,props);
        const auto point=v.project(props.CentreOfMass());
        v.setPickMode(inspector::Kind::Face);
        std::cout<<"Selected pocket face: "<<v.pick(point.x(),point.y())<<'\n';
        capture("-pocket");
        std::ofstream state(prefix+"-camera.txt");
        const auto c=v.camera();
        state<<std::setprecision(17)<<"DPR "<<v.devicePixelRatioF()<<"\neye "<<c->Eye().X()<<' '<<c->Eye().Y()<<' '<<c->Eye().Z()
            <<"\ncenter "<<c->Center().X()<<' '<<c->Center().Y()<<' '<<c->Center().Z()<<"\nscale "<<c->Scale()
            <<"\nMSAA requested "<<inspector::CaptureProbe::view(v)->RenderingParams().NbMsaaSamples<<'\n';
        // Isolate antialiasing from cube-tone changes using identical geometry,
        // selection, projection, camera, drawable and current cube presentation.
        auto& params=inspector::CaptureProbe::view(v)->ChangeRenderingParams();
        const auto samplesRequested=params.NbMsaaSamples;
        params.NbMsaaSamples=0;capture("-pocket-msaa-off");
        params.NbMsaaSamples=samplesRequested;capture("-pocket-msaa-on");
        if(!v.error().empty()) throw std::runtime_error(v.error());
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
