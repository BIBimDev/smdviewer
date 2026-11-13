#pragma once
#include "model.hpp"
#include <QWidget>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <V3d_View.hxx>
#include <functional>

namespace inspector {
class Viewport : public QWidget {
public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;
    QPaintEngine* paintEngine() const override { return nullptr; }
    void showDisplay(const Display* display, int isolated = -1);
    void setPickMode(Kind);
    bool select(const std::string& id);
    void fit();
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
protected:
    void showEvent(QShowEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    struct Object { const Component* component; occ::handle<AIS_Shape> ais; };
    void initialize();
    void populate();
    void activate();
    void guard(const std::function<void()>&);
    std::string ownerId(const occ::handle<SelectMgr_EntityOwner>&) const;
    int mode() const;
    QPoint physical(const QPointF&) const;
    const Display* display_ = nullptr;
    int isolated_ = -1;
    Kind kind_ = Kind::Face;
    occ::handle<AIS_InteractiveContext> context_;
    occ::handle<V3d_View> view_;
    std::vector<Object> objects_;
    QPoint press_, previous_;
    QSize nativeSize_;
    bool dragged_ = false;
    std::string error_;
};
}
