#pragma once
#include "model.hpp"
#include "view_cube.hpp"
#include "origin_marker.hpp"
#include "scale_bar.hpp"
#include "decorations.hpp"
#include "appearance.hpp"
#include <QWidget>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <V3d_View.hxx>
#include <Bnd_Box.hxx>
#include <functional>

namespace inspector {
enum class DisplayStyle { Shaded, Transparent, Wireframe };
class Viewport : public QWidget {
public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;
    QPaintEngine* paintEngine() const override { return nullptr; }
    void showDisplay(const Display* display, int isolated = -1, bool refit = true);
    void setDisplayStyle(DisplayStyle);
    void setShowContext(bool);
    void setAppearance(const Appearance&);
    Appearance appearance() const { return {palette_,transparency_}; }
    void setPickMode(Kind);
    bool select(const std::string& id);
    void fit();
    void resetCamera();
    void toggleProjection();
    bool perspective() const;
    void orient(V3d_TypeOfOrientation);
    // Read-only snapshot and real cube detection for camera/picking regressions.
    occ::handle<Graphic3d_Camera> camera() const;
    std::optional<V3d_TypeOfOrientation> cubeDirectionAt(int x, int y);
    bool ready() const { return !view_.IsNull(); }
    const std::string& error() const { return error_; }
    std::string selectedId() const;
    // Uses the same hit-test route as a mouse click, in physical viewport pixels.
    std::string pick(int x, int y);
    void redraw();
    bool capture(const std::string& filename);
    QPoint project(const gp_Pnt& point) const;
    std::function<void(const std::string&)> selectionChanged;
    std::function<void(const std::string&)> failed;
    std::function<void()> projectionChanged;
protected:
    void showEvent(QShowEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    friend struct ViewportProbe; // Focused native presentation regressions.
    friend struct NavigationProbe;
    friend struct CaptureProbe;
    struct Object { const Component* component; occ::handle<AIS_Shape> ais; };
    void initialize();
    void populate(bool refit);
    void applyStyle(const occ::handle<AIS_Shape>&);
    void populateContext();
    void updateFaceHighlight();
    void activate();
    void backOffPerspectiveEye();
    Bnd_Box foregroundBounds() const;
    void frameForeground();
    void fitDepth();
    void guard(const std::function<void()>&);
    std::string ownerId(const occ::handle<SelectMgr_EntityOwner>&) const;
    int mode() const;
    bool cubeArea(int x,int y) const;
    QPoint physical(const QPointF&) const;
    const Display* display_ = nullptr;
    int isolated_ = -1;
    Kind kind_ = Kind::Face;
    DisplayStyle style_ = DisplayStyle::Shaded;
    bool showContext_ = true;
    Palette palette_=factoryPalette;
    double transparency_=factoryTransparency;
    occ::handle<AIS_InteractiveContext> context_;
    occ::handle<V3d_View> view_;
    occ::handle<ViewCube> cube_;
    occ::handle<OriginMarker> origin_;
    occ::handle<ScaleBar> scaleBar_;
    occ::handle<FaceHighlight> faceHighlight_;
    Graphic3d_ZLayerId contextLayer_ = Graphic3d_ZLayerId_UNKNOWN;
    Graphic3d_ZLayerId contextOverlayLayer_ = Graphic3d_ZLayerId_UNKNOWN;
    Graphic3d_ZLayerId selectionLayer_ = Graphic3d_ZLayerId_UNKNOWN;
    Graphic3d_ZLayerId originLayer_ = Graphic3d_ZLayerId_UNKNOWN;
    std::vector<occ::handle<ContextOutline>> contextObjects_;
    std::vector<Object> objects_;
    QPoint press_, previous_;
    QSize nativeSize_;
    bool dragged_ = false;
    bool cubePressed_ = false;
    bool cubeLeftGesture_ = false;
    std::string error_;
};
}
