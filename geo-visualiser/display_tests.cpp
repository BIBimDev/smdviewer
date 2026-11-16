#include "display_tests.hpp"
#include <QApplication>
#include <QImage>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <V3d_Viewer.hxx>
#include <Prs3d_IsoAspect.hxx>
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
    static auto& view(Viewport& v) { return v.view_; }
    static auto kind(Viewport& v) { return v.kind_; }
    static auto layer(Viewport& v) { return v.contextLayer_; }
    static auto overlayLayer(Viewport& v) { return v.contextOverlayLayer_; }
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
QImage capture(Viewport& v,const std::string& suffix) {
    const auto path=std::filesystem::temp_directory_path()/("geo-vis-display-"+suffix+".png");
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
    }
    const auto& origin=ViewportProbe::origin(v);
    NCollection_List<int> modes;context->ActivatedModes(origin,modes);
    check(origin->IsInfinite() && modes.IsEmpty() && !origin->HasSelection(0),"origin excluded from bounds and selector");
    check(origin->TransformPersistence()->Mode()==Graphic3d_TMF_ZoomPers &&
          origin->TransformPersistence()->AnchorPoint().IsEqual(gp_Pnt(0,0,0),0),"origin zoom persistence at actual source-local zero");
    check(origin->ZLayer()==Graphic3d_ZLayerId_Default,"origin uses depth-tested model layer");
    const auto& settings=context->CurrentViewer()->ZLayerSettings(ViewportProbe::layer(v));
    check(settings.ToEnableDepthTest() && !settings.ToEnableDepthWrite() && !settings.ToClearDepth(),"context layer cannot obscure foreground depth");
    const auto& overlay=context->CurrentViewer()->ZLayerSettings(ViewportProbe::overlayLayer(v));
    check(!overlay.ToEnableDepthTest() && !overlay.ToEnableDepthWrite() && !overlay.ToClearDepth(),"B/A faint outlines reveal contained reductions without writing depth");
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
    // Specimen zero lies on a boundary corner, so some positive arrows can
    // legitimately remain exposed. Enclose it in an independent test occluder.
    const double radius=v.camera()->ViewDimensions().Y()*100/v.height();
    occ::handle<AIS_Shape> occluder=new AIS_Shape(BRepPrimAPI_MakeBox(gp_Pnt(-radius,-radius*0.1,-radius),gp_Pnt(radius,radius*0.1,radius)).Shape());
    ViewportProbe::context(v)->Display(occluder,AIS_Shaded,-1,false);
    check(markerPixels(v,"origin-occluded").isEmpty(),"origin does not show through opaque foreground");
    ViewportProbe::context(v)->Remove(occluder,false);
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
    v.setShowContext(true);v.setDisplayStyle(DisplayStyle::Shaded);v.showDisplay(nullptr);
    check(v.error().empty(),v.error());
    std::cout<<"PASS — styles/owners, context picking/bounds, wireframe and world-origin framebuffer regressions\n";
}
