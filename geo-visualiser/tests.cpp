#include "model.hpp"
#include "viewport.hpp"
#include "window.hpp"
#include "smd/json.hpp"
#include <QApplication>
#include <QPushButton>
#include <QImage>
#include <QScreen>
#include <QMouseEvent>
#include <QWheelEvent>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <bit>
#include <cstdint>
#include <cmath>
#include <functional>
#include <iostream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using namespace inspector;
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher>;
void require(bool value, const std::string& why) { if(!value) throw std::runtime_error(why); }
void flags(std::ostream& out, const TopoDS_Shape& s) {
    if(s.IsNull()) { out << "null "; return; }
    out << s.TShape().get() << ' ' << s.Orientation() << ' ' << s.Free() << s.Locked()
        << s.Modified() << s.Checked() << s.Orientable() << s.Closed() << s.Infinite() << s.Convex() << ' ';
    for(TopoDS_Iterator it(s); it.More(); it.Next()) flags(out,it.Value());
}
void shape(std::ostream& out, const TopoDS_Shape& s) {
    if(!s.IsNull()) BRepTools::Write(s,out);
    flags(out,s);
}
void boundary(std::ostream& out, const smd_geometry::ShellAssembly& b) {
    shape(out,b.shell);
    for(const auto& v:b.vertices) shape(out,v);
    for(const auto& f:b.faces) shape(out,f);
    for(const auto& e:b.edges) {
        shape(out,e.edge); out << e.firstVertex << ' ' << e.secondVertex << ' ';
        for(const auto& u:e.uses) out << u.faceOrdinal << ' ' << u.loopPosition << ' ' << u.forward << ' ';
    }
    for(const auto& loop:b.loopEdges) for(const auto& e:loop) shape(out,e);
    for(auto f:b.reversedFaceOrdinals) out << f << ' ';
}
void ancestor(std::ostream& out, const smd_geometry::FaceAncestor& a) {
    out << static_cast<int>(a.origin) << ' ' << static_cast<int>(a.change) << ' ' << a.sourceFaceOrdinal.has_value() << ' ';
    if(a.sourceFaceOrdinal) out << *a.sourceFaceOrdinal << ' ';
}
std::string snapshot(const Document& d) {
    std::ostringstream out;
    out << std::setprecision(17) << smd::toJsonV1(d.parsed);
    const auto number = [&](double x) { out << std::bit_cast<std::uint64_t>(x) << ' '; };
    // Bitwise source B-rep evidence supplements the diagnostic JSON snapshot.
    const auto& b=d.parsed.geometry.brep;
    for(const auto* vertices:{&b.verticesLocal,&b.verticesWorld}) {
        out << vertices->size() << ' ';
        for(auto v:*vertices) { number(v.x);number(v.y);number(v.z); }
    }
    for(const auto& f:b.faces) {
        number(f.flag); out << f.indices.size() << ' '; for(auto v:f.indices) out << v << ' ';
        out << f.plane.has_value() << ' '; if(f.plane) { number(f.plane->a);number(f.plane->b);number(f.plane->c);number(f.plane->d); }
        out << f.name.has_value() << ' '; if(f.name) out << std::quoted(*f.name);
    }
    for(const auto& p:b.planes) { number(p.a);number(p.b);number(p.c);number(p.d); }
    for(const auto& n:b.faceNames) out << std::quoted(n);
    out << b.transform.has_value(); if(b.transform) for(auto n:b.transform->values) number(n);
    out << b.vertexCountTokenIndex;
    for(const auto& v:d.converted.vertices) shape(out,v);
    for(const auto& f:d.converted.faces) shape(out,f);
    boundary(out,d.shell); shape(out,d.source.solid); boundary(out,d.source.boundary);
    out << d.source.globallyReversed; number(d.source.volumeMm3);
    shape(out,d.candidate.solid); out << d.candidate.supportingFaceOrdinal;
    for(auto v:{d.candidate.minimumLocalMm,d.candidate.maximumLocalMm,d.candidate.dimensionsMm}) {number(v.x);number(v.y);number(v.z);}
    const auto body=[&](const smd_geometry::BooleanSolid& s) {
        shape(out,s.solid);number(s.volumeMm3);
        for(const auto& f:s.faces) {shape(out,f.face);for(const auto& a:f.ancestors) ancestor(out,a);}
    };
    for(const auto& s:d.decomposition.extensions) body(s);
    for(const auto& s:d.decomposition.reductions) body(s);
    body(d.decomposition.reconstructed);
    for(const auto& h:d.decomposition.history) {out << h.stage;ancestor(out,h.ancestor);out << static_cast<int>(h.change);for(const auto& f:h.descendants) shape(out,f);}
    return out.str();
}
void independent(const Component& c) {
    ShapeMap original,display;
    TopExp::MapShapes(c.original,original);TopExp::MapShapes(c.display,display);
    require(original.Extent()==display.Extent(),"copy topology size");
    for(int i=1;i<=display.Extent();++i) {
        require(!original.Contains(display(i)),"private display topology");
        for(int j=1;j<=original.Extent();++j) {
            if(display(i).ShapeType()==TopAbs_FACE && original(j).ShapeType()==TopAbs_FACE)
                require(BRep_Tool::Surface(TopoDS::Face(display(i)))!=BRep_Tool::Surface(TopoDS::Face(original(j))),"private display surface");
            if(display(i).ShapeType()==TopAbs_EDGE && original(j).ShapeType()==TopAbs_EDGE) {
                double a,b,x,y;
                require(BRep_Tool::Curve(TopoDS::Edge(display(i)),a,b)!=BRep_Tool::Curve(TopoDS::Edge(original(j)),x,y),"private display curve");
            }
        }
    }
    for(const auto& e:c.entities) {
        require(original.Contains(e.original) && display.Contains(e.display),"explicit copy correspondence");
        require(e.original.ShapeType()==e.display.ShapeType(),"copy type");
        if(e.kind==Kind::Vertex) require(BRep_Tool::Pnt(TopoDS::Vertex(e.original)).IsEqual(BRep_Tool::Pnt(TopoDS::Vertex(e.display)),0),"unmoved display point");
    }
}
void rejects(const std::function<void()>& action, const std::string& expected) {
    try { action(); } catch(const std::exception& e) {
        require(std::string(e.what()).find(expected)!=std::string::npos,std::string("diagnostic: ")+e.what());
        return;
    }
    throw std::runtime_error("expected rejection: "+expected);
}
const Entity& first(const Display& d, Kind kind) {
    for(const auto& c:d.components) for(const auto& e:c.entities) if(e.kind==kind) return e;
    throw std::runtime_error("entity absent");
}
void navigation(Viewport& viewport,const Display& display) {
    if(display.components.empty()) return;
    const auto point=BRep_Tool::Pnt(TopoDS::Vertex(first(display,Kind::Vertex).display));
    const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons) {
        QMouseEvent event(type,p,QPointF(viewport.mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);
        QApplication::sendEvent(&viewport,&event);
    };
    const auto drag=[&](Qt::MouseButton button) {
        mouse(QEvent::MouseButtonPress,{450,400},button,button);
        mouse(QEvent::MouseMove,{475,420},Qt::NoButton,button);
        mouse(QEvent::MouseButtonRelease,{475,420},button,Qt::NoButton);
    };
    auto before=viewport.project(point);drag(Qt::LeftButton);
    require(viewport.project(point)!=before,"orbit changes camera projection");
    before=viewport.project(point);drag(Qt::RightButton);
    require(viewport.project(point)!=before,"pan changes camera projection");
    before=viewport.project(point);
    QWheelEvent wheel(QPointF(450,400),QPointF(viewport.mapToGlobal(QPoint(450,400))),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(&viewport,&wheel);
    require(viewport.project(point)!=before,"wheel zoom changes camera projection");
    before=viewport.project(point);viewport.fit();
    require(viewport.project(point)!=before,"fit changes camera projection");
}
void render(Viewport& viewport, Display& display) {
    viewport.showDisplay(&display);
    QApplication::processEvents();
    require(viewport.error().empty(),viewport.error());
    navigation(viewport,display);
    if(!display.components.empty()) {
        auto label=display.components.front().id;
        for(auto& ch:label) if(ch=='/') ch='-';
        const auto path=std::filesystem::temp_directory_path()/("geo-visualiser-"+label+".png");
        require(viewport.capture(path.string()),"actual framebuffer capture");
        QImage pixels(QString::fromStdString(path.string()));
        const auto background=pixels.pixel(0,0);
        std::size_t foreground=0;
        for(int y=0;y<pixels.height();++y) for(int x=0;x<pixels.width();++x) foreground+=pixels.pixel(x,y)!=background;
        require(foreground>100,"nonempty rendered geometry in framebuffer");
    }
    for(int isolated=-1;isolated<static_cast<int>(display.components.size());++isolated) {
        viewport.showDisplay(&display,isolated);
        QApplication::processEvents();
        for(auto kind:{Kind::Vertex,Kind::Edge,Kind::Face,Kind::Body}) {
            QApplication::processEvents();
            viewport.setPickMode(kind);
            require(viewport.selectedId().empty(),"mode change clears selection");
            bool picked=false,expected=false;
            for(std::size_t i=0;i<display.components.size();++i) for(const auto& e:display.components[i].entities) {
                if(e.kind!=kind) continue;
                if(isolated>=0 && i!=static_cast<std::size_t>(isolated)) {
                    require(!viewport.select(e.id),"hidden entity cannot be selected");continue;
                }
                expected=true;
                require(viewport.select(e.id),"tree-to-AIS owner: "+e.id);
                require(viewport.selectedId()==e.id,"one selected mapped owner");
                gp_Pnt point;
                if(kind==Kind::Vertex) point=BRep_Tool::Pnt(TopoDS::Vertex(e.display));
                else {
                    GProp_GProps props;
                    if(kind==Kind::Edge) BRepGProp::LinearProperties(e.display,props);
                    else if(kind==Kind::Face) BRepGProp::SurfaceProperties(e.display,props);
                    else BRepGProp::VolumeProperties(e.display,props);
                    point=props.CentreOfMass();
                }
                const auto pixel=viewport.project(point);
                auto id=viewport.pick(pixel.x(),pixel.y());
                if(!id.empty()) {
                    picked=true;
                    require(viewport.selectedId()==id,"viewport selection maps to one entity");
                    bool visible=false;
                    for(std::size_t j=0;j<display.components.size();++j) {
                        if(isolated>=0 && j!=static_cast<std::size_t>(isolated)) continue;
                        for(const auto& hit:display.components[j].entities) if(hit.id==id && hit.kind==kind) visible=true;
                    }
                    require(visible,"pick belongs to visible component and active kind");
                }
            }
            if(expected && !picked) {
                viewport.capture((std::filesystem::temp_directory_path()/"geo-visualiser-pick-failure.png").string());
                std::cerr << "Viewport " << viewport.width() << 'x' << viewport.height() << " dpr=" << viewport.devicePixelRatioF() << " error=" << viewport.error() << '\n';
                for(const auto& c:display.components) for(const auto& e:c.entities) if(e.kind==Kind::Vertex) {
                    const auto p=viewport.project(BRep_Tool::Pnt(TopoDS::Vertex(e.display)));
                    std::cerr << e.id << " projected " << p.x() << ',' << p.y() << '\n';
                }
            }
            require(!expected || picked,std::string("actual hit-test: ")+name(display.representation)+" isolation="+std::to_string(isolated)+" mode="+name(kind));
        }
    }
    // At least one actual AIS-rendered mesh must exist for nonempty groups.
    if(!display.components.empty()) {
        bool mesh=false;
        for(const auto& c:display.components) for(const auto& e:c.entities) if(e.kind==Kind::Face) {
            TopLoc_Location location;
            mesh |= !BRep_Tool::Triangulation(TopoDS::Face(e.display),location).IsNull();
        }
        require(mesh,"AIS rendering produced display triangulation");
    }
    viewport.showDisplay(nullptr);
    require(viewport.selectedId().empty(),"representation change clears selection");
    require(viewport.pick(0,0).empty(),"removed representation cannot intercept pick");
}
void uiChecks(const std::filesystem::path& samples) {
    Window window(samples);window.show();QApplication::processEvents();
    auto* specimensBox=window.findChild<QComboBox*>("specimen");
    auto* reps=window.findChild<QComboBox*>("representation");
    auto* tree=window.findChild<QTreeWidget*>("entities");
    auto* tags=window.findChild<QLineEdit*>("tag");
    auto* save=window.findChild<QPushButton*>("saveTag");
    auto* remove=window.findChild<QPushButton*>("removeTag");
    auto* pickMode=window.findChild<QComboBox*>("pick");
    Viewport* viewport=nullptr;
    for(auto* child:window.findChildren<QWidget*>()) if(auto* candidate=dynamic_cast<Viewport*>(child)) viewport=candidate;
    require(viewport!=nullptr,"UI viewport exists");
    require(specimensBox->count()==2 && reps->currentIndex()==1,"exact specimen menu; initial S");
    window.screen()->grabWindow(window.winId()).save(QString::fromStdString((std::filesystem::temp_directory_path()/"geo-visualiser-desktop.png").string()));
    // Check the actual Window callbacks in both directions for every mode.
    const auto document=loadDocument(0,samples);
    const auto display=prepareDisplay(*document,Representation::Solid);
    for(auto kind:{Kind::Vertex,Kind::Edge,Kind::Face,Kind::Body}) {
        const auto& e=first(display,kind);
        bool selected=false;
        for(QTreeWidgetItemIterator it(tree);*it;++it) if((*it)->data(0,Qt::UserRole).toString().toStdString()==e.id) {
            tree->setCurrentItem(*it);selected=true;break;
        }
        require(selected && viewport->selectedId()==e.id && pickMode->currentIndex()==static_cast<int>(kind),"tree-to-viewport UI synchronization");
        bool picked=false;
        for(const auto& c:display.components) for(const auto& probe:c.entities) if(probe.kind==Kind::Vertex) {
            const auto pixel=viewport->project(BRep_Tool::Pnt(TopoDS::Vertex(probe.display)));
            const auto id=viewport->pick(pixel.x(),pixel.y());
            if(!id.empty()) {
                require(tree->currentItem() && tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==id,"viewport-to-tree UI synchronization");picked=true;
            }
        }
        require(picked,"UI pick detected for each mode");
    }
    const auto selectFace=[&] {
        for(QTreeWidgetItemIterator it(tree);*it;++it) if((*it)->data(0,Qt::UserRole).toString().contains("/Face/")) {tree->setCurrentItem(*it);return;}
        throw std::runtime_error("UI face absent");
    };
    selectFace();tags->setText("source-face-tag");save->click();
    reps->setCurrentIndex(2);selectFace();require(tags->text().isEmpty(),"S tag cannot leak onto coincident B face");
    specimensBox->setCurrentIndex(1);specimensBox->setCurrentIndex(0);reps->setCurrentIndex(1);selectFace();
    require(tags->text()=="source-face-tag","tag survives specimen switches");
    tags->setText("renamed");save->click();require(tree->currentItem()->text(1)=="renamed","tag rename");
    remove->click();require(tags->text().isEmpty() && tree->currentItem()->text(1).isEmpty(),"tag removal");
    tags->setText("expires");save->click();
    Window fresh(samples);fresh.show();QApplication::processEvents();
    auto* freshTree=fresh.findChild<QTreeWidget*>("entities");
    for(QTreeWidgetItemIterator it(freshTree);*it;++it) require((*it)->text(1).isEmpty(),"fresh window has no persisted tags");
    Window missing(samples/"missing-inspector-fixtures");missing.show();QApplication::processEvents();
    require(missing.findChild<QTreeWidget*>("entities")->topLevelItemCount()==0,"failure exposes no results");
    require(missing.findChild<QLabel*>("status")->text().contains("Cannot read original fixture"),"visible missing-fixture diagnostic");
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2 || (argc==3 && std::string(argv[2])=="--render"),"usage: smd-inspector-tests <samples> [--render]");
        const std::filesystem::path samples=argv[1];
        const bool rendering=argc==3;
        std::unique_ptr<QApplication> application;
        std::unique_ptr<Viewport> viewport;
        if(rendering) {application=std::make_unique<QApplication>(argc,argv);viewport=std::make_unique<Viewport>();viewport->resize(900,800);viewport->show();QApplication::processEvents();require(viewport->ready() && viewport->error().empty(),"viewport initialization: "+viewport->error());}
        std::set<std::string> allIds;
        Tags tags;
        for(std::size_t fixture=0;fixture<2;++fixture) {
            const auto d=loadDocument(fixture,samples);
            const auto before=snapshot(*d);
            require(d->converted.faces.size()==(fixture==0?6u:14u),"converted face counts");
            require(d->candidate.supportingFaceOrdinal==(fixture==0?1u:5u),"candidate support reference");
            const auto& a=d->decomposition.extensions;const auto& r=d->decomposition.reductions;
            if(fixture==0) require(a.empty() && r.empty(),"plain empty A/R");
            else {
                require(a.size()==1 && r.size()==1,"combined one A and one R");
                require(std::abs(a[0].volumeMm3-15000000.0)<=1.0,"independent A reference, 1 mm3 tolerance");
                require(std::abs(r[0].volumeMm3-42614000.00009004)<=1.0,"independent R reference, 1 mm3 tolerance");
            }
            std::string sourceTagId;
            for(auto representation:{Representation::Faces,Representation::Solid,Representation::Candidate,Representation::Extensions,Representation::Reductions}) {
                auto display=prepareDisplay(*d,representation);
                int vertices=0,edges=0,faces=0;
                std::map<std::pair<std::size_t,std::size_t>,std::vector<const Entity*>> occurrences;
                for(const auto& c:display.components) {
                    independent(c);
                    for(const auto& e:c.entities) {
                        require(allIds.insert(e.id).second,"globally scoped viewer IDs");
                        vertices+=e.kind==Kind::Vertex;edges+=e.kind==Kind::Edge;faces+=e.kind==Kind::Face;
                        if(representation==Representation::Faces || representation==Representation::Solid) {
                            if(e.kind==Kind::Face) {
                                require(e.sourceFace.has_value(),"known source face identity is present");
                                const auto& originals=representation==Representation::Faces?d->converted.faces:d->source.boundary.faces;
                                require(e.original.IsSame(originals.at(*e.sourceFace)),"source face identity follows original handle");
                            }
                            if(e.kind==Kind::Vertex) {
                                require(e.sourceVertex.has_value(),"known source vertex identity is present");
                                const auto& originals=representation==Representation::Faces?d->converted.vertices:d->source.boundary.vertices;
                                require(e.original.IsSame(originals.at(*e.sourceVertex)),"source vertex identity follows original handle");
                            }
                            if(e.kind==Kind::Edge) require(e.sourceEndpoints.has_value(),"established edge correspondence is present");
                        }
                        if(e.sourceEndpoints) {
                            occurrences[*e.sourceEndpoints].push_back(&e);
                            require(e.edgeUses.size()==(representation==Representation::Faces?1u:2u),"occurrence versus shared-edge correspondence");
                            for(const auto& u:e.edgeUses) {
                                const auto& loop=d->parsed.geometry.brep.faces.at(u.faceOrdinal).indices;
                                const auto x=loop.at(u.loopPosition),y=loop.at((u.loopPosition+1)%loop.size());
                                require(e.sourceEndpoints->first==std::min(x,y) && e.sourceEndpoints->second==std::max(x,y) && u.forward==(x<y),"source edge correspondence preserved");
                            }
                        }
                        if(representation==Representation::Candidate || representation==Representation::Extensions || representation==Representation::Reductions)
                            require(!e.sourceFace && !e.sourceVertex && !e.sourceEndpoints,"derived topology has no invented direct source identity");
                        if(e.kind==Kind::Face && (representation==Representation::Extensions || representation==Representation::Reductions)) {
                            const auto& bodies=representation==Representation::Extensions?a:r;
                            const smd_geometry::BooleanFace* original=nullptr;
                            for(const auto& body:bodies) for(const auto& f:body.faces) if(e.original.IsSame(f.face)) original=&f;
                            require(original && e.ancestors.size()==original->ancestors.size() && !e.ancestors.empty(),"complete recorded ancestry");
                            for(std::size_t i=0;i<e.ancestors.size();++i) {
                                const auto& x=e.ancestors[i];const auto& y=original->ancestors[i];
                                require(x.origin==y.origin && x.change==y.change && x.sourceFaceOrdinal==y.sourceFaceOrdinal,"exact ancestor and change classification");
                            }
                        }
                    }
                }
                if(representation==Representation::Faces) {
                    require(edges==(fixture==0?24:72),"distinct converted edge occurrences");
                    for(const auto& [endpoints,uses]:occurrences) {
                        (void)endpoints;require(uses.size()==2,"two converted occurrences per shared source edge");
                        require(uses[0]->id!=uses[1]->id && !uses[0]->original.IsSame(uses[1]->original) && !uses[0]->display.IsSame(uses[1]->display),"coincident occurrences remain distinct");
                    }
                }
                if(representation==Representation::Solid) {
                    require(vertices==(fixture==0?8:24) && edges==(fixture==0?12:36) && faces==(fixture==0?6:14),"S topology reference");
                    sourceTagId=first(display,Kind::Face).id;tags.set(sourceTagId,"test tag");
                }
                if(representation==Representation::Candidate) require(tags.get(first(display,Kind::Face).id).empty(),"candidate/source tags separate");
                require(snapshot(*d)==before,"evidence preserved through display preparation");
                if(rendering) {render(*viewport,display);require(snapshot(*d)==before,"evidence preserved through AIS rendering and picking");}
            }
            require(tags.get(first(prepareDisplay(*d,Representation::Solid),Kind::Face).id)=="test tag","tag survives display recreation");
            auto bad=d->parsed;bad.geometry.brep.faces[0].indices.clear();
            std::shared_ptr<const Document> partial;
            rejects([&] {partial=buildDocument("bad",bad);},"convertFaces");require(!partial,"failed pipeline cannot publish partial result");
            bad=d->parsed;bad.geometry.brep.faces.pop_back();
            rejects([&] {partial=buildDocument("open",bad);},"assembleShell");require(!partial,"failed shell cannot publish partial result");
            if(fixture==1) {
                auto ledger=*d;
                auto& ancestors=ledger.decomposition.extensions[0].faces[0].ancestors;
                ancestors.push_back({smd_geometry::FaceOrigin::Candidate,{},smd_geometry::FaceChange::Generated});
                ancestors.push_back({smd_geometry::FaceOrigin::Unknown,{},smd_geometry::FaceChange::Unknown});
                const auto display=prepareDisplay(ledger,Representation::Extensions);
                bool retained=false;
                for(const auto& c:display.components) for(const auto& e:c.entities) if(e.original.IsSame(ledger.decomposition.extensions[0].faces[0].face)) retained=e.ancestors.size()==ancestors.size();
                require(retained,"synthetic multi-contributor ledger retained without invented identities");
            }
            std::cout << specimens[fixture].key << ": adapter and preservation passed" << (rendering?" including actual AIS rendering/picking":"") << '\n';
        }
        rejects([&] {loadDocument(0,samples/"missing-inspector-fixtures");},"Cannot read original fixture");
        rejects([&] {loadDocument(2,samples);},"Unknown specimen");
        require(Tags{}.get("plain/S/0/Face/0").empty(),"new session has no tags");
        if(rendering) uiChecks(samples);
        std::cout << "PASS — " << (rendering?"automated desktop test; remote-desktop operator verification remains separate":"adapter test; rendering not exercised") << '\n';
        return 0;
    } catch(const Standard_Failure& e) { std::cerr << "FAIL OCCT: " << e.what() << '\n';return 1; }
    catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n';return 1; }
}
