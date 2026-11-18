#include "display_tests.hpp"
#include <QApplication>
#include <QImage>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QWindow>
#include <Image_PixMap.hxx>
#include <V3d_ImageDumpOptions.hxx>
#include <BRepGProp.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <V3d_Viewer.hxx>
#include <Prs3d_IsoAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <Select3D_SensitiveEntity.hxx>
#include <SelectMgr_Selection.hxx>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace inspector {
struct ViewportProbe {
    static auto owner(Viewport& v) { v.context_->InitSelected(); return v.context_->SelectedOwner(); }
    static auto& objects(Viewport& v) { return v.objects_; }
    static auto& outlines(Viewport& v) { return v.contextObjects_; }
    static auto& context(Viewport& v) { return v.context_; }
    static auto& origin(Viewport& v) { return v.origin_; }
    static auto& highlight(Viewport& v) { return v.faceHighlight_; }
    static auto& view(Viewport& v) { return v.view_; }
    static auto kind(Viewport& v) { return v.kind_; }
    static auto layer(Viewport& v) { return v.contextLayer_; }
    static auto overlayLayer(Viewport& v) { return v.contextOverlayLayer_; }
    static auto originLayer(Viewport& v) { return v.originLayer_; }
    static auto selectionLayer(Viewport& v) { return v.selectionLayer_; }
};
}
namespace {
using namespace inspector;
using ShapeMap=NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher>;
void check(bool value,const std::string& why) { if(!value) throw std::runtime_error(why); }
bool sameCamera(const auto& a,const auto& b,bool includeClipping=true) {
    const bool same=a->ProjectionType()==b->ProjectionType() && a->Eye().Distance(b->Eye())<1e-8 &&
        a->Center().Distance(b->Center())<1e-8 && a->Direction().IsEqual(b->Direction(),1e-12) &&
        a->Up().IsEqual(b->Up(),1e-12) && std::abs(a->Scale()-b->Scale())<1e-8 &&
        (!includeClipping || (std::abs(a->ZNear()-b->ZNear())<1e-8 && std::abs(a->ZFar()-b->ZFar())<1e-8));
    if(!same) std::cerr<<"Camera difference: eye "<<a->Eye().Distance(b->Eye())<<", target "<<a->Center().Distance(b->Center())
        <<", scale "<<a->Scale()<<" -> "<<b->Scale()<<", clip "<<a->ZNear()<<","<<a->ZFar()<<" -> "<<b->ZNear()<<","<<b->ZFar()<<'\n';
    return same;
}
QSize drawableSize(Viewport& v) {
    int width=0,height=0;ViewportProbe::view(v)->Window()->Size(width,height);
    return {width,height};
}
void settleDrawable(Viewport& v) {
    QElapsedTimer deadline;deadline.start();
    QSize previous;int stable=0;
    auto camera=v.camera();
    // A single processEvents() need not finish native-window creation/resize.
    // Wait for exposure and repeated positive sizes, servicing Qt between them.
    while(deadline.elapsed()<5000) {
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        v.redraw();
        const auto size=drawableSize(v);
        if(v.windowHandle() && v.windowHandle()->isExposed() && size.width()>0 && size.height()>0 &&
           size==previous && sameCamera(camera,v.camera()) && camera->Aspect()==v.camera()->Aspect()) {
            if(++stable==3) return;
        } else stable=0;
        previous=size;camera=v.camera();QThread::msleep(20);
    }
    std::cerr<<"Unsettled drawable="<<previous.width()<<'x'<<previous.height()<<" DPR="<<v.devicePixelRatioF()<<'\n';
    check(false,"native specimen drawable did not settle");
}
QImage capture(Viewport& v,const std::string& suffix,bool native=false) {
    const auto path=std::filesystem::temp_directory_path()/("geo-vis-display-"+suffix+".png");
    if(native) {
        // Viewport::project uses OCCT's native drawable. At fractional Windows
        // scaling it can differ by a pixel from rounded Qt logical size * DPR.
        // Capture that exact drawable with the same camera, without aspect adjustment.
        const auto size=drawableSize(v);
        check(size.width()>0 && size.height()>0,"positive native capture dimensions");
        V3d_ImageDumpOptions options;options.Width=size.width();options.Height=size.height();
        options.ToAdjustAspect=false;
        Image_PixMap pixels;
        check(ViewportProbe::view(v)->ToPixMap(pixels,options),"native capture "+suffix);
        QImage image(int(pixels.SizeX()),int(pixels.SizeY()),QImage::Format_RGB32);
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
            const auto color=pixels.PixelColor(x,y).GetRGB();
            image.setPixelColor(x,y,QColor::fromRgbF(color.Red(),color.Green(),color.Blue()));
        }
        check(image.save(QString::fromStdString(path.string())),"save native capture "+suffix);
        return image;
    }
    check(v.capture(path.string()),"capture "+suffix);
    return QImage(QString::fromStdString(path.string()));
}
QRect difference(const QImage& a,const QImage& b,const QRect& ignored) {
    QRect bounds;
    for(int y=0;y<a.height();++y) for(int x=0;x<a.width();++x)
        if(!ignored.contains(x,y) && a.pixel(x,y)!=b.pixel(x,y)) bounds=bounds.united(QRect(x,y,1,1));
    return bounds;
}
QRect markerPixels(Viewport& v,const std::string& suffix) {
    auto& context=ViewportProbe::context(v);
    context->ClearDetected(false);
    context->Erase(ViewportProbe::origin(v),false);v.redraw();
    const auto without=capture(v,suffix+"-without-origin");
    context->Display(ViewportProbe::origin(v),0,-1,false);v.redraw();
    // The interactive cube has its own immediate/hover presentation and can
    // repaint independently. Keep that reserved footprint out of marker deltas.
    const int cube=qRound(200*v.devicePixelRatioF());
    return difference(without,capture(v,suffix),QRect(0,without.height()-cube,cube,cube));
}
const Entity& first(const inspector::Display& display,Kind kind) {
    for(const auto& c:display.components) for(const auto& e:c.entities) if(e.kind==kind) return e;
    throw std::runtime_error("missing entity");
}
void passive(Viewport& v) {
    auto& context=ViewportProbe::context(v);
    for(const auto& ais:ViewportProbe::outlines(v)) {
        check(ais->IsInfinite() && ais->DisplayMode()==AIS_WireFrame,"context is wire-only and excluded from Fit");
        NCollection_List<int> modes;context->ActivatedModes(ais,modes);
        check(modes.IsEmpty() && !ais->HasSelection(0),"context never registered with selector");
        check(ais->Attributes()->UIsoAspect()->Number()==0 && ais->Attributes()->VIsoAspect()->Number()==0,"no context isoparametric lines");
        check(ais->Attributes()->WireAspect()->Aspect()->Type()==Aspect_TOL_DASH &&
              ais->Attributes()->WireAspect()->Aspect()->Width()>1.5,"stronger dashed context stroke");
        occ::handle<SelectMgr_Selection> selection=new SelectMgr_Selection(0);ais->ComputeSelection(selection,0);
        check(selection->Entities().IsEmpty(),"context constructs no sensitive entities");
    }
    const auto& origin=ViewportProbe::origin(v);
    NCollection_List<int> modes;context->ActivatedModes(origin,modes);
    check(origin->IsInfinite() && modes.IsEmpty() && !origin->HasSelection(0),"origin excluded from bounds and selector");
    check(origin->TransformPersistence()->Mode()==Graphic3d_TMF_ZoomPers &&
          origin->TransformPersistence()->AnchorPoint().IsEqual(gp_Pnt(0,0,0),0),"origin zoom persistence at actual source-local zero");
    check(origin->ZLayer()==ViewportProbe::originLayer(v),"origin uses its own overlay layer");
    const auto& originSettings=context->CurrentViewer()->ZLayerSettings(origin->ZLayer());
    check(originSettings.IsImmediate() && !originSettings.ToEnableDepthTest() && !originSettings.ToEnableDepthWrite() && !originSettings.ToClearDepth(),"origin overlays model and transparent selection without modifying depth");
    const auto& highlight=ViewportProbe::highlight(v);
    if(!highlight.IsNull()) {
        NCollection_List<int> active;context->ActivatedModes(highlight,active);
        check(highlight->IsInfinite() && active.IsEmpty() && !highlight->HasSelection(0),"selected fill is passive and excluded from bounds");
        occ::handle<SelectMgr_Selection> selection=new SelectMgr_Selection(0);
        highlight->ComputeSelection(selection,0);
        check(selection->Entities().IsEmpty(),"selected fill has no sensitive entities");
        check(highlight->ZLayer()==ViewportProbe::selectionLayer(v),"selected fill precedes origin layer");
    }
    const auto& settings=context->CurrentViewer()->ZLayerSettings(ViewportProbe::layer(v));
    check(settings.ToEnableDepthTest() && !settings.ToEnableDepthWrite() && !settings.ToClearDepth(),"context layer cannot obscure foreground depth");
    const auto& overlay=context->CurrentViewer()->ZLayerSettings(ViewportProbe::overlayLayer(v));
    check(!overlay.ToEnableDepthTest() && !overlay.ToEnableDepthWrite() && !overlay.ToClearDepth(),"B/A outlines reveal contained reductions without writing depth");
}
void specimenHighlightChecks(Viewport& v,const inspector::Display& display) {
    settleDrawable(v);
    v.orient(V3d_Yneg);v.fit();v.setPickMode(Kind::Face);
    const auto size=drawableSize(v);
    const auto camera=v.camera();
    QPoint point;gp_Pnt interior;std::string id;
    for(const auto& c:display.components) for(const auto& e:c.entities) if(e.kind==Kind::Face) {
        BRepAdaptor_Surface surface(TopoDS::Face(e.display));
        if(surface.GetType()!=GeomAbs_Plane || std::abs(surface.Plane().Axis().Direction().Y())<0.99) continue;
        GProp_GProps props;BRepGProp::SurfaceProperties(e.display,props);
        interior=props.CentreOfMass();
        point=v.project(interior);
        std::cerr<<"Specimen projection: drawable="<<size.width()<<'x'<<size.height()
                 <<" DPR="<<v.devicePixelRatioF()<<" point="<<point.x()<<','<<point.y()
                 <<" candidate="<<e.id<<'\n';
        check(QRect(QPoint(0,0),size).contains(point),"specimen interior projects inside settled drawable");
        id=v.pick(point.x(),point.y());
        if(!id.empty()) break;
    }
    check(!id.empty(),"specimen front-face interior selectable");
    for(const auto style:{DisplayStyle::Shaded,DisplayStyle::Transparent,DisplayStyle::Wireframe}) {
        v.setDisplayStyle(style);v.select("");ViewportProbe::context(v)->ClearDetected(false);
        const auto before=capture(v,"specimen-before-"+std::to_string(int(style)),true);
        check(v.select(id),"inspector-style selection of actual specimen face");
        const auto owner=ViewportProbe::owner(v);
        const auto fill=ViewportProbe::highlight(v);
        const auto image=capture(v,"specimen-selected-"+std::to_string(int(style)),true);
        const auto currentSize=drawableSize(v);
        const auto projected=v.project(interior);
        std::cerr<<"Specimen probe: drawable="<<currentSize.width()<<'x'<<currentSize.height()
                 <<" image="<<image.width()<<'x'<<image.height()<<" DPR="<<v.devicePixelRatioF()
                 <<" point="<<point.x()<<','<<point.y()<<" current="<<projected.x()<<','<<projected.y()
                 <<" selected="<<v.selectedId()<<" style="<<int(style)<<'\n';
        check(currentSize==size && image.size()==size && before.size()==size &&
              sameCamera(camera,v.camera()) && camera->Aspect()==v.camera()->Aspect() && projected==point,
              "specimen projection and captures retain settled drawable and camera");
        check(image.rect().contains(point) && before.rect().contains(point),"specimen probe inside both captured images");
        const auto amber=image.pixelColor(point);
        check(image.pixel(point)!=before.pixel(point) && amber.red()>amber.blue()+30 && amber.green()>amber.blue()+30,"actual specimen face interior filled amber in all styles");
        for(const auto next:{DisplayStyle::Shaded,DisplayStyle::Transparent,DisplayStyle::Wireframe}) {
            v.setDisplayStyle(next);
            check(v.selectedId()==id && ViewportProbe::owner(v)==owner && ViewportProbe::highlight(v)==fill,"style cycling retains specimen owner and bounded fill");
        }
    }
    v.setDisplayStyle(DisplayStyle::Shaded);v.resetCamera();
}
void boundedHighlightChecks(Viewport& v) {
    auto polygon=[](std::initializer_list<gp_Pnt> points) {
        BRepBuilderAPI_MakePolygon builder;
        for(const auto& p:points) builder.Add(p);
        builder.Close();return builder.Wire();
    };
    BRepBuilderAPI_MakeFace builder(polygon({{-100,-100,0},{100,-100,0},{100,100,0},
        {30,100,0},{30,40,0},{-30,40,0},{-30,100,0},{-100,100,0}}));
    builder.Add(polygon({{-70,-40,0},{-70,10,0},{-20,10,0},{-20,-40,0}}));
    const auto face=builder.Face();
    const auto other=BRepBuilderAPI_MakeFace(polygon({{140,-100,0},{240,-100,0},{240,100,0},{140,100,0}})).Face();
    TopoDS_Compound compound;BRep_Builder brep;brep.MakeCompound(compound);brep.Add(compound,face);brep.Add(compound,other);
    Component component;component.id="bounded";component.display=compound;
    Entity entity;entity.id="bounded/face";entity.kind=Kind::Face;entity.display=face;component.entities.push_back(entity);
    entity.id="bounded/other";entity.display=other;component.entities.push_back(entity);
    inspector::Display display{Representation::Faces,{component},{}};
    v.setShowContext(false);v.showDisplay(&display);v.resetCamera();v.orient(V3d_Zpos);v.fit();v.setPickMode(Kind::Face);
    const auto inside=v.project(gp_Pnt(60,0,0)),hole=v.project(gp_Pnt(-45,-15,0));
    const auto recess=v.project(gp_Pnt(0,70,0)),unselected=v.project(gp_Pnt(190,0,0));
    const auto edge=v.project(gp_Pnt(100,0,0));
    const auto camera=v.camera();
    for(const auto style:{DisplayStyle::Shaded,DisplayStyle::Transparent,DisplayStyle::Wireframe}) {
        v.setDisplayStyle(style);v.select("");const auto before=capture(v,"bounded-before-"+std::to_string(int(style)));
        check(v.pick(inside.x(),inside.y())=="bounded/face","bounded selected face picked through fill");
        const auto owner=ViewportProbe::owner(v);passive(v);
        const auto image=capture(v,"bounded-selected-"+std::to_string(int(style)));
        const auto amber=image.pixelColor(inside),base=before.pixelColor(inside);
        check(amber!=base && amber.red()>amber.blue()+30 && amber.green()>amber.blue()+30,"actual translucent amber interior in every style");
        check(image.pixel(hole)==before.pixel(hole) && image.pixel(recess)==before.pixel(recess),"inner hole and concave recess stay unfilled");
        check(image.pixel(unselected)==before.pixel(unselected),"unselected face unaffected by highlight");
        if(style==DisplayStyle::Wireframe) check(image.pixel(unselected)==image.pixel(0,0),"only selected face filled in Wireframe");
        bool bright=false;
        for(int dx=-3;dx<=3;++dx) {const auto c=image.pixelColor(edge+QPoint(dx,0));bright|=c.red()>230 && c.green()>220 && c.blue()<100;}
        check(bright,"brighter yellow boundary retained around amber fill");
        check(v.pick(inside.x(),inside.y())=="bounded/face","fill never intercepts selected face pick");
        check(v.selectedId()=="bounded/face" && ViewportProbe::owner(v)==owner && sameCamera(camera,v.camera()),"bounded highlight preserves owner/camera");
    }
    v.setPickMode(Kind::Edge);check(ViewportProbe::highlight(v).IsNull(),"pick-mode change clears face fill");
    v.showDisplay(nullptr);check(ViewportProbe::highlight(v).IsNull(),"representation change removes stale face fill");
    std::cout<<"PASS — bounded translucent highlight, inner wire/recess, bright boundary and unselected Wireframe negative control\n";
}
void hiddenFaceHighlightChecks(Viewport& v) {
    // Synthetic display copies only. Two separate faces exactly overlap in
    // screen space; no source specimen or geometry-pipeline behavior is added.
    auto square=[](double z) {
        BRepBuilderAPI_MakePolygon loop;
        for(const auto& p:{gp_Pnt(-100,-100,z),gp_Pnt(100,-100,z),gp_Pnt(100,100,z),gp_Pnt(-100,100,z)}) loop.Add(p);
        loop.Close();return BRepBuilderAPI_MakeFace(loop.Wire()).Face();
    };
    const auto front=square(20),rear=square(-20);
    TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);builder.Add(compound,front);builder.Add(compound,rear);
    Component component;component.id="overlap";component.display=compound;
    Entity e;e.kind=Kind::Face;e.id="overlap/front";e.display=front;component.entities.push_back(e);
    e.id="overlap/rear";e.display=rear;component.entities.push_back(e);
    inspector::Display display{Representation::Faces,{component},{}};
    v.setShowContext(false);v.setDisplayStyle(DisplayStyle::Shaded);v.showDisplay(&display);
    v.resetCamera();v.orient(V3d_Zpos);v.fit();v.setPickMode(Kind::Face);
    const auto camera=v.camera();
    const auto interior=v.project(gp_Pnt(55,50,-20)); // Away from edges and origin arrows.
    const auto opaque=capture(v,"overlap-unselected");
    check(opaque.pixel(interior)!=opaque.pixel(0,0),"overlap regression samples the opaque front interior");
    // Window::select uses setPickMode + this same public selection path when a
    // tree row is invoked. Picking cannot select the hidden rear face here.
    check(v.select("overlap/rear"),"hidden rear face selected through tree-to-viewport path");
    const auto owner=ViewportProbe::owner(v);
    const auto fill=ViewportProbe::highlight(v);
    int notifications=0;v.selectionChanged=[&](const auto&) {++notifications;};
    for(const auto style:{DisplayStyle::Shaded,DisplayStyle::Transparent,DisplayStyle::Wireframe,DisplayStyle::Shaded}) {
        v.setDisplayStyle(style);passive(v);
        const auto image=capture(v,"overlap-selected-"+std::to_string(int(style)));
        for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx) {
            const auto point=interior+QPoint(dx,dy);
            if(style==DisplayStyle::Shaded) check(image.pixel(point)==opaque.pixel(point),"hidden rear fill must not tint the opaque front interior");
            else {const auto color=image.pixelColor(point);check(color.red()>color.blue()+30 && color.green()>color.blue()+30,"Transparent/Wireframe retain hidden-face amber overlay");}
        }
        check(v.selectedId()=="overlap/rear" && ViewportProbe::owner(v)==owner && ViewportProbe::highlight(v)==fill &&
              ViewportProbe::kind(v)==Kind::Face && notifications==0 && sameCamera(camera,v.camera()),"hidden selection/style changes preserve actual owner, fill, mode and camera");
    }
    // Expose that same selected face, without selecting it again. Repeated
    // captures and nearby oblique views check stable coincident-surface fill.
    for(const auto direction:{V3d_Zneg,V3d_XposYnegZneg,V3d_Zneg}) {
        v.orient(direction);
        const auto point=v.project(gp_Pnt(55,50,-20));
        const auto image=capture(v,"overlap-exposed-"+std::to_string(int(direction)));
        for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx) {
            const auto color=image.pixelColor(point+QPoint(dx,dy));
            check(color.red()>color.blue()+30 && color.green()>color.blue()+30,"exposed rear face has stable amber interior");
        }
        const auto edge=v.project(gp_Pnt(100,50,-20));
        bool bright=false;
        for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx) {
            const auto c=image.pixelColor(edge+QPoint(dx,dy));bright|=c.red()>230 && c.green()>220 && c.blue()<100;
        }
        check(bright,"exposed rear face has bright boundary");
        const auto repeated=capture(v,"overlap-exposed-repeat");
        for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx)
            check(image.pixel(point+QPoint(dx,dy))==repeated.pixel(point+QPoint(dx,dy)),"coincident fill stable across redraws");
        check(v.selectedId()=="overlap/rear" && ViewportProbe::owner(v)==owner && ViewportProbe::highlight(v)==fill && notifications==0,"view snap exposes existing rear selection without reselecting");
    }
    v.selectionChanged={};v.showDisplay(nullptr);
    std::cout<<"PASS — hidden rear fill occluded in Shaded, exposed without reselecting, overlay styles and coincident fill retained\n";
}
void appearanceRenderChecks(Viewport& v,const Document& document) {
    const auto original=v.appearance();
    Appearance changed=original;
    changed.palette.b=Quantity_Color(0.1,0.7,0.2,Quantity_TOC_sRGB);
    changed.palette.a=Quantity_Color(0.8,0.3,0.2,Quantity_TOC_sRGB);
    changed.palette.r=Quantity_Color(0.2,0.3,0.9,Quantity_TOC_sRGB);
    changed.transparency=0.31;
    for(const auto representation:{Representation::Candidate,Representation::Extensions,Representation::Reductions}) {
        auto display=prepareDisplay(document,representation);
        v.showDisplay(&display,display.components.empty()?-1:0);v.resetCamera();
        if(display.components.empty()) {v.setAppearance(changed);v.setAppearance(original);v.showDisplay(nullptr);continue;}
        v.select("");v.setDisplayStyle(DisplayStyle::Shaded);
        const auto opening=capture(v,"appearance-opening-"+std::to_string(int(representation)));
        for(const auto style:{DisplayStyle::Shaded,DisplayStyle::Transparent,DisplayStyle::Wireframe}) {
            v.setDisplayStyle(style);v.setPickMode(Kind::Face);
            const auto id=first(display,Kind::Face).id;check(v.select(id),"appearance selected face setup");
            const auto owner=ViewportProbe::owner(v);const auto highlight=ViewportProbe::highlight(v);
            const auto camera=v.camera();int notifications=0;v.selectionChanged=[&](const auto&) {++notifications;};
            for(const auto& appearance:{changed,original}) {
                v.setAppearance(appearance);
                check(v.selectedId()==id && ViewportProbe::owner(v)==owner && ViewportProbe::highlight(v)==highlight && notifications==0,
                      "appearance preview/rollback retains owner and face highlight without selection callbacks");
                check(sameCamera(camera,v.camera()),"appearance preserves camera and clipping");
                for(const auto& object:ViewportProbe::objects(v)) {
                    Quantity_Color surface;object.ais->Color(surface);
                    check(surface.IsEqual(appearance.palette.surface(representation)),"preview updates actual surface colour");
                    check(object.ais->Attributes()->FaceBoundaryAspect()->Aspect()->Color().IsEqual(Quantity_Color(Quantity_NOC_BLACK)),
                          "appearance retains black filled-face boundaries");
                    const auto wire=appearance.palette.wire(representation);
                    check(object.ais->Attributes()->WireAspect()->Aspect()->Color().IsEqual(wire) &&
                          object.ais->Attributes()->FreeBoundaryAspect()->Aspect()->Color().IsEqual(wire) &&
                          object.ais->Attributes()->UnFreeBoundaryAspect()->Aspect()->Color().IsEqual(wire),"preview retains surface/wire accent relationship");
                    check(object.ais->DisplayMode()==(style==DisplayStyle::Wireframe?AIS_WireFrame:AIS_Shaded),"preview does not change actual display style");
                    check(std::abs(object.ais->Transparency()-(style==DisplayStyle::Transparent?appearance.transparency:0))<1e-6,
                          "custom transparency affects Transparent only");
                }
                passive(v);
            }
            v.selectionChanged={};
        }
        v.select("");v.setDisplayStyle(DisplayStyle::Shaded);
        const auto factory=capture(v,"appearance-factory-"+std::to_string(int(representation)));
        check(difference(opening,factory,QRect()).isEmpty(),"preview round trip matches appearance before the first edit");
        v.setAppearance(changed);
        const auto preview=capture(v,"appearance-preview-"+std::to_string(int(representation)));
        check(!difference(factory,preview,QRect()).isEmpty(),"custom palette visibly changes framebuffer");
        v.setAppearance(original);
        const auto restored=capture(v,"appearance-restored-"+std::to_string(int(representation)));
        check(difference(factory,restored,QRect()).isEmpty(),"rollback restores exact framebuffer");
        v.showDisplay(nullptr);
    }
    v.setDisplayStyle(DisplayStyle::Shaded);
    std::cout<<"PASS — appearance updates actual AIS colours/transparency, preserves owner/highlight/camera and restores framebuffer\n";
}
void representationClearanceChecks(Viewport& v,const Document& document) {
    v.setDisplayStyle(DisplayStyle::Shaded);
    auto b=prepareDisplay(document,Representation::Candidate);
    auto a=prepareDisplay(document,Representation::Extensions);
    auto r=prepareDisplay(document,Representation::Reductions);
    if(!r.components.empty()) {
        v.showDisplay(&r);v.resetCamera();v.toggleProjection();v.orient(V3d_Zneg);v.fit();
        const auto before=v.camera();
        v.showDisplay(&b,-1,false);
        const auto after=v.camera();
        check(after->Distance()>before->Distance(),"Perspective R Bottom Fit B retreats eye when foreground grows");
        check(after->ProjectionType()==before->ProjectionType() && after->Center().Distance(before->Center())<1e-8 &&
            after->Direction().IsEqual(before->Direction(),1e-12) && after->Up().IsEqual(before->Up(),1e-12),"representation retreat preserves target, orientation and projection");
        Bnd_Box bounds;for(const auto& c:b.components) BRepBndLib::AddOptimal(c.display,bounds,false,false);
        const auto min=bounds.CornerMin(),max=bounds.CornerMax();
        for(double x:{min.X(),max.X()}) for(double y:{min.Y(),max.Y()}) for(double z:{min.Z(),max.Z()})
            check(gp_Vec(after->Eye(),gp_Pnt(x,y,z)).Dot(gp_Vec(after->Direction()))>after->ZNear(),"all B bounds ahead of eye and near plane after retreat");
        const gp_Pnt bottom((min.X()+max.X())/2,(min.Y()+max.Y())/2,min.Z());
        const auto pixel=v.project(bottom);
        const auto image=capture(v,"perspective-R-bottom-fit-B");
        check(image.rect().contains(pixel) && image.pixel(pixel)!=image.pixel(0,0),"B visible after unsafe R to B switch");
        v.setPickMode(Kind::Face);
        check(!v.pick(pixel.x(),pixel.y()).empty(),"B end face pickable after eye retreat");
        std::cout<<"Perspective R Bottom Fit B: eye distance "<<before->Distance()<<" -> "<<after->Distance()<<'\n';
    }
    v.showDisplay(&b);v.resetCamera();v.toggleProjection();
    const auto safe=v.camera();
    for(const auto* display:{&a,&r,&b}) {
        v.showDisplay(display,-1,false);
        check(sameCamera(safe,v.camera(),false),"safe Perspective B/A/R switch preserves exact comparison framing");
    }
    v.resetCamera();v.showDisplay(nullptr);
    std::cout<<"PASS — representation eye clearance and safe Perspective comparison camera\n";
}
}
void displayMappingChecks(const inspector::Document& document) {
    using namespace inspector;
    for(const auto active:{Representation::Faces,Representation::Solid,Representation::Candidate,Representation::Extensions,Representation::Reductions}) {
        const auto display=prepareDisplay(document,active);
        const bool partial=active==Representation::Candidate || active==Representation::Extensions || active==Representation::Reductions;
        const auto expected=partial?std::size_t(active!=Representation::Candidate)+
            (active!=Representation::Extensions?document.decomposition.extensions.size():0)+
            (active!=Representation::Reductions?document.decomposition.reductions.size():0):0;
        check(display.context.size()==expected,"context routes exactly the other partial bodies");
        for(const auto& outline:display.context) {
            check(outline.representation!=active,"active group never repeats as context");
            const auto& source=outline.representation==Representation::Candidate?document.candidate.solid:
                outline.representation==Representation::Extensions?document.decomposition.extensions.front().solid:document.decomposition.reductions.front().solid;
            ShapeMap a,b;TopExp::MapShapes(source,a);TopExp::MapShapes(outline.shape,b);
            check(a.Extent()==b.Extent(),"context preserves topology size");
            for(int i=1;i<=b.Extent();++i) {
                check(!a.Contains(b(i)),"context has private topology");
                if(b(i).ShapeType()==TopAbs_FACE) {
                    TopLoc_Location location;
                    check(BRep_Tool::Triangulation(TopoDS::Face(b(i)),location).IsNull(),"context carries no source mesh");
                }
            }
            ShapeMap av,bv;TopExp::MapShapes(source,TopAbs_VERTEX,av);TopExp::MapShapes(outline.shape,TopAbs_VERTEX,bv);
            for(int i=1;i<=av.Extent();++i)
                check(BRep_Tool::Pnt(TopoDS::Vertex(av(i))).IsEqual(BRep_Tool::Pnt(TopoDS::Vertex(bv(i))),0),"context retains exact untransformed coordinates/order");
            for(const auto& c:display.components) {
                check(!c.display.IsSame(outline.shape),"foreground and context copies are independent");
                for(const auto& e:c.entities) check(!b.Contains(e.display),"context cannot enter foreground entity mappings");
            }
        }
    }
    OriginMarker marker;
    occ::handle<SelectMgr_Selection> selection=new SelectMgr_Selection(0);
    marker.ComputeSelection(selection,0);
    check(selection->Entities().IsEmpty(),"origin builds no sensitive entities");
    std::cout<<"PASS — context routes, private copies, source-local coordinates and passive origin\n";
}
void displayRenderChecks(inspector::Viewport& v,const inspector::Document& document) {
    using namespace inspector;
    v.setShowContext(true);v.setDisplayStyle(DisplayStyle::Shaded);
    auto solid=prepareDisplay(document,Representation::Solid);
    v.showDisplay(&solid);v.resetCamera();QApplication::processEvents();
    passive(v);
    specimenHighlightChecks(v,solid);
    for(const auto kind:{Kind::Vertex,Kind::Edge,Kind::Face,Kind::Body}) {
        v.setPickMode(kind);const auto id=first(solid,kind).id;
        check(v.select(id),"style selection setup");
        const auto owner=ViewportProbe::owner(v);
        const auto camera=v.camera();
        int notifications=0;v.selectionChanged=[&](const auto&) {++notifications;};
        for(const auto style:{DisplayStyle::Transparent,DisplayStyle::Wireframe,DisplayStyle::Shaded}) {
            v.setDisplayStyle(style);
            check(v.selectedId()==id && ViewportProbe::owner(v)==owner && ViewportProbe::kind(v)==kind && notifications==0,"styles preserve actual owner, pick mode and callbacks");
            check(sameCamera(camera,v.camera()),"style preserves complete camera and clipping");
            for(const auto& o:ViewportProbe::objects(v)) {
                check(o.ais->DisplayMode()==(style==DisplayStyle::Wireframe?AIS_WireFrame:AIS_Shaded),"actual foreground display mode");
                check(std::abs(o.ais->Transparency()-(style==DisplayStyle::Transparent?0.72:0))<1e-6,"actual foreground opacity");
            }
            check((kind==Kind::Face)==!ViewportProbe::highlight(v).IsNull(),"only face selection adds bounded fill");
            passive(v);
        }
        v.selectionChanged={};
    }
    // All partial groups share the exact same camera, including near/far planes.
    auto comparisonCamera=v.camera();
    for(const auto r:{Representation::Candidate,Representation::Extensions,Representation::Reductions}) {
        auto display=prepareDisplay(document,r);
        v.showDisplay(&display,-1,false);
        check(sameCamera(comparisonCamera,v.camera(),false),"B/A/R preserves comparison camera");
        comparisonCamera=v.camera(); // Clipping follows the newly active foreground.
        passive(v);
        const Quantity_Color expectedWire=r==Representation::Candidate?Quantity_Color(0.656,0.672,0.688,Quantity_TOC_RGB):
            r==Representation::Extensions?Quantity_Color(0.456,0.472,0.488,Quantity_TOC_RGB):Quantity_Color(0.776,0.616,0.624,Quantity_TOC_RGB);
        for(const auto& o:ViewportProbe::objects(v)) {
            check(o.ais->Attributes()->WireAspect()->Aspect()->Color().IsEqual(expectedWire) &&
                  o.ais->Attributes()->FreeBoundaryAspect()->Aspect()->Color().IsEqual(expectedWire) &&
                  o.ais->Attributes()->UnFreeBoundaryAspect()->Aspect()->Color().IsEqual(expectedWire),"explicit B grey / A dark grey / R muted red Wireframe colours");
        }
        check(ViewportProbe::outlines(v).size()==display.context.size(),"rendered context route");
        if(!display.components.empty()) {
            v.setPickMode(Kind::Face);const auto id=first(display,Kind::Face).id;
            check(v.select(id),"context face selection setup");
            const auto owner=ViewportProbe::owner(v);
            for(const bool show:{false,true}) {
                v.setShowContext(show);
                check(v.selectedId()==id && ViewportProbe::owner(v)==owner,"toggle retains actual selected owner");
                check(sameCamera(comparisonCamera,v.camera()),"toggle retains complete camera");
            }
            // Explicit Fit and Perspective clearance must ignore even very large context.
            v.setShowContext(false);
            ViewportProbe::context(v)->Erase(ViewportProbe::origin(v),false);
            v.fit();const auto foregroundFit=v.camera();
            v.toggleProjection();const auto foregroundPerspective=v.camera();v.toggleProjection();
            ViewportProbe::context(v)->Display(ViewportProbe::origin(v),0,-1,false);
            v.setShowContext(true);v.fit();
            check(sameCamera(foregroundFit,v.camera()),"context and origin excluded from Fit and clipping bounds");
            v.toggleProjection();check(sameCamera(foregroundPerspective,v.camera()),"context and origin excluded from Perspective clearance bounds");
            v.toggleProjection();
            if(r==Representation::Candidate && !display.context.empty()) {
                v.setDisplayStyle(DisplayStyle::Shaded);v.select("");
                v.setShowContext(false);const auto without=capture(v,"B-context-off");
                v.setShowContext(true);const auto with=capture(v,"B-context-on");
                const int cube=qRound(200*v.devicePixelRatioF());
                check(!difference(without,with,QRect(0,with.height()-cube,cube,cube)).isEmpty(),"B context is visibly rendered over its opaque foreground");
            }
            if(r==Representation::Reductions) {
                v.setDisplayStyle(DisplayStyle::Shaded);v.select("");ViewportProbe::context(v)->ClearDetected(false);
                v.setShowContext(false);const auto without=capture(v,"R-context-off");
                v.setShowContext(true);const auto with=capture(v,"R-context-on");
                int checked=0;
                for(int y=3;y<without.height()-3;y+=3) for(int x=qRound(200*v.devicePixelRatioF());x<without.width()-3;x+=3) {
                    const auto pixel=without.pixel(x,y);
                    if(pixel==without.pixel(0,0) || pixel==qRgb(0,0,0)) continue;
                    if(pixel==without.pixel(x-3,y) && pixel==without.pixel(x+3,y) && pixel==without.pixel(x,y-3) && pixel==without.pixel(x,y+3)) {
                        check(with.pixel(x,y)==pixel,"R opaque foreground remains ahead of ghost strokes");++checked;
                    }
                }
                check(checked>100,"R ghost priority checked over actual pocket surface pixels");
            }
            // Foreground stays detectable through coincident context in every style.
            for(const auto style:{DisplayStyle::Shaded,DisplayStyle::Transparent,DisplayStyle::Wireframe}) {
                v.setDisplayStyle(style);bool picked=false;
                for(const auto& c:display.components) for(const auto& e:c.entities) if(e.kind==Kind::Face) {
                    GProp_GProps props;BRepGProp::SurfaceProperties(e.display,props);
                    const auto pixel=v.project(props.CentreOfMass());
                    const auto hit=v.pick(pixel.x(),pixel.y());
                    if(!hit.empty()) { picked=true;check(hit.find(c.id+"/")==0,"context cannot win a foreground pick"); }
                }
                check(picked,"ordinary face picking remains usable in each style");
                v.select(id);capture(v,std::to_string(static_cast<int>(r))+"-style-"+std::to_string(static_cast<int>(style)));
            }
            const auto before=v.camera();const auto count=ViewportProbe::outlines(v).size();
            v.showDisplay(&display,0,false);
            check(count==ViewportProbe::outlines(v).size() && sameCamera(before,v.camera()),"isolation retains context set and camera");
        } else {
            check(ViewportProbe::objects(v).empty() && v.selectedId().empty(),"context alone is never an active body");
            const auto before=v.camera();v.fit();check(sameCamera(before,v.camera()),"Fit on empty foreground ignores context/origin");
        }
        comparisonCamera=v.camera();v.showDisplay(nullptr,-1,false);
    }
    // A plain rectangular B front face provides a direct diagonal/fill negative control.
    auto candidate=prepareDisplay(document,Representation::Candidate);
    v.setShowContext(false);v.setDisplayStyle(DisplayStyle::Shaded);v.showDisplay(&candidate);v.resetCamera();
    v.orient(V3d_Yneg);v.fit();v.select("");
    // Enclose zero in an independent selectable box: all arrow pixels are
    // behind an opaque front face, and then behind the actual selected fill.
    const double radius=v.camera()->ViewDimensions().Y()*100/v.height();
    Component occluder;occluder.id="occluder";
    occluder.display=BRepPrimAPI_MakeBox(gp_Pnt(-radius,-radius*0.1,-radius),gp_Pnt(radius,radius*0.1,radius)).Shape();
    int ordinal=0;
    for(TopExp_Explorer it(occluder.display,TopAbs_FACE);it.More();it.Next()) {
        Entity e;e.id="occluder/"+std::to_string(ordinal++);e.kind=Kind::Face;e.display=it.Current();occluder.entities.push_back(e);
    }
    inspector::Display enclosed{Representation::Solid,{occluder},{}};
    v.showDisplay(&enclosed);v.resetCamera();v.orient(V3d_Yneg);v.fit();v.setPickMode(Kind::Face);
    const auto front=v.project(gp_Pnt(0,-radius*0.1,0));
    for(const auto style:{DisplayStyle::Shaded,DisplayStyle::Transparent,DisplayStyle::Wireframe}) {
        v.setDisplayStyle(style);v.select("");
        check(!markerPixels(v,"origin-over-face-"+std::to_string(int(style))).isEmpty(),"origin shows over foreground in every style");
        const auto overFace=capture(v,"origin-colour-before-"+std::to_string(int(style)));
        const auto id=v.pick(front.x()+5,front.y());check(!id.empty(),"origin arrows do not intercept face pick");
        check(!markerPixels(v,"origin-over-selection-"+std::to_string(int(style))).isEmpty(),"origin shows over selected-face fill in every style");
        const auto overSelection=capture(v,"origin-colour-after-"+std::to_string(int(style)));
        const auto arrow=front+QPoint(qRound(18*v.devicePixelRatioF()),0);
        check(overFace.pixel(arrow)!=overFace.pixel(arrow+QPoint(0,5)) && overFace.pixel(arrow)==overSelection.pixel(arrow),"selected transparency must not tint or cover the origin arrow");
        passive(v);
        const auto fitted=v.camera();
        ViewportProbe::context(v)->Erase(ViewportProbe::origin(v),false);v.fit();
        check(sameCamera(fitted,v.camera()),"overlay origin/selection do not change Fit");
        ViewportProbe::context(v)->Display(ViewportProbe::origin(v),0,-1,false);
    }
    v.setDisplayStyle(DisplayStyle::Shaded);v.showDisplay(&candidate);v.resetCamera();v.orient(V3d_Yneg);v.fit();v.select("");
    const auto shaded=capture(v,"B-shaded-front");
    v.setDisplayStyle(DisplayStyle::Wireframe);
    const auto wire=capture(v,"B-wire-front");
    const auto pmin=document.candidate.minimumLocalMm,pmax=document.candidate.maximumLocalMm;
    const gp_Pnt midpoint((pmin.x+pmax.x)/2,pmin.y,(pmin.z+pmax.z)/2);
    const auto pixel=v.project(midpoint);const auto background=wire.pixel(0,0);
    check(shaded.pixel(pixel)!=shaded.pixel(0,0),"front-face interior has opaque fill in Shaded");
    for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx)
        check(wire.pixel(pixel+QPoint(dx,dy))==background,"Wireframe front-face diagonal/interior is empty");
    // Marker only: inspect actual framebuffer positions and apparent lengths.
    v.showDisplay(nullptr,-1,false);
    ViewportProbe::view(v)->SetAt(0,0,0);
    v.orient(V3d_Yneg);
    auto initial=markerPixels(v,"origin-front");
    check(!initial.isEmpty() && initial.adjusted(-3,-3,3,3).contains(v.project(gp_Pnt(0,0,0))),"visible arrows begin at projected source origin");
    const auto initialCamera=v.camera();v.fit();check(sameCamera(initialCamera,v.camera()),"origin-only scene does not Fit");
    ViewportProbe::view(v)->SetZoom(2);v.redraw();
    auto zoomed=markerPixels(v,"origin-zoom");
    check(std::abs(initial.width()-zoomed.width())<=2 && std::abs(initial.height()-zoomed.height())<=2,"origin apparent arrow size remains constant on zoom");
    ViewportProbe::view(v)->Pan(80,50);v.redraw();
    auto panned=markerPixels(v,"origin-pan");
    check(std::abs(initial.width()-panned.width())<=2 && panned.adjusted(-3,-3,3,3).contains(v.project(gp_Pnt(0,0,0))) && panned.topLeft()!=zoomed.topLeft(),"origin anchor tracks world point through pan");
    v.orient(V3d_XposYnegZpos);
    auto orbited=markerPixels(v,"origin-orbit");
    check(orbited!=panned && orbited.adjusted(-3,-3,3,3).contains(v.project(gp_Pnt(0,0,0))),"origin rotates with world axes");
    v.toggleProjection();
    auto perspective=markerPixels(v,"origin-perspective");
    check(!perspective.isEmpty() && perspective.adjusted(-3,-3,3,3).contains(v.project(gp_Pnt(0,0,0))),"origin anchor tracks Perspective");
    v.setPickMode(Kind::Face);
    const auto anchor=v.project(gp_Pnt(0,0,0));
    check(v.pick(anchor.x()+5,anchor.y()).empty(),"visible origin cannot intercept picking");
    ViewportProbe::view(v)->Pan(5000,0);v.redraw();
    check(markerPixels(v,"origin-offscreen").isEmpty(),"origin may leave view without recentering");
    boundedHighlightChecks(v);
    hiddenFaceHighlightChecks(v);
    representationClearanceChecks(v,document);
    appearanceRenderChecks(v,document);
    v.setShowContext(true);v.setDisplayStyle(DisplayStyle::Shaded);v.showDisplay(nullptr);
    check(v.error().empty(),v.error());
    std::cout<<"PASS — styles/owners, context picking/bounds, wireframe and world-origin framebuffer regressions\n";
}
