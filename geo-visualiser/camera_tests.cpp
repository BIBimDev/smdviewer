#include "camera_tests.hpp"
#include <QApplication>
#include <QMouseEvent>
#include <QImage>
#include <QEvent>
#include <QEventLoop>
#include <QWheelEvent>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_RenderingParams.hxx>
#include <Graphic3d_GraphicDriver.hxx>
#include <V3d_Viewer.hxx>
#include <OpenGl_Group.hxx>
#include <OpenGl_Element.hxx>
#include <OpenGl_Workspace.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_GlCore20.hxx>
#include <SelectMgr_Selection.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRep_Tool.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <limits>
#include <filesystem>
#include <algorithm>
#include <V3d.hxx>
#include <map>
#include <set>
#include <stdexcept>
#include <cmath>
#include <iostream>

namespace inspector {
struct NavigationProbe {
    static auto& view(Viewport& v) { return v.view_; }
    static auto& bar(Viewport& v) { return v.scaleBar_; }
    static auto& cube(Viewport& v) { return v.cube_; }
    static auto& context(Viewport& v) { return v.context_; }
    static auto owner(Viewport& v) { v.context_->InitSelected();return v.context_->SelectedOwner(); }
};
}
namespace {
void check(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
// Explicit contract in V3d enum order; these are outward eye directions, not rays.
constexpr int expected[26][3]={
    {1,0,0},{0,1,0},{0,0,1},{-1,0,0},{0,-1,0},{0,0,-1},
    {1,1,0},{1,0,1},{0,1,1},{-1,-1,0},{-1,1,0},{-1,0,-1},
    {-1,0,1},{0,-1,-1},{0,-1,1},{1,-1,0},{1,0,-1},{0,1,-1},
    {1,1,1},{1,-1,1},{1,1,-1},{-1,1,1},{1,-1,-1},{-1,1,-1},
    {-1,-1,1},{-1,-1,-1}
};
void mouse(inspector::Viewport& v,QEvent::Type type,QPoint physical,
           Qt::MouseButton button,Qt::MouseButtons buttons) {
    const QPointF p=QPointF(physical)/v.devicePixelRatioF();
    QMouseEvent event(type,p,QPointF(v.mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);
    QApplication::sendEvent(&v,&event);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}
void direction(inspector::Viewport& v,int i) {
    const auto c=v.camera();
    check(c->Direction().IsEqual(gp_Dir(-expected[i][0],-expected[i][1],-expected[i][2]),1e-10),"cube click direction contract");
    const gp_Dir up=i==V3d_Zpos?gp::DY():i==V3d_Zneg?-gp::DY():gp::DZ();
    const auto projected=gp_Vec(up)-gp_Vec(c->Direction())*up.Dot(c->Direction());
    check(c->Up().IsEqual(gp_Dir(projected),1e-10),"predictable screen up, including top/bottom");
}
QPointF cubePixel(inspector::Viewport& v,const gp_Pnt& point) {
    const auto c=v.camera();
    auto mv=c->OrientationMatrix();
    const int width=qRound(v.width()*v.devicePixelRatioF()),height=qRound(v.height()*v.devicePixelRatioF());
    inspector::NavigationProbe::cube(v)->TransformPersistence()->Apply(c,c->ProjectionMatrix(),mv,width,height);
    const auto p=c->ProjectionMatrix()*mv*NCollection_Vec4<double>(point.X(),point.Y(),point.Z(),1);
    return {(p.x()/p.w()+1)*width/2,(1-p.y()/p.w())*height/2};
}
void projectedTileClicks(inspector::Viewport& v,int face,const std::string& selected,bool oblique=false) {
    const auto normal=V3d::GetProjAxis(static_cast<V3d_TypeOfOrientation>(face));
    const auto owner=inspector::NavigationProbe::owner(v);
    int clicked=0;
    for(int i=0;i<26;++i) for(const auto& tile:inspector::cubeTiles(static_cast<V3d_TypeOfOrientation>(i),inspector::NavigationProbe::cube(v)->Size()))
        if(tile.normal.IsEqual(normal,1e-12)) {
            v.orient(oblique?V3d_XnegYnegZpos:static_cast<V3d_TypeOfOrientation>(face));
            const auto centre=(tile.corners[0].XYZ()+tile.corners[1].XYZ()+tile.corners[2].XYZ()+tile.corners[3].XYZ())/4;
            const auto pixel=cubePixel(v,gp_Pnt(centre)).toPoint();
            check(v.cubeDirectionAt(pixel.x(),pixel.y())==tile.orientation,"projected visible face/edge/corner tile centre hits its exact owner");
            mouse(v,QEvent::MouseButtonPress,pixel,Qt::LeftButton,Qt::LeftButton);
            mouse(v,QEvent::MouseButtonRelease,pixel,Qt::LeftButton,Qt::NoButton);
            direction(v,i);
            check(v.selectedId()==selected && inspector::NavigationProbe::owner(v)==owner,"projected tile clicks preserve exact model owner");
            ++clicked;
        }
    check(clicked==9,"nine geometrically projected visible tiles actually clicked");
}
void cubeProjectionChecks(inspector::Viewport& v) {
    using inspector::NavigationProbe;
    v.resetCamera();v.orient(V3d_XnegYnegZpos);v.fit();
    const double size=NavigationProbe::cube(v)->Size(),ratio=v.devicePixelRatioF();
    const std::array<gp_Pnt,4> edge={gp_Pnt(-size/2,-size/2,size/2),gp_Pnt(size/2,-size/2,size/2),
        gp_Pnt(-size/2,size/2,size/2),gp_Pnt(size/2,size/2,size/2)};
    std::array<double,2> oppositeEdgeRatios{};
    for(int projection=0;projection<2;++projection) {
        auto& context=NavigationProbe::context(v);
        context->ClearDetected(false);v.redraw();
        const auto centre=cubePixel(v,gp::Origin());
        check(std::abs(centre.x()-100*ratio)<1 && std::abs(centre.y()-(v.height()-100)*ratio)<1,"cube origin remains at its lower-left anchor");
        const auto a=cubePixel(v,edge[0]),b=cubePixel(v,edge[1]),c=cubePixel(v,edge[2]),d=cubePixel(v,edge[3]);
        oppositeEdgeRatios[projection]=QLineF(a,b).length()/QLineF(c,d).length();
        const auto path=std::filesystem::temp_directory_path()/(projection?"geo-vis-refine-02-fix01-projection-P.png":"geo-vis-refine-02-fix01-projection-O.png");
        check(v.capture(path.string()),"cube projection framebuffer");
        const QImage image(QString::fromStdString(path.string()));
        // Off-centre tiles avoid axis labels and antialiased grid boundaries.
        const auto top=cubePixel(v,gp_Pnt(size/3,0,size/2)).toPoint();
        const auto left=cubePixel(v,gp_Pnt(-size/2,0,size/3)).toPoint();
        const auto topColour=image.pixelColor(top),leftColour=image.pixelColor(left);
        check(std::abs(topColour.red()-leftColour.red())+std::abs(topColour.green()-leftColour.green())+
              std::abs(topColour.blue()-leftColour.blue())>90,"adjacent Z+ and X- faces have distinct rendered tones");
        const auto before=v.camera();
        NavigationProbe::view(v)->SetZoom(1.7);NavigationProbe::view(v)->Pan(70,40);v.redraw();
        check(QLineF(a,cubePixel(v,edge[0])).length()<1e-6 && QLineF(b,cubePixel(v,edge[1])).length()<1e-6,"cube position and apparent size invariant under model zoom/pan");
        context->ClearDetected(false);
        const auto stable=std::filesystem::temp_directory_path()/"geo-vis-refine-02-fix01-cube-stable.png";
        check(v.capture(stable.string()),"cube pan/zoom framebuffer");
        const QImage after(QString::fromStdString(stable.string()));
        check(topColour==after.pixelColor(top) && leftColour==after.pixelColor(left),"cube tones remain stable under pan/zoom and model lighting");
        NavigationProbe::view(v)->Camera()->Copy(before);v.redraw();
        v.toggleProjection();
    }
    check(std::abs(oppositeEdgeRatios[0]-1)<1e-10 && std::abs(oppositeEdgeRatios[1]-1)>0.03,"O has parallel equal edges; P has visibly unequal receding edges");
    std::cout<<"Cube opposite-edge ratios O/P: "<<oppositeEdgeRatios[0]<<" / "<<oppositeEdgeRatios[1]<<'\n';
    v.resetCamera();
}
// Read the actual draw framebuffer, without altering GL state or pixels.
class SampleRead : public OpenGl_Element {
public:
    explicit SampleRead(int& samples):samples_(samples) {}
    void Render(const occ::handle<OpenGl_Workspace>& workspace) const override {
        int count=0;workspace->GetGlContext()->core11fwd->glGetIntegerv(GL_SAMPLES,&count);
        samples_=std::max(samples_,count);
    }
    void Release(OpenGl_Context*) override {}
private:
    int& samples_;
};
class SamplePresentation : public AIS_InteractiveObject {
public:
    explicit SamplePresentation(int& samples):samples_(samples) { SetInfiniteState(true); }
    void Compute(const occ::handle<PrsMgr_PresentationManager>&,const occ::handle<Prs3d_Presentation>& presentation,int) override {
        const auto group=occ::down_cast<OpenGl_Group>(presentation->NewGroup());
        group->SetMinMaxValues(-1,-1,-1,1,1,1);
        group->AddElement(new SampleRead(samples_));
    }
    void ComputeSelection(const occ::handle<SelectMgr_Selection>&,int) override {}
private:
    int& samples_;
};
void rasterChecks(inspector::Viewport& v) {
    using inspector::NavigationProbe;
    auto& view=NavigationProbe::view(v);auto& context=NavigationProbe::context(v);
    int observed=-1;
    const occ::handle<SamplePresentation> probe=new SamplePresentation(observed);
    context->Display(probe,0,-1,false);
    const auto limit=context->CurrentViewer()->Driver()->InquireLimit(Graphic3d_TypeOfLimit_MaxMsaa);
    const auto requested=view->RenderingParams().NbMsaaSamples;
    check(requested==4,"modest raster MSAA default requested");
    v.redraw();
    std::cout<<"Raster MSAA: requested "<<requested<<", hardware maximum "<<limit<<", actual draw samples "<<observed<<'\n';
    check(observed>=0 && observed<=4 && observed<=std::max(limit,0),"draw framebuffer uses supported samples or the zero-sample fallback");
    view->ChangeRenderingParams().NbMsaaSamples=0;observed=-1;v.redraw();
    const auto path=std::filesystem::temp_directory_path()/"geo-vis-refine-02-fix01-no-msaa.png";
    check(v.capture(path.string()) && observed==0,"non-multisampled fallback still renders");
    view->ChangeRenderingParams().NbMsaaSamples=requested;v.redraw();
    context->Remove(probe,false);
    check(v.error().empty(),"raster fallback leaves viewport functional");
}
void scaleBarChecks(inspector::Viewport& v,const std::string& suffix) {
    using inspector::NavigationProbe;
    v.redraw();
    const auto& bar=NavigationProbe::bar(v);
    const auto c=v.camera();
    const gp_Vec right=gp_Vec(c->Direction()).Crossed(gp_Vec(c->OrthogonalizedUp()));
    // Independent known 1000 mm distance on the viewing/target plane.
    const double projected=QLineF(v.project(c->Center()),v.project(c->Center().Translated(right*1000))).length();
    check(std::abs(bar->lengthPixels()-projected*bar->lengthMm()/1000)<1.5,"scale bar matches known projected distance");
    check((bar->label().find("at target")!=std::string::npos)==v.perspective(),"Perspective scale explicitly labelled at target");
    check(bar->lengthPixels()<=150*v.devicePixelRatioF()+1 && bar->lengthPixels()>=50*v.devicePixelRatioF(),"rounded bar stays compact at display scaling");
    NCollection_List<int> modes;NavigationProbe::context(v)->ActivatedModes(bar,modes);
    occ::handle<SelectMgr_Selection> selection=new SelectMgr_Selection(0);bar->ComputeSelection(selection,0);
    check(bar->IsInfinite() && modes.IsEmpty() && selection->Entities().IsEmpty(),"scale bar passive and excluded from Fit");
    const auto path=std::filesystem::temp_directory_path()/("geo-vis-scale-"+suffix+".png");
    check(v.capture(path.string()),"scale bar framebuffer");
    QImage image(QString::fromStdString(path.string()));
    // Isolate drawn coverage from an otherwise identical frame. A fixed bright
    // RGB threshold misses partially covered MSAA pixels at DPR 1. Positive
    // coverage relative to the negative control excludes the dark halo, while
    // accepting the light stroke over either background or the amber face.
    const auto withoutPath=std::filesystem::temp_directory_path()/("geo-vis-scale-"+suffix+"-without.png");
    NavigationProbe::context(v)->Erase(bar,false);v.redraw();
    check(v.capture(withoutPath.string()),"scale bar negative-control framebuffer");
    const QImage without(QString::fromStdString(withoutPath.string()));
    NavigationProbe::context(v)->Display(bar,0,-1,false);v.redraw();
    const double ratio=v.devicePixelRatioF();
    const int end=image.width()-qRound(18*ratio),row=image.height()-qRound(18*ratio);
    int longest=0;
    for(int y=row-qRound(2*ratio);y<=row+qRound(2*ratio);++y) {
        int run=0;
        for(int x=std::max(0,end-qRound(160*ratio));x<std::min(image.width(),end+qRound(4*ratio));++x) {
            const auto drawn=image.pixelColor(x,y),base=without.pixelColor(x,y);
            const int lightening=std::max({drawn.red()-base.red(),drawn.green()-base.green(),drawn.blue()-base.blue()});
            run=lightening>8?run+1:0;
            longest=std::max(longest,run);
        }
    }
    std::cout<<"Scale "<<suffix<<": "<<bar->label()<<", expected "<<bar->lengthPixels()<<" px, drawn "<<longest<<" px, DPR "<<ratio<<'\n';
    check(std::abs(longest-bar->lengthPixels())<=4*ratio,"drawn scale bar length matches projected world distance");
}
void passiveScaleChecks(inspector::Viewport& v,const inspector::Display& display) {
    using inspector::NavigationProbe;
    for(bool perspective:{false,true}) {
        v.resetCamera();v.orient(V3d_Yneg);v.fit();
        if(perspective) v.toggleProjection();
        const auto initial=v.camera();
        NavigationProbe::context(v)->Erase(NavigationProbe::bar(v),false);v.fit();
        const auto without=v.camera();
        // Compare the same Fit from the same camera in both visibility states.
        NavigationProbe::view(v)->Camera()->Copy(initial);
        NavigationProbe::context(v)->Display(NavigationProbe::bar(v),0,-1,false);v.fit();
        check(v.camera()->Center().Distance(without->Center())<1e-8 && std::abs(v.camera()->Scale()-without->Scale())<1e-8 &&
              without->Eye().Distance(v.camera()->Eye())<1e-8,"scale bar visibility has no effect on Fit");
        const inspector::Entity* face=nullptr;gp_Pnt interior;
        for(const auto& c:display.components) for(const auto& e:c.entities) if(e.kind==inspector::Kind::Face) {
            GProp_GProps props;BRepGProp::SurfaceProperties(e.display,props);
            if(!face || props.CentreOfMass().Y()<interior.Y()) { face=&e;interior=props.CentreOfMass(); }
        }
        check(face!=nullptr,"front face for scale overlay picking");
        const double ratio=v.devicePixelRatioF();
        const QPoint target(qRound(v.width()*ratio-18*ratio-NavigationProbe::bar(v)->lengthPixels()/2),qRound((v.height()-18)*ratio));
        for(int attempt=0;attempt<4;++attempt) {
            const auto p=v.project(interior);
            NavigationProbe::view(v)->Pan(target.x()-p.x(),p.y()-target.y());v.redraw();
        }
        check((target-v.project(interior)).manhattanLength()<=3,"foreground face panned behind actual scale stroke");
        check(v.pick(target.x(),target.y())==face->id,"scale stroke does not intercept foreground pick");
    }
    std::cout<<"PASS — scale bar does not affect Fit or intercept foreground picks in O/P\n";
}
void endProjectionChecks(inspector::Viewport& v,const inspector::Display& display) {
    std::vector<gp_Pnt> vertices;
    double zmin=std::numeric_limits<double>::max(),zmax=std::numeric_limits<double>::lowest();
    for(const auto& component:display.components)
        for(TopExp_Explorer it(component.display,TopAbs_VERTEX);it.More();it.Next()) {
            const auto p=BRep_Tool::Pnt(TopoDS::Vertex(it.Current()));
            vertices.push_back(p);zmin=std::min(zmin,p.Z());zmax=std::max(zmax,p.Z());
        }
    for(const auto orientation:{V3d_Zpos,V3d_Zneg}) {
        const bool top=orientation==V3d_Zpos;
        const double endZ=top?zmax:zmin;
        const inspector::Entity* endFace=nullptr;
        gp_Pnt interior;
        // Find an interior point of an actual end face, including a concave end.
        // Test point/plane tolerance: 1e-7 mm; never modify the display geometry.
        for(const auto& component:display.components) for(const auto& e:component.entities) {
            if(e.kind!=inspector::Kind::Face) continue;
            bool atEnd=true;
            double xmin=std::numeric_limits<double>::max(),ymin=xmin;
            double xmax=std::numeric_limits<double>::lowest(),ymax=xmax;
            for(TopExp_Explorer it(e.display,TopAbs_VERTEX);it.More();it.Next()) {
                const auto p=BRep_Tool::Pnt(TopoDS::Vertex(it.Current()));
                atEnd=atEnd && std::abs(p.Z()-endZ)<1e-7;
                xmin=std::min(xmin,p.X());xmax=std::max(xmax,p.X());
                ymin=std::min(ymin,p.Y());ymax=std::max(ymax,p.Y());
            }
            if(!atEnd) continue;
            for(double x:{0.25,0.5,0.75}) for(double y:{0.25,0.5,0.75}) {
                const gp_Pnt p(xmin+x*(xmax-xmin),ymin+y*(ymax-ymin),endZ);
                if(BRepClass_FaceClassifier(TopoDS::Face(e.display),p,1e-7).State()==TopAbs_IN) {endFace=&e;interior=p;}
            }
        }
        check(endFace!=nullptr,"combined column has a supported end face");
        v.resetCamera();v.orient(orientation);v.fit();
        check(v.select(endFace->id),"end face selected before Top/Bottom Fit P");
        const auto before=v.camera();
        const auto oldCallback=v.selectionChanged;
        int notifications=0;v.selectionChanged=[&](const std::string&) {++notifications;};
        v.toggleProjection();
        const auto after=v.camera();
        double minDepth=std::numeric_limits<double>::max();
        for(const auto& p:vertices) minDepth=std::min(minDepth,gp_Vec(after->Eye(),p).Dot(gp_Vec(after->Direction())));
        std::cout<<(top?"Top":"Bottom")<<" Fit P: scale "<<before->Scale()<<" -> "<<after->Scale()
                 <<", eye distance "<<before->Distance()<<" -> "<<after->Distance()<<", nearest geometry depth "<<minDepth<<std::endl;
        check(minDepth>0 && minDepth>after->ZNear(),"Perspective eye and near plane stay in front of the combined column");
        check(after->IsOrthographic()==false && after->Scale()>before->Scale(),"unsafe end-view scale backs off in Perspective");
        check(before->Direction().IsEqual(after->Direction(),1e-12) && before->Up().IsEqual(after->Up(),1e-12) &&
              before->Center().Distance(after->Center())<1e-8,"end-view backoff preserves direction/up/target");
        check(v.selectedId()==endFace->id && notifications==0,"end-view backoff preserves selection without notification");
        const auto pixel=v.project(interior);
        check(pixel.x()>0 && pixel.x()<qRound(v.width()*v.devicePixelRatioF()) && pixel.y()>0 && pixel.y()<qRound(v.height()*v.devicePixelRatioF()),"end-face interior projects inside viewport");
        for(TopExp_Explorer it(endFace->display,TopAbs_VERTEX);it.More();it.Next()) {
            const auto p=v.project(BRep_Tool::Pnt(TopoDS::Vertex(it.Current())));
            check(p.x()>0 && p.x()<qRound(v.width()*v.devicePixelRatioF()) && p.y()>0 && p.y()<qRound(v.height()*v.devicePixelRatioF()),"entire end-face boundary remains inside viewport");
        }
        const auto image=std::filesystem::temp_directory_path()/(top?"geo-vis-camera-top-fit-perspective.png":"geo-vis-camera-bottom-fit-perspective.png");
        check(v.capture(image.string()),"Top/Bottom Fit P framebuffer");
        check(v.pick(pixel.x(),pixel.y())==endFace->id,"actual Perspective end face stays visible and pickable");
        v.selectionChanged=oldCallback;
    }
    std::cout<<"PASS — combined Top/Bottom Fit P eye clearance and rendered end faces\n";
}

}
void cameraSnapClearanceChecks(inspector::Viewport& v,const inspector::Display& candidate) {
    using inspector::NavigationProbe;
    v.showDisplay(&candidate);v.setPickMode(inspector::Kind::Face);
    for(const auto orientation:{V3d_Zpos,V3d_Zneg}) {
        v.resetCamera();v.orient(orientation);v.fit();
        scaleBarChecks(v,orientation==V3d_Zpos?"top-end-orthographic":"bottom-end-orthographic");
        check(NavigationProbe::bar(v)->label().find(" mm")!=std::string::npos,"small fitted end view uses millimetre label");
        v.orient(V3d_Xpos);
        v.toggleProjection(); // Same action as P; side has clearance without Fit.
        const auto before=v.camera();
        const auto& component=candidate.components.front();
        double endZ=orientation==V3d_Zpos?std::numeric_limits<double>::lowest():std::numeric_limits<double>::max();
        for(TopExp_Explorer it(component.display,TopAbs_VERTEX);it.More();it.Next()) {
            const double z=BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).Z();
            endZ=orientation==V3d_Zpos?std::max(endZ,z):std::min(endZ,z);
        }
        const inspector::Entity* end=nullptr;gp_Pnt interior;
        for(const auto& e:component.entities) if(e.kind==inspector::Kind::Face) {
            GProp_GProps props;BRepGProp::SurfaceProperties(e.display,props);
            const auto p=props.CentreOfMass();
            if(std::abs(p.Z()-endZ)<1e-7) {end=&e;interior=p;break;}
        }
        check(end!=nullptr && v.select(end->id),"B end face selected before unsafe cube snap");
        double xmin=std::numeric_limits<double>::max(),ymin=xmin;
        double xmax=std::numeric_limits<double>::lowest(),ymax=xmax;
        for(TopExp_Explorer it(end->display,TopAbs_VERTEX);it.More();it.Next()) {
            const auto p=BRep_Tool::Pnt(TopoDS::Vertex(it.Current()));
            xmin=std::min(xmin,p.X());xmax=std::max(xmax,p.X());
            ymin=std::min(ymin,p.Y());ymax=std::max(ymax,p.Y());
        }
        // Avoid the passive origin arrows projected onto the face centre.
        interior=gp_Pnt(xmin+0.75*(xmax-xmin),ymin+0.75*(ymax-ymin),endZ);
        check(BRepClass_FaceClassifier(TopoDS::Face(end->display),interior,1e-7).State()==TopAbs_IN,"B end framebuffer probe is strictly interior");
        const auto owner=NavigationProbe::owner(v);
        const auto callback=v.selectionChanged;int notifications=0;
        v.selectionChanged=[&](const std::string&) {++notifications;};
        v.orient(orientation);
        const auto after=v.camera();
        double nearest=std::numeric_limits<double>::max();
        for(TopExp_Explorer it(component.display,TopAbs_VERTEX);it.More();it.Next())
            nearest=std::min(nearest,gp_Vec(after->Eye(),BRep_Tool::Pnt(TopoDS::Vertex(it.Current()))).Dot(gp_Vec(after->Direction())));
        check(after->Distance()>before->Distance() && nearest>after->ZNear() && nearest>0,"cube snap retreats outside foreground before near clipping");
        check(after->Center().Distance(before->Center())<1e-8 && !after->IsOrthographic(),"cube snap keeps target and Perspective");
        direction(v,int(orientation));
        check(v.selectedId()==end->id && NavigationProbe::owner(v)==owner && notifications==0,"unsafe cube snap preserves exact owner without notification");
        const auto point=v.project(interior);
        check(v.pick(point.x(),point.y())==end->id,"retreated B end face visible and pickable");
        scaleBarChecks(v,orientation==V3d_Zpos?"top-retreat":"bottom-retreat");
        const auto imagePath=std::filesystem::temp_directory_path()/(orientation==V3d_Zpos?"geo-vis-scale-top-retreat.png":"geo-vis-scale-bottom-retreat.png");
        const QImage image(QString::fromStdString(imagePath.string()));
        const auto colour=image.pixelColor(point);
        check(colour.red()>colour.blue()+30 && colour.green()>colour.blue()+30,"retreated selected B end has visible amber framebuffer fill");
        for(TopExp_Explorer it(end->display,TopAbs_VERTEX);it.More();it.Next()) {
            const auto p=v.project(BRep_Tool::Pnt(TopoDS::Vertex(it.Current())));
            check(p.x()>0 && p.y()>0 && p.x()<image.width() && p.y()<image.height(),"retreated B end boundary stays inside viewport");
        }
        const auto safe=v.camera();v.orient(V3d_Xpos);
        check(v.camera()->Distance()==safe->Distance() && v.camera()->Scale()==safe->Scale(),"safe Perspective cube snap retains eye distance and scale");
        v.selectionChanged=callback;
        std::cout<<(orientation==V3d_Zpos?"Top":"Bottom")<<" B side-P-snap: distance "<<before->Distance()<<" -> "<<after->Distance()<<", nearest depth "<<nearest<<'\n';
    }
    v.resetCamera();v.showDisplay(nullptr);
    std::cout<<"PASS — combined B end-on Orthographic Fit, side without Fit, P, Top/Bottom snap clearance\n";
}
void cameraMappingChecks() {
    std::set<std::array<int,3>> directions;
    int count=0;
    for(int i=0;i<26;++i) {
        const auto orientation=static_cast<V3d_TypeOfOrientation>(i);
        const auto d=V3d::GetProjAxis(orientation);
        check(d.IsEqual(gp_Dir(expected[i][0],expected[i][1],expected[i][2]),1e-12),"explicit 26-direction mapping");
        directions.insert({expected[i][0],expected[i][1],expected[i][2]});
        const auto tiles=inspector::cubeTiles(orientation,90);
        check(tiles.size()==std::size_t(std::abs(expected[i][0])+std::abs(expected[i][1])+std::abs(expected[i][2])),"shared edge/corner owns tiles on all incident faces");
        std::set<int> axes;
        for(const auto& t:tiles) {
            ++count;
            const gp_XYZ center=(t.corners[0].XYZ()+t.corners[1].XYZ()+t.corners[2].XYZ()+t.corners[3].XYZ())/4;
            for(int axis=0;axis<3;++axis) {
                if(std::abs(t.normal.Coord(axis+1))>0.5) {
                    check(axes.insert(axis).second,"one tile per incident face");
                    check(std::abs(center.Coord(axis+1)-expected[i][axis]*45)<1e-10,"tile on correct signed face");
                } else check(std::abs(center.Coord(axis+1)-expected[i][axis]*30)<1e-10,"tile in correct third of face");
            }
            const auto cross=gp_Vec(t.corners[0],t.corners[1]).Crossed(gp_Vec(t.corners[1],t.corners[2]));
            check(gp_Dir(cross).IsEqual(t.normal,1e-12),"outward tile winding");
            check(t.orientation==orientation,"shared tile direction identity");
        }
    }
    check(directions.size()==26 && count==54,"six 3x3 faces, 26 unique owners");
    std::cout<<"PASS — 26 explicit directions and all 54 outward tiles\n";
}
void cameraRenderChecks(inspector::Viewport& v,const inspector::Display& display) {
    v.showDisplay(&display);v.resetCamera();QApplication::processEvents();
    std::string selected;
    for(const auto& c:display.components) for(const auto& e:c.entities) if(e.kind==inspector::Kind::Face) { selected=e.id;break; }
    v.setPickMode(inspector::Kind::Face);check(v.select(selected),"selected camera-test face");
    const auto selectedOwner=inspector::NavigationProbe::owner(v);
    cubeProjectionChecks(v);
    rasterChecks(v);
    // Compare actual render pixels rather than only checking detection metadata.
    const auto imagePath=std::filesystem::temp_directory_path()/"geo-vis-camera-cube-idle.png";
    QEvent leave(QEvent::Leave);QApplication::sendEvent(&v,&leave);
    check(v.capture(imagePath.string()),"idle cube framebuffer");
    QImage idle(QString::fromStdString(imagePath.string()));
    QPoint cubeCenter(qRound(100*v.devicePixelRatioF()),qRound((v.height()-100)*v.devicePixelRatioF()));
    bool hoverTarget=false;
    for(int offset=0;offset<30 && !hoverTarget;++offset) {
        if(v.cubeDirectionAt(cubeCenter.x()+offset,cubeCenter.y())) {cubeCenter.rx()+=offset;hoverTarget=true;}
    }
    check(hoverTarget,"visible cube hover target");
    mouse(v,QEvent::MouseMove,cubeCenter,Qt::NoButton,Qt::NoButton);
    const auto hoverPath=std::filesystem::temp_directory_path()/"geo-vis-camera-cube-hover.png";
    check(v.capture(hoverPath.string()),"hover cube framebuffer");
    QImage hover(QString::fromStdString(hoverPath.string()));
    int changed=0;
    for(int y=std::max(0,idle.height()-qRound(200*v.devicePixelRatioF()));y<idle.height();++y)
        for(int x=0;x<std::min(idle.width(),qRound(200*v.devicePixelRatioF()));++x)
            changed+=idle.pixel(x,y)!=hover.pixel(x,y);
    check(changed>20,"visible cube hover feedback in framebuffer");
    const auto beforeOrbit=v.camera();
    const QPoint orbitStart(qRound(v.width()*v.devicePixelRatioF()/2),qRound(v.height()*v.devicePixelRatioF()/2));
    mouse(v,QEvent::MouseButtonPress,orbitStart,Qt::LeftButton,Qt::LeftButton);
    mouse(v,QEvent::MouseMove,orbitStart+QPoint(70,40),Qt::NoButton,Qt::LeftButton);
    mouse(v,QEvent::MouseButtonRelease,orbitStart+QPoint(70,40),Qt::LeftButton,Qt::NoButton);
    QApplication::sendEvent(&v,&leave);
    const auto orbitPath=std::filesystem::temp_directory_path()/"geo-vis-camera-cube-orbit.png";
    check(v.capture(orbitPath.string()),"orbit cube framebuffer");
    QImage orbit(QString::fromStdString(orbitPath.string()));
    changed=0;
    for(int y=std::max(0,idle.height()-qRound(200*v.devicePixelRatioF()));y<idle.height();++y)
        for(int x=0;x<std::min(idle.width(),qRound(200*v.devicePixelRatioF()));++x)
            changed+=idle.pixel(x,y)!=orbit.pixel(x,y);
    check(changed>20 && !beforeOrbit->Direction().IsEqual(v.camera()->Direction(),1e-6),"cube presentation follows viewport orbit");
    v.resetCamera();
    int selections=0;
    const auto callback=v.selectionChanged;
    v.selectionChanged=[&](const std::string&) {++selections;};
    for(int projection=0;projection<2;++projection) {
        check(v.perspective()==(projection==1),"projection test setup");
        for(int face:{V3d_Xneg,V3d_Yneg,V3d_Zpos}) projectedTileClicks(v,face,selected,true);
        std::set<int> clicked;
        for(int face=0;face<6;++face) {
            projectedTileClicks(v,face,selected);
            v.orient(static_cast<V3d_TypeOfOrientation>(face));
            const int h=qRound(v.height()*v.devicePixelRatioF());
            std::map<int,QPoint> hits;
            const double ratio=v.devicePixelRatioF();
            for(int y=h-qRound(145*ratio);y<h-qRound(55*ratio);y+=qRound(5*ratio))
                for(int x=qRound(55*ratio);x<qRound(145*ratio);x+=qRound(5*ratio))
                if(const auto hit=v.cubeDirectionAt(x,y)) hits.try_emplace(int(*hit),QPoint(x,y));
            if(hits.size()!=9) std::cerr<<"Cube face "<<face<<" in projection "<<projection<<" has "<<hits.size()<<" targets\n";
            check(hits.size()==9,"nine real sensitive targets on each principal cube face");
            for(const auto& [target,p]:hits) {
                check(expected[target][face%3]==(face<3?1:-1),"visible face targets map to that face's signed axis");
                v.orient(static_cast<V3d_TypeOfOrientation>(face));
                const auto before=v.camera();
                mouse(v,QEvent::MouseMove,p,Qt::NoButton,Qt::NoButton);
                mouse(v,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);
                mouse(v,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);
                direction(v,target);
                const auto after=v.camera();
                check(after->Center().Distance(before->Center())<1e-8 && std::abs(after->Scale()-before->Scale())<1e-8,"cube keeps target and scale");
                check(v.perspective()==(projection==1) && v.selectedId()==selected && inspector::NavigationProbe::owner(v)==selectedOwner && selections==0,"cube preserves projection and exact selected owner without callback");
                clicked.insert(target);
            }
        }
        check(clicked.size()==26,"all 26 directions clicked in real viewport");
        // Consumes empty padding too, including when the shape has been panned behind it.
        v.orient(V3d_XposYnegZpos);v.fit();
        const auto fitted=v.camera();
        const QPoint start(qRound(v.width()*v.devicePixelRatioF()/2),qRound(v.height()*v.devicePixelRatioF()/2));
        // O/P tile locations differ. Use a visible X+ face-centre in the
        // active projection, rather than reusing an O hover pixel in a P gap.
        const QPoint end=cubePixel(v,gp_Pnt(inspector::NavigationProbe::cube(v)->Size()/2,0,0)).toPoint();
        mouse(v,QEvent::MouseButtonPress,start,Qt::RightButton,Qt::RightButton);
        mouse(v,QEvent::MouseMove,end,Qt::NoButton,Qt::RightButton);
        mouse(v,QEvent::MouseButtonRelease,end,Qt::RightButton,Qt::NoButton);
        GProp_GProps properties;BRepGProp::VolumeProperties(display.components.front().display,properties);
        // Perspective pan is measured at the target plane; correct for the
        // solid centre's different depth before testing actual overlap.
        for(int attempt=0;attempt<3 && (v.project(properties.CentreOfMass())-end).manhattanLength()>10;++attempt) {
            const QPoint correction=end-v.project(properties.CentreOfMass());
            mouse(v,QEvent::MouseButtonPress,start,Qt::RightButton,Qt::RightButton);
            mouse(v,QEvent::MouseMove,start+correction,Qt::NoButton,Qt::RightButton);
            mouse(v,QEvent::MouseButtonRelease,start+correction,Qt::RightButton,Qt::NoButton);
        }
        check((v.project(properties.CentreOfMass())-end).manhattanLength()<=10,"model panned behind navigation cube");
        check(v.cubeDirectionAt(end.x(),end.y()).has_value(),"cube detects ahead of geometry behind it");
        mouse(v,QEvent::MouseButtonPress,end,Qt::LeftButton,Qt::LeftButton);
        mouse(v,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
        check(v.selectedId()==selected && selections==0,"cube over geometry preserves model owner");
        const auto before=v.camera();
        v.pick(8,qRound(v.height()*v.devicePixelRatioF())-8);
        check(v.selectedId()==selected && selections==0,"cube padding never deselects/picks the model");
        // Compare cube and ordinary viewport orbit from identical cameras.
        const auto owner=inspector::NavigationProbe::owner(v);
        const double ratio=v.devicePixelRatioF();
        const QPoint delta(qRound(25*ratio),qRound(10*ratio));
        mouse(v,QEvent::MouseButtonPress,end,Qt::LeftButton,Qt::LeftButton);
        check(QWidget::mouseGrabber()==&v,"cube gesture captures pointer");
        mouse(v,QEvent::MouseMove,end+delta,Qt::NoButton,Qt::LeftButton);
        const auto cubeOrbit=v.camera();
        mouse(v,QEvent::MouseButtonRelease,end+delta,Qt::LeftButton,Qt::NoButton);
        check(!before->Direction().IsEqual(cubeOrbit->Direction(),1e-6) && cubeOrbit->Direction().IsEqual(v.camera()->Direction(),1e-12),"cube drag orbits without release snap");
        inspector::NavigationProbe::view(v)->Camera()->Copy(before);v.redraw();
        mouse(v,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
        mouse(v,QEvent::MouseMove,start+delta,Qt::NoButton,Qt::LeftButton);
        mouse(v,QEvent::MouseButtonRelease,start+delta,Qt::LeftButton,Qt::NoButton);
        check(cubeOrbit->Direction().IsEqual(v.camera()->Direction(),1e-12) && cubeOrbit->Up().IsEqual(v.camera()->Up(),1e-12),"cube drag has same sense and amount as viewport orbit");
        const QPoint outside(qRound((v.width()+30)*ratio),qRound((v.height()+30)*ratio));
        mouse(v,QEvent::MouseButtonPress,end,Qt::LeftButton,Qt::LeftButton);
        mouse(v,QEvent::MouseMove,outside,Qt::NoButton,Qt::LeftButton);
        const auto outsideCamera=v.camera();
        mouse(v,QEvent::MouseButtonRelease,outside,Qt::LeftButton,Qt::NoButton);
        check(QWidget::mouseGrabber()!=&v && outsideCamera->Direction().IsEqual(v.camera()->Direction(),1e-12),"release outside viewport ends grab without snap");
        check(v.selectedId()==selected && inspector::NavigationProbe::owner(v)==owner && selections==0 && v.perspective()==(projection==1),"cube drags retain exact selected owner and projection");
        v.orient(V3d_XposYnegZpos);
        const auto clickDirection=v.cubeDirectionAt(end.x(),end.y());
        check(clickDirection.has_value(),"short-click cube target");
        mouse(v,QEvent::MouseButtonPress,end,Qt::LeftButton,Qt::LeftButton);
        mouse(v,QEvent::MouseMove,end+QPoint(qRound(ratio),qRound(ratio)),Qt::NoButton,Qt::LeftButton);
        mouse(v,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
        direction(v,int(*clickDirection));
        v.orient(V3d_XposYnegZpos);
        mouse(v,QEvent::MouseButtonPress,end,Qt::LeftButton,Qt::LeftButton);
        mouse(v,QEvent::MouseMove,end+delta,Qt::NoButton,Qt::LeftButton);
        mouse(v,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
        const auto returned=v.camera();
        mouse(v,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
        check(returned->Direction().IsEqual(v.camera()->Direction(),1e-12) && v.selectedId()==selected && selections==0,"drag returning to tile stays a drag without snap or pick");
        // A chorded press/release must not restart or finish a left-owned drag.
        for(const auto extra:{Qt::RightButton,Qt::MiddleButton}) {
            v.orient(V3d_XposYnegZpos);
            mouse(v,QEvent::MouseButtonPress,end,Qt::LeftButton,Qt::LeftButton);
            mouse(v,QEvent::MouseMove,end+delta,Qt::NoButton,Qt::LeftButton);
            const auto started=v.camera();
            mouse(v,QEvent::MouseButtonPress,end+delta,extra,Qt::LeftButton|extra);
            check(QWidget::mouseGrabber()==&v,"extra button press preserves left cube capture");
            mouse(v,QEvent::MouseButtonRelease,end+delta,extra,Qt::LeftButton);
            check(QWidget::mouseGrabber()==&v && started->Direction().IsEqual(v.camera()->Direction(),1e-12),"extra button release preserves left cube state and capture");
            mouse(v,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
            const auto returnedChord=v.camera();
            // A reset would use the second press as orbit origin, producing a
            // different result when returning to the original left press point.
            check(returnedChord->Direction().IsEqual(returned->Direction(),1e-12),"extra press does not reset cube orbit origin");
            mouse(v,QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
            check(QWidget::mouseGrabber()!=&v && returnedChord->Direction().IsEqual(v.camera()->Direction(),1e-12) &&
                  v.selectedId()==selected && inspector::NavigationProbe::owner(v)==owner && selections==0,"final left release ends capture without snap or selected owner change");
        }
        v.orient(V3d_XposYnegZpos);v.fit();
        check(v.camera()->Center().Distance(fitted->Center())<1e-7 && std::abs(v.camera()->Scale()-fitted->Scale())<1e-7,"screen cube excluded from Fit bounds");
        v.toggleProjection();
    }
    // Return to O and check target-plane scale across O/P/O after pan/orbit/zoom.
    const auto original=v.camera();
    for(int i=0;i<2;++i) {
        const auto before=v.camera();
        const gp_Pnt targetPlane=before->Center().Translated(gp_Vec(before->Up())*(before->Scale()/5));
        const auto pixel=v.project(targetPlane);
        v.toggleProjection();const auto after=v.camera();
        check((pixel-v.project(targetPlane)).manhattanLength()<=1,"same apparent scale measured in target-plane pixels");
        check(before->Direction().IsEqual(after->Direction(),1e-12) && before->Up().IsEqual(after->Up(),1e-12),"toggle preserves direction/up");
        check(before->Center().Distance(after->Center())<1e-8 && std::abs(before->Scale()-after->Scale())<1e-8,"toggle preserves target/apparent target-plane scale");
        check(v.selectedId()==selected && selections==0,"toggle keeps model selection and callback count");
    }
    check(v.camera()->ProjectionType()==original->ProjectionType(),"O/P/O projection round trip");
    // Picking outside the control still follows normal model/background selection.
    v.pick(0,0);check(v.selectedId().empty() && selections==1,"normal background picking outside cube");
    v.selectionChanged=callback;
    endProjectionChecks(v,display);
    v.resetCamera();scaleBarChecks(v,"orthographic-fit");
    const QPointF wheelPoint(v.width()/2.,v.height()/2.);
    QWheelEvent wheel(wheelPoint,QPointF(v.mapToGlobal(wheelPoint.toPoint())),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(&v,&wheel);scaleBarChecks(v,"orthographic-zoom");
    v.toggleProjection();scaleBarChecks(v,"perspective");
    v.resize(1000,650);QApplication::processEvents();v.redraw();scaleBarChecks(v,"perspective-resize");
    v.resize(900,800);QApplication::processEvents();
    passiveScaleChecks(v,display);
    v.resetCamera();
    v.showDisplay(nullptr);
    check(v.error().empty(),"camera viewport error");
    std::cout<<"PASS — real cube clicks: all 26 directions from 54 tiles and oblique tile centres in both projections; left-owned right/middle chords and camera/selection isolation\n";
}
