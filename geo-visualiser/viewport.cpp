#include "viewport.hpp"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QImage>
#include <Image_PixMap.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <Select3D_SensitiveEntity.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <Standard_Failure.hxx>
#include <V3d_Viewer.hxx>
#ifdef _WIN32
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif
#include <cmath>
#include <stdexcept>

namespace inspector {
Viewport::Viewport(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setMinimumSize(400,400);
    setFocusPolicy(Qt::StrongFocus);
}
Viewport::~Viewport() {
    if(!context_.IsNull()) context_->RemoveAll(false);
    if(!view_.IsNull()) view_->Remove();
    view_.Nullify();
    context_.Nullify();
}
void Viewport::guard(const std::function<void()>& action) {
    if(!error_.empty()) return;
    try { action(); }
    catch(const Standard_Failure& e) { error_ = std::string("OCCT viewport: ") + e.what(); }
    catch(const std::exception& e) { error_ = e.what(); }
    if(!error_.empty() && failed) failed(error_);
}
void Viewport::initialize() {
#ifndef _WIN32
    if(QGuiApplication::platformName() != "xcb") throw std::runtime_error("Use QT_QPA_PLATFORM=xcb under X11/XWayland; native Wayland is unsupported.");
#endif
    occ::handle<Aspect_DisplayConnection> connection = new Aspect_DisplayConnection();
    occ::handle<OpenGl_GraphicDriver> driver = new OpenGl_GraphicDriver(connection);
    occ::handle<V3d_Viewer> viewer = new V3d_Viewer(driver);
    viewer->SetDefaultLights();
    viewer->SetLightOn();
    context_ = new AIS_InteractiveContext(viewer);
    context_->SetAutoActivateSelection(false);
    context_->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)->SetColor(Quantity_NOC_YELLOW);
    context_->HighlightStyle(Prs3d_TypeOfHighlight_Selected)->SetColor(Quantity_NOC_YELLOW);
    view_ = viewer->CreateView();
#ifdef _WIN32
    occ::handle<WNT_Window> window = new WNT_Window(reinterpret_cast<Aspect_Handle>(winId()));
#else
    occ::handle<Xw_Window> window = new Xw_Window(connection, static_cast<Aspect_Drawable>(winId()));
#endif
    view_->SetWindow(window);
    view_->SetBackgroundColor(Quantity_Color(0.10,0.12,0.16,Quantity_TOC_RGB));
    view_->SetProj(V3d_XposYnegZpos);
    view_->MustBeResized();
    populate();
}
int Viewport::mode() const {
    return AIS_Shape::SelectionMode(kind_ == Kind::Vertex ? TopAbs_VERTEX : kind_ == Kind::Edge ? TopAbs_EDGE :
                                   kind_ == Kind::Face ? TopAbs_FACE : TopAbs_SOLID);
}
void Viewport::populate() {
    if(context_.IsNull()) return;
    context_->RemoveAll(false); // Removal also removes hidden selection owners.
    objects_.clear();
    if(display_) for(std::size_t i=0; i<display_->components.size(); ++i) {
        if(isolated_ >= 0 && i != static_cast<std::size_t>(isolated_)) continue;
        const auto& c = display_->components[i];
        occ::handle<AIS_Shape> ais = new AIS_Shape(c.display);
        ais->SetColor(display_->representation == Representation::Reductions ? Quantity_Color(0.90,0.40,0.30,Quantity_TOC_RGB) :
                      display_->representation == Representation::Extensions ? Quantity_Color(0.35,0.75,0.45,Quantity_TOC_RGB) :
                      Quantity_Color(0.60,0.72,0.85,Quantity_TOC_RGB));
        ais->Attributes()->SetFaceBoundaryDraw(true);
        ais->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(Quantity_NOC_BLACK, Aspect_TOL_SOLID, 1.0));
        context_->Display(ais, AIS_Shaded, -1, false);
        // Display(...,-1) deliberately skips registration with the selector.
        // Load is required before activating topological selection modes.
        context_->Load(ais,-1);
        objects_.push_back({&c,ais});
    }
    activate();
    fit();
    view_->Redraw();
}
void Viewport::showDisplay(const Display* display, int isolated) {
    display_ = display;
    isolated_ = isolated;
    guard([&] { populate(); });
}
void Viewport::activate() {
    if(context_.IsNull()) return;
    context_->ClearSelected(false);
    context_->ClearDetected(false);
    for(const auto& o : objects_) {
        context_->Deactivate(o.ais);
        if(kind_ != Kind::Body || o.component->display.ShapeType() == TopAbs_SOLID)
            context_->Activate(o.ais,mode());
    }
}
void Viewport::setPickMode(Kind k) {
    kind_ = k;
    guard([&] { activate(); redraw(); });
}
std::string Viewport::ownerId(const occ::handle<SelectMgr_EntityOwner>& owner) const {
    auto brep = occ::down_cast<StdSelect_BRepOwner>(owner);
    if(brep.IsNull() || !brep->HasShape()) return {};
    for(const auto& o : objects_) if(owner->Selectable() == o.ais)
        for(const auto& e : o.component->entities)
            if(e.kind == kind_ && e.display.IsSame(brep->Shape())) return e.id;
    return {};
}
std::string Viewport::selectedId() const {
    if(context_.IsNull()) return {};
    context_->InitSelected();
    return context_->MoreSelected() ? ownerId(context_->SelectedOwner()) : std::string{};
}
bool Viewport::select(const std::string& id) {
    bool found = false;
    guard([&] {
        if(context_.IsNull()) return;
        context_->ClearSelected(false);
        for(const auto& o : objects_) {
            if(!o.ais->HasSelection(mode())) continue;
            for(const auto& sensitive : o.ais->Selection(mode())->Entities()) {
                const auto& owner = sensitive->BaseSensitive()->OwnerId();
                if(!id.empty() && ownerId(owner) == id) {
                    context_->SetSelected(owner, false);
                    found = true;
                    break;
                }
            }
            if(found) break;
        }
        redraw();
    });
    return found;
}
std::string Viewport::pick(int x, int y) {
    std::string id;
    guard([&] {
        if(context_.IsNull()) return;
        context_->MoveTo(x,y,view_,false);
        if(context_->HasDetected()) id = ownerId(context_->DetectedOwner());
        if(id.empty()) context_->ClearSelected(false);
        else context_->SelectDetected(AIS_SelectionScheme_Replace);
        redraw();
    });
    if(selectionChanged) selectionChanged(id);
    return id;
}
void Viewport::fit() {
    guard([&] { if(!view_.IsNull() && !objects_.empty()) { view_->FitAll(0.06,false); view_->ZFitAll(); view_->Redraw(); } });
}
void Viewport::redraw() {
    if(view_.IsNull()) return;
    int widthPx=0,heightPx=0;
    view_->Window()->Size(widthPx,heightPx);
    // Qt can deliver resizeEvent before the native child has its final size.
    // Synchronize against the actual drawable immediately before painting.
    const QSize size(widthPx,heightPx);
    if(size!=nativeSize_) {
        nativeSize_=size;
        view_->MustBeResized();
        if(!objects_.empty()) {view_->FitAll(0.06,false);view_->ZFitAll();}
    }
    view_->Redraw();
}
bool Viewport::capture(const std::string& filename) {
    bool success = false;
    guard([&] {
        if(view_.IsNull()) return;
        Image_PixMap pixels;
        const int widthPx=qRound(width()*devicePixelRatioF()),heightPx=qRound(height()*devicePixelRatioF());
        if(!view_->ToPixMap(pixels,widthPx,heightPx)) return;
        QImage image(widthPx,heightPx,QImage::Format_RGB32);
        for(int y=0;y<heightPx;++y) for(int x=0;x<widthPx;++x) {
            const auto color=pixels.PixelColor(x,y);
            image.setPixelColor(x,y,QColor::fromRgbF(color.GetRGB().Red(),color.GetRGB().Green(),color.GetRGB().Blue()));
        }
        success=image.save(QString::fromStdString(filename));
    });
    return success;
}
QPoint Viewport::project(const gp_Pnt& point) const {
    int x=0,y=0;
    if(!view_.IsNull()) view_->Convert(point.X(),point.Y(),point.Z(),x,y);
    return {x,y};
}
QPoint Viewport::physical(const QPointF& p) const { return (p * devicePixelRatioF()).toPoint(); }
void Viewport::showEvent(QShowEvent*) { guard([&] { if(view_.IsNull()) initialize(); }); }
void Viewport::paintEvent(QPaintEvent*) { guard([&] { redraw(); }); }
void Viewport::resizeEvent(QResizeEvent*) { nativeSize_={};update(); }
void Viewport::mousePressEvent(QMouseEvent* e) {
    press_ = previous_ = physical(e->position()); dragged_ = false;
    guard([&] { if(!view_.IsNull() && e->button() == Qt::LeftButton) view_->StartRotation(press_.x(),press_.y()); });
}
void Viewport::mouseMoveEvent(QMouseEvent* e) {
    const auto p = physical(e->position());
    if((p-press_).manhattanLength() > 4*devicePixelRatioF()) dragged_ = true;
    guard([&] {
        if(view_.IsNull()) return;
        if(e->buttons() & Qt::LeftButton) { if(dragged_) view_->Rotation(p.x(),p.y()); }
        else if(e->buttons() & (Qt::MiddleButton | Qt::RightButton)) view_->Pan(p.x()-previous_.x(),previous_.y()-p.y());
        else context_->MoveTo(p.x(),p.y(),view_,true);
    });
    previous_ = p;
}
void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    const auto p = physical(e->position());
    if(e->button() == Qt::LeftButton && !dragged_) pick(p.x(),p.y());
}
void Viewport::wheelEvent(QWheelEvent* e) {
    guard([&] { if(!view_.IsNull()) { view_->SetZoom(std::pow(1.15,e->angleDelta().y()/120.0)); redraw(); } });
}
}
