#include "model.hpp"
#include "camera_tests.hpp"
#include "display_tests.hpp"
#include "refine_tests.hpp"
#include <QSettings>
#include <QKeyEvent>
#include <QLineEdit>
#include "viewport.hpp"
#include "window.hpp"
#include "smd/json.hpp"
#include <QApplication>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QDialogButtonBox>
#include <QStatusBar>
#include <QSplitter>
#include <QImage>
#include <QScreen>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QProcess>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <fstream>
#include <array>
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
constexpr const char* fixtures[] = {"plain_column.smd", "SinglePocketingSingleCorbelColumn.smd"};
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
const Entity& first(const inspector::Display& d, Kind kind) {
    for(const auto& c:d.components) for(const auto& e:c.entities) if(e.kind==kind) return e;
    throw std::runtime_error("entity absent");
}
void navigation(Viewport& viewport,const inspector::Display& display) {
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
void render(Viewport& viewport, inspector::Display& display) {
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
// All copies/edits are temporary; original specimens are never changed.
struct Copies {
    QTemporaryDir directory;
    std::filesystem::path first, second, bad;
    explicit Copies(const std::filesystem::path& samples) {
        require(directory.isValid(),"temporary directory");
        const auto root=std::filesystem::path(directory.path().toStdU16String());
        first=root/std::filesystem::path(u"outside space ü 柱")/std::filesystem::path(u"same name ü 柱.smd");
        second=root/"other"/std::filesystem::path(u"same name ü 柱.smd");
        bad=root/"unsupported input.smd";
        std::filesystem::create_directories(first.parent_path());
        std::filesystem::create_directories(second.parent_path());
        std::filesystem::copy_file(samples/fixtures[0],first);
        std::filesystem::copy_file(samples/fixtures[0],second);
        std::ofstream(bad)<<"unsupported input";
    }
    void change() const { std::ofstream(first,std::ios::app)<<"\n \n"; }
};
void documentChecks(const std::filesystem::path& samples) {
    Copies copies(samples);
    Tags tags;
    const auto a=loadDocument(copies.first), again=loadDocument(copies.first), b=loadDocument(copies.second);
    require(a!=again && !a->source.solid.IsSame(again->source.solid),"same-file open builds a new pipeline");
    const auto id=[&](const Document& d) {return first(prepareDisplay(d,Representation::Solid),Kind::Face).id;};
    tags.set(id(*a),"saved");
    require(tags.get(id(*again))=="saved","unchanged canonical path/content retains tags");
    require(tags.get(id(*b)).empty(),"same basename and identical bytes at another path cannot inherit tags");
    copies.change();
    const auto changed=loadDocument(copies.first);
    require(changed->identity!=a->identity && tags.get(id(*changed)).empty(),"changed bytes at same path cannot inherit tags");
    require(Tags{}.get(id(*a)).empty(),"fresh session drops tags");
    rejects([&] {loadDocument(copies.bad);},pathText(copies.bad)+": parseSmd10125:");
    rejects([&] {loadDocument(copies.bad.parent_path()/"missing.smd");},"missing.smd: read:");
    // Synthetic triangular prism: valid S, but no supported rectangular B.
    auto parsed=a->parsed;
    auto& brep=parsed.geometry.brep;
    brep={};
    brep.verticesLocal={{0,0,0},{10,0,0},{0,10,0},{0,0,20},{10,0,20},{0,10,20}};
    for(auto loop:std::vector<std::vector<std::size_t>>{{2,1,0},{3,4,5},{0,1,4,3},{1,2,5,4},{2,0,3,5}}) {
        const auto p=brep.verticesLocal[loop[0]],q=brep.verticesLocal[loop[1]],r=brep.verticesLocal[loop[2]];
        const auto n=gp_Vec(q.x-p.x,q.y-p.y,q.z-p.z).Crossed(gp_Vec(r.x-p.x,r.y-p.y,r.z-p.z)).Normalized();
        brep.faces.push_back({0,loop,smd::Plane{n.X(),n.Y(),n.Z(),-(n.X()*p.x+n.Y()*p.y+n.Z()*p.z)},"synthetic"});
    }
    const auto converted=smd_geometry::convertFaces(brep);
    const auto solid=smd_geometry::buildSolid(smd_geometry::assembleShell(brep,converted));
    require(!solid.solid.IsNull(),"synthetic input passes S validation");
    rejects([&] {buildDocument("unsupported-B",parsed);},"buildPrincipalPrismCandidate:");
}
QString titleFor(const std::filesystem::path& file) {
    return "GEO-Visualiser | "+QString::fromStdU16String(file.filename().u16string());
}
void workspaceChecks(Window& window,Viewport& canvas) {
    auto* central=window.centralWidget();
    auto* representation=central->findChild<QComboBox*>("representation");
    auto* isolate=central->findChild<QComboBox*>("component");
    auto* pick=central->findChild<QComboBox*>("pick");
    auto* style=central->findChild<QComboBox*>("displayStyle");
    auto* fit=central->findChild<QPushButton*>("fit");
    auto* menu=central->findChild<QToolButton*>("mainMenu");
    auto* tree=central->findChild<QTreeWidget*>("entities");
    auto* properties=central->findChild<QPlainTextEdit*>("details");
    auto* splitter=central->findChild<QSplitter*>();
    auto* status=window.statusBar()->findChild<QLabel*>("status");
    auto* projection=window.statusBar()->findChild<QLabel*>("projection");
    require(representation && isolate && pick && style && fit && menu && tree && properties && splitter && status && projection,
            "command, workspace and status controls exist");
    for(auto* control:{representation,style}) {
        require(!control->toolTip().isEmpty() && control->toolTip()==control->accessibleName(),"unlabelled dropdown has a tooltip and accessible name");
        for(auto* label:central->findChildren<QLabel*>()) require(label->text()!=control->accessibleName(),"dropdown label is absent from the command row");
    }
    for(auto* control:{isolate,pick}) {
        bool labelled=false;
        for(auto* label:central->findChildren<QLabel*>()) labelled|=label->buddy()==control && label->isVisible();
        require(labelled,"Isolate and Pick retain visible labels");
    }
    require(splitter->orientation()==Qt::Horizontal && splitter->widget(0)==tree->parentWidget() && splitter->widget(1)==&canvas &&
            !splitter->handle(1)->isHidden(),"canvas and sidebar retain the adjustable split");
    const auto sizes=splitter->sizes();
    splitter->setSizes({sizes[0]-50,sizes[1]+50});
    require(splitter->sizes()[0]<sizes[0] && splitter->sizes()[1]>sizes[1],"resizing the split grows the canvas as the sidebar shrinks");
    splitter->setSizes(sizes);
    const auto rect=[&](QWidget* widget) {return QRect(widget->mapTo(&window,QPoint{}),widget->size());};
    const auto checkRow=[&] {
        QApplication::processEvents();
        const auto rowY=rect(representation).center().y();
        int right=-1;
        for(auto* control:std::array<QWidget*,6>{representation,isolate,pick,style,fit,menu}) {
            const auto bounds=rect(control);
            require(control->isVisible() && bounds.left()>right && std::abs(bounds.center().y()-rowY)<=1,
                    "command controls are ordered horizontally on one row");
            right=bounds.right();
        }
        require(std::abs(rect(menu).right()-rect(splitter).right())<=1 && rect(menu).left()>rect(fit).right(),
                "menu is at the far right of the command row");
        require(rect(tree).bottom()<rect(properties).top() && rect(&canvas).left()>rect(tree).right() &&
                rect(&canvas).top()<=rect(tree).top() && rect(&canvas).bottom()>=rect(properties).bottom(),
                "List of Elements sits above Properties beside a full-height Canvas");
        require(status->isVisible() && projection->isVisible() && rect(status).right()<rect(projection).left() &&
                std::abs(rect(status).center().y()-rect(projection).center().y())<=1 && rect(status).top()>rect(&canvas).bottom() &&
                rect(projection).width()>=projection->sizeHint().width(),"status and full projection share the bottom row");
        require(status->text().startsWith("Loaded | Reductions") && status->toolTip().contains("1 component(s)") &&
                status->toolTip().contains("body 0:") && status->toolTip().contains("mm³"),"status preserves component and volume information");
    };
    const auto normal=window.size();
    checkRow();
    window.resize(std::max(700,window.minimumSizeHint().width()+12),normal.height());
    require(window.width()<normal.width(),"narrow layout uses a smaller window");
    checkRow();
    std::cout<<"PASS — single-row command and status layout at "<<normal.width()<<" and "<<window.width()
             <<" logical pixels, Qt device pixel ratio "<<window.devicePixelRatioF()<<'\n';
    window.resize(normal);
    splitter->setSizes(sizes);
    QApplication::processEvents();
}
void uiChecks(const std::filesystem::path& samples,const QString& preferenceDirectory) {
    Copies copies(samples);
    Window window(samples);window.show();QApplication::processEvents();
    auto* menuButton=window.findChild<QToolButton*>("mainMenu");
    auto* fileMenu=window.findChild<QMenu*>("fileMenu");
    auto* open=window.findChild<QAction*>("openSmd");
    auto* reps=window.findChild<QComboBox*>("representation");
    auto* component=window.findChild<QComboBox*>("component");
    auto* tree=window.findChild<QTreeWidget*>("entities");
    const auto tagText=[&] { return tree->currentItem()?tree->currentItem()->text(1):QString{}; };
    const auto setTag=[&](const QString& value) { editSessionTag(tree,tree->currentItem(),value); };
    auto* details=window.findChild<QPlainTextEdit*>("details");
    auto* status=window.findChild<QLabel*>("status");
    auto* projection=window.findChild<QLabel*>("projection");
    auto* pickMode=window.findChild<QComboBox*>("pick");
    Viewport* viewport=nullptr;
    for(auto* child:window.findChildren<QWidget*>()) if(auto* candidate=dynamic_cast<Viewport*>(child)) viewport=candidate;
    require(viewport && menuButton && fileMenu && open && status && projection,"file action, status and viewport exist");
    require(menuButton->menu()->actions().contains(fileMenu->menuAction()) && fileMenu->actions().contains(open) &&
            open->shortcut()==QKeySequence(Qt::CTRL | Qt::Key_O),"File submenu owns the Ctrl+O open action");
    QElapsedTimer startup;startup.start();
    while(status->text().startsWith("Loading") && startup.elapsed()<10000) {
        QApplication::processEvents(QEventLoop::AllEvents,50);
        QThread::msleep(1);
    }
    require(!window.findChild<QComboBox*>("specimen") && !window.findChild<QLabel*>("filename") &&
            reps->currentIndex()==1 && window.windowTitle()==titleFor(fixtures[0]),
            "startup loads plain S and sets the title: "+window.windowTitle().toStdString()+" | "+status->text().toStdString());
    const auto sameCamera=[](const auto& a,const auto& b) {
        return a->ProjectionType()==b->ProjectionType() && a->Eye().Distance(b->Eye())==0 &&
            a->Center().Distance(b->Center())==0 && a->Direction().IsEqual(b->Direction(),0) &&
            a->Up().IsEqual(b->Up(),0) && a->Scale()==b->Scale();
    };
    const auto key=[&](QWidget* target,int code,bool repeat=false,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        QKeyEvent event(QEvent::KeyPress,code,modifiers,QString(QChar(code).toLower()),repeat);
        QApplication::sendEvent(target,&event);
    };
    const auto keyP=[&](QWidget* target,bool repeat=false) { key(target,Qt::Key_P,repeat); };
    enum class OpenVia { Menu, Shortcut };
    // Drive the menu and shortcut through the real Qt event path, and exercise the same dialog for both.
    const auto openFile=[&](const std::filesystem::path& file, bool cancel=false, OpenVia via=OpenVia::Menu) {
        bool handled=false;
        bool menuActivated=false;
        const auto titleBefore=window.windowTitle();
        std::string error;
        QString diagnostic;
        QTimer driver;
        driver.setInterval(25);
        QObject::connect(&driver,&QTimer::timeout,[&] {
            if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); dialog && !handled) {
                handled=true;
                require(window.windowTitle()==titleBefore,"open dialog preserves the active title until success");
                if(std::filesystem::path(dialog->directory().absolutePath().toStdU16String())!=samples) {
                    error="dialog must start in checkout public/samples";dialog->reject();return;
                }
                const auto before=viewport->camera();
                const int representation=reps->currentIndex();
                auto* filenameEdit=dialog->findChild<QLineEdit*>("fileNameEdit");
                filenameEdit->setFocus();filenameEdit->clear();
                for(int code:{Qt::Key_S,Qt::Key_B,Qt::Key_A,Qt::Key_R,Qt::Key_P}) key(filenameEdit,code);
                require(filenameEdit->text()=="sbarp" && reps->currentIndex()==representation && sameCamera(before,viewport->camera()),"s/b/a/r/p remain normal file dialog text");
                if(cancel) {dialog->reject();return;}
                dialog->setDirectory(QString::fromStdU16String(file.parent_path().u16string()));
                dialog->findChild<QLineEdit*>("fileNameEdit")->setText(QString::fromStdU16String(file.u16string()));
                QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
            } else if(dialog) {
                // Directory changes can reset the filename while its model loads.
                dialog->findChild<QLineEdit*>("fileNameEdit")->setText(QString::fromStdU16String(file.u16string()));
                QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
            } else if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                require(window.windowTitle()==titleBefore,"failed-open warning preserves the active title");
                diagnostic=message->text();message->accept();
            } else if(via==OpenVia::Menu && !menuActivated && fileMenu->isVisible()) {
                menuActivated=true;
                fileMenu->setActiveAction(open);
                // Leave this timer callback before the action enters the file dialog's event loop.
                QTimer::singleShot(0,fileMenu,[&] { key(fileMenu,Qt::Key_Return); });
            } else if(via==OpenVia::Menu && !menuActivated && menuButton->menu()->isVisible()) {
                menuButton->menu()->setActiveAction(fileMenu->menuAction());
                key(menuButton->menu(),Qt::Key_Right);
            }
        });
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline,&QTimer::timeout,[&] {
            error="file dialog timed out: "+pathText(file);
            if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                error+=" directory="+dialog->directory().absolutePath().toStdString()+" selected="+dialog->selectedFiles().join("|").toStdString();
                dialog->grab().save("/tmp/geo-vis-open-dialog-timeout.png");
            }
            if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
            menuButton->menu()->close();
        });
        window.activateWindow();window.raise();viewport->setFocus();QApplication::processEvents();
        deadline.start(10000);
        driver.start();
        if(via==OpenVia::Menu) menuButton->showMenu();
        else key(viewport,Qt::Key_O,false,Qt::ControlModifier);
        driver.stop();QApplication::processEvents();
        require(handled && (via==OpenVia::Shortcut || menuActivated) && error.empty(),
                error.empty()?"file dialog exercised by the requested entry point":error);
        require(window.windowTitle()==(cancel || !diagnostic.isEmpty()?titleBefore:titleFor(file)),"title tracks successful opens only");
        return diagnostic;
    };
    const auto selectFace=[&] {
        for(QTreeWidgetItemIterator it(tree);*it;++it) if((*it)->data(0,Qt::UserRole).toString().contains("/Face/")) {tree->setCurrentItem(*it);return;}
        throw std::runtime_error("UI face absent");
    };
    const auto shortcutParity=[&] {
        const auto state=[&] {
            QString result=QString::number(reps->currentIndex())+"|"+QString::number(component->currentIndex())+"|"+
                QString::number(component->count())+"|"+QString::number(component->isEnabled())+"|"+
                QString::number(pickMode->currentIndex())+"|"+QString::fromStdString(viewport->selectedId())+"|"+
                tagText()+"|"+details->toPlainText()+"|"+status->text();
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                result+="|"+(*it)->text(0)+"|"+(*it)->text(1)+"|"+(*it)->data(0,Qt::UserRole).toString();
            return result;
        };
        for(bool perspective:{false,true}) {
            reps->setCurrentIndex(1);viewport->resetCamera();
            if(perspective) keyP(viewport);
            for(const auto [code,index]:{std::pair{Qt::Key_S,1},{Qt::Key_B,2},{Qt::Key_A,3},{Qt::Key_R,4}}) {
                const int source=index==1?2:1;
                reps->setCurrentIndex(source);selectFace();
                reps->setCurrentIndex(index);
                const auto expected=state();const auto expectedCamera=viewport->camera();
                reps->setCurrentIndex(source);selectFace();viewport->setFocus();QApplication::processEvents();
                key(viewport,code);
                require(state()==expected && sameCamera(expectedCamera,viewport->camera()),"shortcut/dropdown parity: camera, selection, tree, details, isolation and empty state");
                const auto retained=state();const auto before=viewport->camera();
                for(int other:{Qt::Key_S,Qt::Key_B,Qt::Key_A,Qt::Key_R,Qt::Key_P}) {
                    key(viewport,other,true);
                    for(const auto modifier:{Qt::ShiftModifier,Qt::ControlModifier,Qt::AltModifier}) key(viewport,other,false,modifier);
                }
                require(state()==retained && sameCamera(before,viewport->camera()),"navigation auto-repeat and modified keys ignored");
                key(viewport,code);
                require(state()==retained && sameCamera(before,viewport->camera()),"current representation shortcut is same no-op as dropdown");
            }
        }
        reps->setCurrentIndex(1);viewport->resetCamera();
        std::cout<<"PASS — S/B/A/R dropdown parity in O/P, empty groups, repeats and modifiers for "<<window.windowTitle().toStdString()<<'\n';
    };
    const auto camera=[&] {
        std::vector<QPoint> result;
        for(const auto p:{gp_Pnt(-375,-325,-1000),gp_Pnt(375,325,5000),gp_Pnt(0,0,0)}) result.push_back(viewport->project(p));
        return result;
    };
    refineUiChecks(window,*viewport);
    const auto initialCamera=camera();
    shortcutParity();
    const auto reset=[&] {
        require(reps->currentIndex()==1 && component->currentIndex()==0 && pickMode->currentIndex()==2,"successful open resets S/All/Face");
        require(!viewport->perspective() && projection->text()=="O · Orthographic","successful open resets actual Orthographic and indicator");
        require(!tree->currentItem() && tree->selectedItems().empty() && viewport->selectedId().empty(),"successful open clears tree and viewport owner");
        require(tagText().isEmpty() && !details->toPlainText().contains("Viewer ID"),"successful open clears details and active entity");
        require(viewport->error().empty(),viewport->error());
    };
    // Check the actual Window callbacks in both directions for every mode.
    const auto document=loadDocument(samples/fixtures[0]);
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
    selectFace();setTag("restart probe");
    require(openFile(samples/fixtures[1]).isEmpty(),"open combined");reset();
    const auto combinedCamera=viewport->camera();
    reps->setCurrentIndex(4);keyP(viewport);selectFace();
    require(openFile(samples/fixtures[1],false,OpenVia::Shortcut).isEmpty(),"open combined with Ctrl+O");reset();
    require(sameCamera(combinedCamera,viewport->camera()),"menu and Ctrl+O produce the same scene and camera");
    shortcutParity();
    selectFace();setTag("camera tag");
    auto* style=window.findChild<QComboBox*>("displayStyle");
    require(style && !window.findChild<QCheckBox*>("showContext") && style->currentIndex()==0 && viewport->appearance().showContext,
            "Shaded and context are defaults; Show context is in Settings");
    const auto retainedId=viewport->selectedId();
    const auto retainedCamera=viewport->camera();
    const auto retainedRow=tree->currentItem();
    const auto retainedDetails=details->toPlainText();
    for(int i:{1,2,0}) {
        style->setCurrentIndex(i);
        require(viewport->selectedId()==retainedId && tree->currentItem()==retainedRow &&
                tree->currentItem()->text(1)=="camera tag" && tagText()=="camera tag" &&
                details->toPlainText()==retainedDetails && pickMode->currentIndex()==2 &&
                sameCamera(retainedCamera,viewport->camera()),"style control preserves face/tree/tag/details/camera/pick mode");
    }
    auto* settings=window.findChild<QAction*>("settingsAction");
    require(settings,"Settings action available");
    const auto editContext=[&](const auto& edit) {
        bool visited=false;
        QTimer::singleShot(0,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            require(dialog && dialog->objectName()=="appearanceSettings","Settings opens the appearance dialog");
            auto* context=dialog->findChild<QCheckBox*>("showContext");
            require(context && context->isChecked()==viewport->appearance().showContext,"checkbox reflects the active preference");
            visited=true;
            edit(*dialog,*context);
        });
        settings->trigger();
        require(visited,"Settings preview exercised");
    };
    for(int i:{2,3,4}) {
        style->setCurrentIndex(i-2);
        const auto before=viewport->camera();reps->setCurrentIndex(i);
        require(sameCamera(before,viewport->camera()),"B to A to R controls preserve camera/projection");
        selectFace();setTag("context tag");
        const auto id=viewport->selectedId();auto* row=tree->currentItem();
        const auto beforeToggle=viewport->camera();const int roots=tree->topLevelItemCount();
        const auto outlines=visibleContextOutlines(*viewport);
        require(outlines>0,"partial representation starts with visible context outlines");
        const auto checkContext=[&](bool show) {
            require(viewport->appearance().showContext==show && visibleContextOutlines(*viewport)==(show?outlines:0),
                    "Show context changes the displayed outlines immediately");
            const auto currentCamera=viewport->camera();
            require(viewport->selectedId()==id && tree->currentItem()==row && tagText()=="context tag" &&
                    tree->topLevelItemCount()==roots && reps->currentIndex()==i && style->currentIndex()==i-2 &&
                    pickMode->currentIndex()==static_cast<int>(Kind::Face) && sameCamera(beforeToggle,currentCamera) &&
                    beforeToggle->ZNear()==currentCamera->ZNear() && beforeToggle->ZFar()==currentCamera->ZFar() &&
                    beforeToggle->Aspect()==currentCamera->Aspect(),"Show context preserves camera, owner, tree, tag, pick mode and style");
        };
        editContext([&](QDialog& dialog,QCheckBox& context) {
            context.click();checkContext(false);
            context.click();checkContext(true);
            context.click();checkContext(false);
            if(i==2) dialog.findChild<QDialogButtonBox*>("appearanceButtons")->button(QDialogButtonBox::Cancel)->click();
            if(i==3) key(&dialog,Qt::Key_Escape);
            if(i==4) dialog.close();
        });
        checkContext(true);
    }
    editContext([&](QDialog& dialog,QCheckBox& context) {
        context.click();
        dialog.findChild<QDialogButtonBox*>("appearanceButtons")->button(QDialogButtonBox::Ok)->click();
    });
    require(!viewport->appearance().showContext && visibleContextOutlines(*viewport)==0,"OK retains context off");
    editContext([&](QDialog& dialog,QCheckBox& context) {
        dialog.findChild<QPushButton*>("defaultValues")->click();
        require(context.isChecked() && visibleContextOutlines(*viewport)>0,"Default values previews context on");
        dialog.reject();
    });
    require(!viewport->appearance().showContext && visibleContextOutlines(*viewport)==0,"rollback restores the saved off value");
    QProcess restarted;
    restarted.start(QCoreApplication::applicationFilePath(),
        {QString::fromStdU16String(samples.u16string()),"--fresh-window",preferenceDirectory});
    const bool restartedFinished=restarted.waitForFinished(15000);
    require(restartedFinished && restarted.exitStatus()==QProcess::NormalExit && restarted.exitCode()==0,
            "new process restores context preference and starts without saved tags: "+restarted.readAllStandardError().toStdString());
    editContext([&](QDialog& dialog,QCheckBox& context) {
        context.click();
        dialog.findChild<QDialogButtonBox*>("appearanceButtons")->button(QDialogButtonBox::Ok)->click();
    });
    style->setCurrentIndex(0);
    std::cout<<"PASS — Show context previews and rolls back actual outlines, and OK persists across process restart\n";
    reps->setCurrentIndex(1);selectFace();setTag("camera tag");
    const auto cameraSelection=viewport->selectedId();
    auto* cameraRow=tree->currentItem();
    const auto cameraDetails=details->toPlainText();
    // Synthetic key delivery must also establish the real Qt focus chain;
    // an inactive window can otherwise propagate ignored editor keys upward.
    window.activateWindow();
    window.raise();
    viewport->setFocus();QApplication::processEvents();
    const auto orthographic=viewport->camera();
    keyP(viewport);
    require(viewport->perspective() && projection->text()=="P · Perspective","P selects actual Perspective and indicator");
    const auto perspective=viewport->camera();
    keyP(viewport,true);keyP(viewport,true);
    require(sameCamera(perspective,viewport->camera()),"holding P ignores auto-repeat");
    require(orthographic->Direction().IsEqual(perspective->Direction(),1e-12) && orthographic->Up().IsEqual(perspective->Up(),1e-12) &&
            orthographic->Center().Distance(perspective->Center())<1e-8 && std::abs(orthographic->Scale()-perspective->Scale())<1e-8,"P retains direction/up/target/scale");
    keyP(viewport);
    require(!viewport->perspective() && projection->text()=="O · Orthographic","second P returns Orthographic indicator");
    require(viewport->selectedId()==cameraSelection && tree->currentItem()==cameraRow && tree->currentItem()->text(1)=="camera tag" &&
            tagText()=="camera tag" && details->toPlainText()==cameraDetails,"P twice preserves owner/tree/highlight/tag/details");
    editSessionTag(tree,tree->currentItem(),"",true,false,true);
    require(tagText()=="camera tag" && !viewport->perspective() && reps->currentIndex()==1 && viewport->selectedId()==cameraSelection,
            "typing s/b/a/r/p in cancelled tag dialog preserves navigation and tag");
    window.activateWindow();window.raise();QApplication::processEvents();
    details->setFocus();
    require(QApplication::focusWidget()==details,"details typing test has actual Qt editor focus");
    for(int code:{Qt::Key_S,Qt::Key_B,Qt::Key_A,Qt::Key_R,Qt::Key_P}) key(details,code);
    require(details->toPlainText()==cameraDetails && !viewport->perspective() && reps->currentIndex()==1,"read-only text editor suppresses navigation keys");
    viewport->setFocus();viewport->orient(V3d_Zpos);
    require(projection->text()=="O · Orthographic","Orthographic Top remains O");
    keyP(viewport);viewport->orient(V3d_XposYnegZpos);
    require(projection->text()=="P · Perspective","Perspective cube corner remains P");
    reps->setCurrentIndex(4);component->setCurrentIndex(1);selectFace();
    setTag("saved R tag");editSessionTag(tree,tree->currentItem(),"unsaved editor text",true);
    auto reduction=prepareDisplay(*loadDocument(samples/fixtures[1]),Representation::Reductions);
    navigation(*viewport,reduction);
    const auto previousCamera=camera();
    const auto previousCameraState=viewport->camera();
    require(viewport->perspective(),"cancel/failure test starts in Perspective");
    const auto selected=viewport->selectedId();
    const auto previousDetails=details->toPlainText(), previousStatus=status->text();
    const auto retained=[&] {
        require(reps->currentIndex()==4 && component->currentIndex()==1 && pickMode->currentIndex()==2,"failed/cancelled open retains controls");
        require(viewport->selectedId()==selected && tree->currentItem() && tree->currentItem()->text(1)=="saved R tag","failed/cancelled open retains selected owner and saved tag");
        require(tagText()=="saved R tag" && details->toPlainText()==previousDetails && status->text()==previousStatus && camera()==previousCamera,"failed/cancelled open retains tag, details, status and camera");
        require(sameCamera(previousCameraState,viewport->camera()) && projection->text()=="P · Perspective","cancel/failure preserves complete camera and projection indicator");
        require(window.windowTitle()==titleFor(fixtures[1]),"failed/cancelled open retains the active title");
    };
    openFile({},true);retained();
    const auto diagnostic=openFile(copies.bad);retained();
    openFile({},true,OpenVia::Shortcut);retained();
    require(openFile(copies.bad,false,OpenVia::Shortcut)==diagnostic,"menu and Ctrl+O report the same failure");retained();
    std::cerr<<"Failed-open actual dialog: "<<diagnostic.toStdString()<<'\n'
             <<"Failed-open expected full path: "<<QString::fromStdU16String(copies.bad.u16string()).toStdString()<<'\n';
    const auto expectedPath=QDir::fromNativeSeparators(QString::fromStdU16String(copies.bad.u16string()));
    require(QDir::fromNativeSeparators(diagnostic).startsWith(expectedPath+": "),"failed attempt identifies full path");
    require(diagnostic.contains(": parseSmd10125: "),"failed attempt identifies parseSmd10125 stage");
    std::cout<<"PASS — menu and Ctrl+O parity for successful, cancelled and failed opens\n";
    workspaceChecks(window,*viewport);
    require(openFile(samples/fixtures[0],false,OpenVia::Shortcut).isEmpty(),"combined to plain");reset();
    require(camera()==initialCamera,"plain open resets default orientation and fits new S");
    for(int representation:{3,4}) {
        reps->setCurrentIndex(representation);
        require(status->text().contains("0 component(s)") && status->text().contains("empty") && component->count()==1,"plain A/R are empty after combined load");
        require(!viewport->select(selected),"old R pick owner removed");
    }
    require(openFile(copies.first).isEmpty(),"outside copy with spaces/non-ASCII path");reset();
    require(window.windowTitle()==titleFor(copies.first),"active Unicode basename in title");
    selectFace();setTag("source-face-tag");
    reps->setCurrentIndex(2);selectFace();require(tagText().isEmpty(),"S tag cannot leak onto coincident B face");
    navigation(*viewport,display);
    pickMode->setCurrentIndex(1);
    require(openFile(copies.first).isEmpty(),"same-file reopen");reset();
    require(camera()==initialCamera,"same-file reopen resets camera");
    selectFace();require(tagText()=="source-face-tag","same unchanged file retains saved tag");
    setTag("renamed");require(tree->currentItem()->text(1)=="renamed","tag rename");
    editSessionTag(tree,tree->currentItem(),"",false,true);require(tagText().isEmpty() && tree->currentItem()->text(1).isEmpty(),"tag removal");
    setTag("expires");
    require(openFile(copies.second).isEmpty(),"same basename other directory");reset();
    selectFace();require(tagText().isEmpty(),"same basename tags isolated in UI");
    require(openFile(copies.first).isEmpty(),"reopen first copy");selectFace();require(tagText()=="expires","first file tags retained");
    copies.change();
    require(openFile(copies.first).isEmpty(),"changed content same path");reset();
    selectFace();require(tagText().isEmpty(),"changed file tags isolated in UI");
    Window fresh(samples);fresh.show();QApplication::processEvents();
    auto* freshTree=fresh.findChild<QTreeWidget*>("entities");
    for(QTreeWidgetItemIterator it(freshTree);*it;++it) require((*it)->text(1).isEmpty(),"fresh window has no persisted tags");
    Window missing(samples/"missing-inspector-fixtures");missing.show();QApplication::processEvents();
    require(missing.findChild<QTreeWidget*>("entities")->topLevelItemCount()==0,"startup failure exposes no results");
    require(missing.findChild<QLabel*>("status")->text().contains("read:"),"visible missing-fixture stage diagnostic");
    require(missing.windowTitle()=="GEO-Visualiser","failed startup keeps the unloaded title");
    window.screen()->grabWindow(window.winId()).save(QString::fromStdString((std::filesystem::temp_directory_path()/"geo-visualiser-desktop.png").string()));
    std::cout<<"PASS — Qt dialog, document reset, failure/cancel preservation and tag isolation\n";
}
}
int main(int argc,char** argv) {
    try {
        std::cout<<std::unitbuf;
        const bool freshWindow=argc==4 && std::string(argv[2])=="--fresh-window";
        QTemporaryDir preferences;
        require(preferences.isValid(),"isolated preferences directory");
        const auto preferenceDirectory=freshWindow?QString::fromLocal8Bit(argv[3]):preferences.path();
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,preferenceDirectory);
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,preferenceDirectory);
        require(argc==2 || freshWindow || (argc==3 && (std::string(argv[2])=="--render" || std::string(argv[2])=="--open" || std::string(argv[2])=="--camera" || std::string(argv[2])=="--display")),
                "usage: smd-inspector-tests <samples> [--render|--open|--camera|--display|--fresh-window <test preferences>]");
        const std::filesystem::path samples=argv[1];
        if(argc==3 && std::string(argv[2])=="--open") {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
            QApplication application(argc,argv);
            uiChecks(samples,preferenceDirectory);
            return 0;
        }
        if(freshWindow) {
            QApplication application(argc,argv);
            Window fresh(samples);fresh.show();QApplication::processEvents();
            require(fresh.windowTitle()==titleFor(fixtures[0]),"restart loads plain file and sets its title");
            Viewport* viewport=nullptr;
            for(auto* child:fresh.findChildren<QWidget*>()) if(auto* candidate=dynamic_cast<Viewport*>(child)) viewport=candidate;
            require(viewport && !viewport->appearance().showContext,"restart applies the saved context preference");
            auto* tree=fresh.findChild<QTreeWidget*>("entities");
            require(tree->topLevelItemCount()==1,"restart has S");
            for(QTreeWidgetItemIterator it(tree);*it;++it) require((*it)->text(1).isEmpty(),"restart has no saved tags");
            return 0;
        }
        cameraMappingChecks();
        const bool rendering=argc==3;
        std::unique_ptr<QApplication> application;
        std::unique_ptr<Viewport> viewport;
        if(rendering) {QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);application=std::make_unique<QApplication>(argc,argv);viewport=std::make_unique<Viewport>();viewport->resize(900,800);viewport->show();QApplication::processEvents();require(viewport->ready() && viewport->error().empty(),"viewport initialization: "+viewport->error());}
        if(argc==3 && std::string(argv[2])=="--camera") {
            const auto document=loadDocument(samples/fixtures[1]);
            const auto display=prepareDisplay(*document,Representation::Solid);
            cameraRenderChecks(*viewport,display);
            cameraSnapClearanceChecks(*viewport,prepareDisplay(*document,Representation::Candidate));
            uiChecks(samples,preferenceDirectory);
            return 0;
        }
        if(argc==3 && std::string(argv[2])=="--display") {
            for(const auto fixture:fixtures) {
                const auto document=loadDocument(samples/fixture);
                const auto before=snapshot(*document);
                displayMappingChecks(*document);
                displayRenderChecks(*viewport,*document);
                require(snapshot(*document)==before,"evidence preserved through display regression suite");
            }
            uiChecks(samples,preferenceDirectory);
            return 0;
        }
        std::set<std::string> allIds;
        Tags tags;
        for(std::size_t fixture=0;fixture<2;++fixture) {
            const auto d=loadDocument(samples/fixtures[fixture]);
            const auto before=snapshot(*d);
            displayMappingChecks(*d);
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
                if(rendering) {
                    if(fixture==1 && representation==Representation::Solid) cameraRenderChecks(*viewport,display);
                    if(fixture==1 && representation==Representation::Candidate) cameraSnapClearanceChecks(*viewport,display);
                    render(*viewport,display);require(snapshot(*d)==before,"evidence preserved through AIS rendering and picking");
                }
            }
            require(tags.get(first(prepareDisplay(*d,Representation::Solid),Kind::Face).id)=="test tag","tag survives display recreation");
            if(rendering) {
                displayRenderChecks(*viewport,*d);
                require(snapshot(*d)==before,"evidence preserved through styles, context and origin render checks");
            }
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
            std::cout << fixtures[fixture] << ": adapter and preservation passed" << (rendering?" including actual AIS rendering/picking":"") << '\n';
        }
        rejects([&] {loadDocument(samples/"missing-inspector-fixtures");},"read:");
        documentChecks(samples);
        require(Tags{}.get("plain/S/0/Face/0").empty(),"new session has no tags");
        if(rendering) uiChecks(samples,preferenceDirectory);
        std::cout << "PASS — " << (rendering?"automated desktop test; remote-desktop operator verification remains separate":"adapter test; rendering not exercised") << '\n';
        return 0;
    } catch(const Standard_Failure& e) { std::cerr << "FAIL OCCT: " << e.what() << '\n';return 1; }
    catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n';return 1; }
}
