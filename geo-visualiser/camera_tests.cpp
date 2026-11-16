#include "camera_tests.hpp"
#include <QApplication>
#include <QMouseEvent>
#include <QImage>
#include <QEvent>
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
}
void direction(inspector::Viewport& v,int i) {
    const auto c=v.camera();
    check(c->Direction().IsEqual(gp_Dir(-expected[i][0],-expected[i][1],-expected[i][2]),1e-10),"cube click direction contract");
    const gp_Dir up=i==V3d_Zpos?gp::DY():i==V3d_Zneg?-gp::DY():gp::DZ();
    const auto projected=gp_Vec(up)-gp_Vec(c->Direction())*up.Dot(c->Direction());
    check(c->Up().IsEqual(gp_Dir(projected),1e-10),"predictable screen up, including top/bottom");
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
        std::set<int> clicked;
        for(int face=0;face<6;++face) {
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
                check(v.perspective()==(projection==1) && v.selectedId()==selected && selections==0,"cube preserves projection and selected model without callback");
                clicked.insert(target);
            }
        }
        check(clicked.size()==26,"all 26 directions clicked in real viewport");
        // Consumes empty padding too, including when the shape has been panned behind it.
        v.orient(V3d_XposYnegZpos);v.fit();
        const auto fitted=v.camera();
        const QPoint start(qRound(v.width()*v.devicePixelRatioF()/2),qRound(v.height()*v.devicePixelRatioF()/2));
        const QPoint end=cubeCenter;
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
        // A cube drag is deliberately a no-op, rather than orbiting the shape.
        mouse(v,QEvent::MouseButtonPress,end,Qt::LeftButton,Qt::LeftButton);
        mouse(v,QEvent::MouseMove,end+QPoint(25,10),Qt::NoButton,Qt::LeftButton);
        mouse(v,QEvent::MouseButtonRelease,end+QPoint(25,10),Qt::LeftButton,Qt::NoButton);
        check(before->Direction().IsEqual(v.camera()->Direction(),1e-12),"cube dragging disabled");
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
    v.resetCamera();
    v.showDisplay(nullptr);
    check(v.error().empty(),"camera viewport error");
    std::cout<<"PASS — real cube clicks: all 26 directions from 54 tiles in both projections; camera/selection isolation\n";
}
