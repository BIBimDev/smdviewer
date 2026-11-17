#include "viewport.hpp"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QImage>
#include <V3d.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Image_PixMap.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_IsoAspect.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <Select3D_SensitiveEntity.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <Standard_Failure.hxx>
#include <V3d_Viewer.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Precision.hxx>
#ifdef _WIN32
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif
#include <cmath>
#include <algorithm>
#include <limits>
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
    Graphic3d_ZLayerSettings contextSettings;
    contextSettings.SetName("Partial-body context outlines");
    contextSettings.SetClearDepth(false);
    contextSettings.SetEnableDepthWrite(false);
    contextSettings.SetRenderInDepthPrepass(false);
    if(!viewer->InsertLayerBefore(contextLayer_,contextSettings,Graphic3d_ZLayerId_Default))
        throw std::runtime_error("Cannot create contextual outline layer");
    contextSettings.SetName("Partial-body overlay outlines");
    contextSettings.SetEnableDepthTest(false);
    if(!viewer->InsertLayerAfter(contextOverlayLayer_,contextSettings,Graphic3d_ZLayerId_Default))
        throw std::runtime_error("Cannot create contextual overlay layer");
    contextSettings.SetName("Selected bounded face");
    auto selectionSettings=contextSettings;
    selectionSettings.SetEnableDepthTest(style_==DisplayStyle::Shaded);
    // Test against opaque foreground depth, but keep a visible face's fill
    // just ahead of its coincident surface without moving any geometry.
    selectionSettings.SetDepthOffsetNegative();
    if(!viewer->InsertLayerAfter(selectionLayer_,selectionSettings,contextOverlayLayer_))
        throw std::runtime_error("Cannot create selected-face layer");
    contextSettings.SetName("Source-local origin overlay");
    // OCCT batches transparent groups until the end of the normal pass. An
    // immediate layer keeps the arrows above that fill without clearing depth.
    contextSettings.SetImmediate(true);
    if(!viewer->InsertLayerAfter(originLayer_,contextSettings,selectionLayer_))
        throw std::runtime_error("Cannot create origin overlay layer");
    context_ = new AIS_InteractiveContext(viewer);
    context_->SetAutoActivateSelection(false);
    context_->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)->SetColor(Quantity_NOC_YELLOW);
    context_->HighlightStyle(Prs3d_TypeOfHighlight_Selected)->SetColor(Quantity_NOC_YELLOW);
    view_ = viewer->CreateView();
    // OCCT's automatic ZFit includes infinite decorations in its graphical box.
    // Model depth uses foreground geometry; retain the existing cube separately.
    view_->SetAutoZFitMode(false);
#ifdef _WIN32
    occ::handle<WNT_Window> window = new WNT_Window(reinterpret_cast<Aspect_Handle>(winId()));
#else
    occ::handle<Xw_Window> window = new Xw_Window(connection, static_cast<Aspect_Drawable>(winId()));
#endif
    view_->SetWindow(window);
    view_->SetBackgroundColor(Quantity_Color(0.10,0.12,0.16,Quantity_TOC_RGB));
    view_->SetProj(V3d_XposYnegZpos);
    view_->MustBeResized();
    cube_ = new ViewCube();
    cube_->setPixelRatio(devicePixelRatioF());
    origin_ = new OriginMarker();
    origin_->SetZLayer(originLayer_);
    origin_->setPixelRatio(devicePixelRatioF());
    populate(true);
}
int Viewport::mode() const {
    return AIS_Shape::SelectionMode(kind_ == Kind::Vertex ? TopAbs_VERTEX : kind_ == Kind::Edge ? TopAbs_EDGE :
                                   kind_ == Kind::Face ? TopAbs_FACE : TopAbs_SOLID);
}
void Viewport::applyStyle(const occ::handle<AIS_Shape>& ais) {
    context_->SetDisplayMode(ais,style_==DisplayStyle::Wireframe?AIS_WireFrame:AIS_Shaded,false);
    context_->SetTransparency(ais,style_==DisplayStyle::Transparent?factoryTransparency:0.0,false);
}
void Viewport::setDisplayStyle(DisplayStyle style) {
    style_=style;
    guard([&] {
        if(!context_.IsNull()) {
            const auto viewer=context_->CurrentViewer();
            auto settings=viewer->ZLayerSettings(selectionLayer_);
            settings.SetEnableDepthTest(style_==DisplayStyle::Shaded);
            viewer->SetZLayerSettings(selectionLayer_,settings);
        }
        for(const auto& object:objects_) applyStyle(object.ais);
        redraw();
    });
}
void Viewport::populateContext() {
    for(const auto& ais:contextObjects_) context_->Remove(ais,false);
    contextObjects_.clear();
    if(!showContext_ || !display_) return;
    for(const auto& outline:display_->context) {
        occ::handle<ContextOutline> ais=new ContextOutline(outline.shape,outline.representation==Representation::Reductions);
        // B/A need outlines through their opaque foreground to reveal contained
        // reductions. In R, keep context behind the pocket's filled foreground.
        ais->SetZLayer(display_->representation==Representation::Reductions?contextLayer_:contextOverlayLayer_);
        // Never Load or Activate: context has no selectable owner or UI mapping.
        context_->Display(ais,AIS_WireFrame,-1,false);
        contextObjects_.push_back(ais);
    }
}
void Viewport::setShowContext(bool show) {
    showContext_=show;
    guard([&] { if(!context_.IsNull()) { populateContext(); redraw(); } });
}
void Viewport::populate(bool refit) {
    if(context_.IsNull()) return;
    context_->RemoveAll(false); // Removal also removes hidden selection owners.
    faceHighlight_.Nullify();
    objects_.clear();
    contextObjects_.clear();
    if(display_) for(std::size_t i=0; i<display_->components.size(); ++i) {
        if(isolated_ >= 0 && i != static_cast<std::size_t>(isolated_)) continue;
        const auto& c = display_->components[i];
        occ::handle<AIS_Shape> ais = new AIS_Shape(c.display);
        ais->SetColor(palette_.surface(display_->representation));
        ais->Attributes()->SetFaceBoundaryDraw(true);
        ais->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(Quantity_NOC_BLACK, Aspect_TOL_SOLID, 1.0));
        ais->Attributes()->SetIsoOnPlane(false);
        ais->Attributes()->UIsoAspect()->SetNumber(0);
        ais->Attributes()->VIsoAspect()->SetNumber(0);
        const auto wire=palette_.wire(display_->representation);
        ais->Attributes()->SetWireAspect(new Prs3d_LineAspect(wire,Aspect_TOL_SOLID,1));
        ais->Attributes()->SetUnFreeBoundaryAspect(new Prs3d_LineAspect(wire,Aspect_TOL_SOLID,1));
        ais->Attributes()->SetFreeBoundaryAspect(new Prs3d_LineAspect(wire,Aspect_TOL_SOLID,1));
        context_->Display(ais, AIS_Shaded, -1, false);
        applyStyle(ais);
        // Display(...,-1) deliberately skips registration with the selector.
        // Load is required before activating topological selection modes.
        context_->Load(ais,-1);
        objects_.push_back({&c,ais});
    }
    populateContext();
    context_->Display(origin_,0,-1,false);
    context_->Display(cube_,0,0,false);
    context_->Activate(cube_,0);
    activate();
    if(refit) fit();
    else {
        if(perspective()) backOffPerspectiveEye();
        fitDepth();
    }
    view_->Redraw();
}
void Viewport::showDisplay(const Display* display, int isolated, bool refit) {
    display_ = display;
    isolated_ = isolated;
    guard([&] { populate(refit); });
}
void Viewport::activate() {
    if(context_.IsNull()) return;
    context_->ClearSelected(false);
    updateFaceHighlight();
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
void Viewport::updateFaceHighlight() {
    if(!faceHighlight_.IsNull()) context_->Remove(faceHighlight_,false);
    faceHighlight_.Nullify();
    context_->InitSelected();
    if(kind_!=Kind::Face || !context_->MoreSelected()) return;
    const auto owner=occ::down_cast<StdSelect_BRepOwner>(context_->SelectedOwner());
    if(owner.IsNull() || !owner->HasShape() || ownerId(owner).empty()) return;
    faceHighlight_=new FaceHighlight(owner->Shape());
    faceHighlight_->SetZLayer(selectionLayer_);
    context_->Display(faceHighlight_,0,-1,false);
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
        updateFaceHighlight();
        redraw();
    });
    return found;
}
std::string Viewport::pick(int x, int y) {
    std::string id;
    bool cubeClick=false;
    guard([&] {
        if(context_.IsNull()) return;
        if(const auto direction=cubeDirectionAt(x,y)) {
            cubeClick=true;
            orient(*direction);
            context_->MoveTo(x,y,view_,false);
            redraw();
            id=selectedId();
            return;
        }
        if(cubeArea(x,y)) { cubeClick=true;id=selectedId();return; }
        if(context_->HasDetected()) id = ownerId(context_->DetectedOwner());
        if(id.empty()) context_->ClearSelected(false);
        else context_->SelectDetected(AIS_SelectionScheme_Replace);
        updateFaceHighlight();
        redraw();
    });
    // A navigation click never synchronizes/clears the inspector selection.
    if(!cubeClick && selectionChanged) selectionChanged(id);
    return id;
}
void Viewport::resetCamera() {
    guard([&] {
        if(view_.IsNull()) return;
        view_->Reset(false);
        view_->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
        orient(V3d_XposYnegZpos);
        fit();
        if(projectionChanged) projectionChanged();
    });
}
bool Viewport::perspective() const {
    return !view_.IsNull() && view_->Camera()->ProjectionType()==Graphic3d_Camera::Projection_Perspective;
}
occ::handle<Graphic3d_Camera> Viewport::camera() const {
    return view_.IsNull()?occ::handle<Graphic3d_Camera>():new Graphic3d_Camera(view_->Camera());
}
void Viewport::toggleProjection() {
    guard([&] {
        if(view_.IsNull()) return;
        const auto& camera=view_->Camera();
        const double scale=camera->Scale();
        camera->SetProjectionType(perspective()?Graphic3d_Camera::Projection_Orthographic:
                                              Graphic3d_Camera::Projection_Perspective);
        camera->SetScale(scale); // OCCT adjusts perspective eye distance about the same target.
        if(perspective()) backOffPerspectiveEye();
        fitDepth();
        redraw();
        if(projectionChanged) projectionChanged();
    });
}
void Viewport::backOffPerspectiveEye() {
    // Only displayed model copies participate: no cube, hidden component or
    // selected-only bounds. Geometry bounds do not use/alter triangulation.
    const auto bounds=foregroundBounds();
    if(bounds.IsVoid()) return;
    const auto min=bounds.CornerMin(),max=bounds.CornerMax();
    const auto& camera=view_->Camera();
    const gp_Vec forward(camera->Direction()),up(camera->OrthogonalizedUp());
    const gp_Vec right=forward.Crossed(up);
    const auto unitFrustum=camera->ViewDimensions(1.0);
    double nearest=std::numeric_limits<double>::lowest(),framedDistance=0;
    for(double x:{min.X(),max.X()}) for(double y:{min.Y(),max.Y()}) for(double z:{min.Z(),max.Z()}) {
        const gp_Vec offset(camera->Center(),gp_Pnt(x,y,z));
        const double towardEye=-offset.Dot(forward);
        nearest=std::max(nearest,towardEye);
        const double lateral=std::max(2*std::abs(offset.Dot(right))/unitFrustum.X(),
                                      2*std::abs(offset.Dot(up))/unitFrustum.Y());
        framedDistance=std::max(framedDistance,towardEye+1.06*lateral);
    }
    // Camera-only numerical clearance in source-local mm, independent of kernel
    // modelling tolerances. Ordinary safe views retain the exact requested scale.
    const double clearance=std::max(0.001,min.Distance(max)*1e-6);
    if(camera->Distance()<=nearest+clearance)
        camera->SetDistance(std::max(nearest+clearance,framedDistance));
}
void Viewport::orient(V3d_TypeOfOrientation direction) {
    guard([&] {
        if(view_.IsNull()) return;
        const auto& camera=view_->Camera();
        // Keep target/distance/scale; SetProj(enum) also pans about the world origin.
        camera->SetDirection(-V3d::GetProjAxis(direction));
        camera->SetUp(direction==V3d_Zpos?gp::DY():direction==V3d_Zneg?-gp::DY():gp::DZ());
        camera->OrthogonalizeUp();
        fitDepth();
        redraw();
    });
}
std::optional<V3d_TypeOfOrientation> Viewport::cubeDirectionAt(int x,int y) {
    if(context_.IsNull()) return {};
    context_->MoveTo(x,y,view_,false);
    if(context_->HasDetected()) {
        const auto owner=occ::down_cast<AIS_ViewCubeOwner>(context_->DetectedOwner());
        if(!owner.IsNull() && owner->Selectable()==cube_) return owner->MainOrientation();
    }
    return {};
}
void Viewport::fit() {
    guard([&] { if(!view_.IsNull() && !objects_.empty()) { frameForeground(); fitDepth(); view_->Redraw(); } });
}
void Viewport::frameForeground() {
    // V3d_View::FitAll also expands for zoom-persistent presentations even when
    // given explicit bounds. Fit the camera directly to exclude the marker.
    const auto& camera=view_->Camera();
    if(camera->FitMinMax(foregroundBounds(),10*Precision::Confusion(),true))
        camera->SetScale(camera->Scale()*1.06); // Existing native Fit margin.
}
Bnd_Box Viewport::foregroundBounds() const {
    Bnd_Box bounds;
    for(const auto& object:objects_) BRepBndLib::AddOptimal(object.component->display,bounds,false,false);
    return bounds;
}
void Viewport::fitDepth() {
    if(view_.IsNull()) return;
    const auto bounds=foregroundBounds();
    Bnd_Box graphical=bounds;
    // Retain the existing cube's graphical depth range, without letting it enter
    // framing or eye-clearance bounds. New context/origin never enter either box.
    if(!cube_.IsNull()) {
        Bnd_Box cubeBounds;
        cube_->BoundingBox(cubeBounds);
        int width=0,height=0;view_->Window()->Size(width,height);
        const auto& camera=view_->Camera();
        cube_->TransformPersistence()->Apply(camera,camera->ProjectionMatrix(),camera->OrientationMatrix(),width,height,cubeBounds);
        graphical.Add(cubeBounds);
    }
    if(!graphical.IsVoid()) view_->Camera()->ZFitAll(1.0,bounds,graphical);
}
void Viewport::redraw() {
    if(view_.IsNull()) return;
    int widthPx=0,heightPx=0;
    view_->Window()->Size(widthPx,heightPx);
    // Qt can deliver resizeEvent before the native child has its final size.
    // Synchronize against the actual drawable immediately before painting.
    const QSize size(widthPx,heightPx);
    if(size!=nativeSize_) {
        if(!cube_.IsNull() && std::abs(cube_->Size()-84*devicePixelRatioF())>0.01) {
            cube_->setPixelRatio(devicePixelRatioF());
            context_->Redisplay(cube_,false);
            origin_->setPixelRatio(devicePixelRatioF());
            context_->Redisplay(origin_,false);
        }
        nativeSize_=size;
        view_->MustBeResized();
        if(!objects_.empty()) frameForeground();
    }
    fitDepth();
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
bool Viewport::cubeArea(int x,int y) const {
    // Reserve the small control footprint, including tile gaps/axis arrows.
    return x>=0 && x<qRound(200*devicePixelRatioF()) && y>=qRound((height()-200)*devicePixelRatioF()) && y<qRound(height()*devicePixelRatioF());
}
QPoint Viewport::physical(const QPointF& p) const { return (p * devicePixelRatioF()).toPoint(); }
void Viewport::showEvent(QShowEvent*) { guard([&] { if(view_.IsNull()) initialize(); }); }
void Viewport::paintEvent(QPaintEvent*) { guard([&] { redraw(); }); }
void Viewport::resizeEvent(QResizeEvent*) { nativeSize_={};update(); }
void Viewport::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    press_ = previous_ = physical(e->position()); dragged_ = false;
    cubePressed_=cubeArea(press_.x(),press_.y());
    guard([&] { if(!view_.IsNull() && !cubePressed_ && e->button() == Qt::LeftButton) view_->StartRotation(press_.x(),press_.y()); });
}
void Viewport::mouseMoveEvent(QMouseEvent* e) {
    const auto p = physical(e->position());
    if((p-press_).manhattanLength() > 4*devicePixelRatioF()) dragged_ = true;
    guard([&] {
        if(view_.IsNull() || cubePressed_) return;
        if(e->buttons() & Qt::LeftButton) { if(dragged_) { view_->Rotation(p.x(),p.y()); redraw(); } }
        else if(e->buttons() & (Qt::MiddleButton | Qt::RightButton)) { view_->Pan(p.x()-previous_.x(),previous_.y()-p.y()); redraw(); }
        else {
            const auto cubeDirection=cubeDirectionAt(p.x(),p.y());
            if(cubeArea(p.x(),p.y()) && !cubeDirection) context_->ClearDetected(false);
            redraw();
        }
    });
    previous_ = p;
}
void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    const auto p = physical(e->position());
    guard([&] {
        if(e->button() == Qt::LeftButton && !dragged_) {
            if(!cubePressed_ || cubeDirectionAt(p.x(),p.y())) pick(p.x(),p.y());
        }
    });
    cubePressed_=false;
}
void Viewport::leaveEvent(QEvent*) {
    guard([&] { if(!context_.IsNull()) { context_->ClearDetected(false); redraw(); } });
}
void Viewport::wheelEvent(QWheelEvent* e) {
    guard([&] { if(!view_.IsNull()) { view_->SetZoom(std::pow(1.15,e->angleDelta().y()/120.0)); redraw(); } });
}
}
