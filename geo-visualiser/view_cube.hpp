#pragma once
#include <AIS_ViewCube.hxx>
#include <array>
#include <vector>

namespace inspector {
// One direction owns all of its outward tiles, including shared edges/corners.
struct CubeTile {
    V3d_TypeOfOrientation orientation;
    gp_Dir normal;
    std::array<gp_Pnt,4> corners;
};
std::vector<CubeTile> cubeTiles(V3d_TypeOfOrientation orientation, double size);

// Native-only 3x3 presentation adaptation. OCCT still owns detection/highlighting.
class ViewCube : public AIS_ViewCube {
public:
    ViewCube();
    void setPixelRatio(double);
    void Compute(const occ::handle<PrsMgr_PresentationManager>&,
                 const occ::handle<Prs3d_Presentation>&, int) override;
    void ComputeSelection(const occ::handle<SelectMgr_Selection>&, int) override;
protected:
    void createBoxPartTriangles(const occ::handle<Graphic3d_ArrayOfTriangles>&,
                               int&, int&, V3d_TypeOfOrientation) const override;
private:
    bool axesOnly_=false;
};
}
