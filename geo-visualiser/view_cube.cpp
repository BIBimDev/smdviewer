#include "view_cube.hpp"
#include <V3d.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <cmath>
#include <SelectMgr_Selection.hxx>
#include <SelectBasics_SelectingVolumeManager.hxx>

namespace inspector {
namespace {
// Stock ViewCube sensitivity expands edges/corners and filters against the
// owner's diagonal. A tiled face needs uniform precision and its own normal,
// otherwise hidden/grazing tiles can win detection through the visible gaps.
class TileSensitive : public AIS_ViewCubeSensitive {
public:
    TileSensitive(const occ::handle<AIS_ViewCubeOwner>& owner,
                  const occ::handle<Graphic3d_ArrayOfTriangles>& triangles,const gp_Dir& normal)
        : AIS_ViewCubeSensitive(owner,triangles),normal_(normal) { SetSensitivityFactor(1); }
    bool Matches(SelectBasics_SelectingVolumeManager& manager,SelectBasics_PickResult& result) override {
        return manager.GetActiveSelectionType()==SelectMgr_SelectionType_Point &&
            manager.GetViewRayDirection().Dot(normal_) < -0.01 &&
            Select3D_SensitivePrimitiveArray::Matches(manager,result);
    }
private:
    gp_Dir normal_;
};
}

std::vector<CubeTile> cubeTiles(V3d_TypeOfOrientation orientation, double size) {
    const auto d=V3d::GetProjAxis(orientation);
    const auto sign=[](double n) { return std::abs(n)<0.1?0:(n>0?1:-1); };
    const std::array<int,3> grid{sign(d.X()),sign(d.Y()),sign(d.Z())};
    std::vector<CubeTile> result;
    for(int axis=0;axis<3;++axis) {
        if(!grid[axis]) continue;
        const int u=(axis+1)%3,v=(axis+2)%3;
        gp_XYZ normal(0,0,0);normal.SetCoord(axis+1,grid[axis]);
        CubeTile tile{orientation,gp_Dir(normal),{}};
        // Dark gaps leave nine distinct squares per face. CCW seen from outside.
        constexpr int offsets[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
        for(int corner=0;corner<4;++corner) {
            const int index=grid[axis]>0?corner:3-corner;
            gp_XYZ p;
            p.SetCoord(axis+1,grid[axis]*size/2);
            p.SetCoord(u+1,grid[u]*size/3+offsets[index][0]*size*0.15);
            p.SetCoord(v+1,grid[v]*size/3+offsets[index][1]*size*0.15);
            tile.corners[corner]=gp_Pnt(p);
        }
        result.push_back(tile);
    }
    return result;
}
ViewCube::ViewCube() {
    setPixelRatio(1);
    SetAutoStartAnimation(false); // Viewport routes clicks without selecting model owners.
    SetYup(false);
    Attributes()->SetFaceBoundaryDraw(false);
    Attributes()->SetDatumAspect(new Prs3d_DatumAspect());
    Attributes()->DatumAspect()->SetDrawLabels(false);

    SetBoxColor(Quantity_Color(0.58,0.72,0.87,Quantity_TOC_RGB));
    for(int i=V3d_Xpos;i<=V3d_Zneg;++i) SetBoxSideLabel(static_cast<V3d_TypeOfOrientation>(i),"");
}
void ViewCube::setPixelRatio(double ratio) {
    SetSize(84*ratio);
    SetBoxFacetExtension(0);
    SetAxesPadding(8*ratio);
    SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers | Graphic3d_TMF_OrthoPers,
        Aspect_TOTP_LEFT_LOWER,NCollection_Vec2<int>(int(std::round(100*ratio)),int(std::round(100*ratio)))));
}
void ViewCube::createBoxPartTriangles(const occ::handle<Graphic3d_ArrayOfTriangles>& tris,
                                    int& nodes,int& triangles,V3d_TypeOfOrientation direction) const {
    for(const auto& tile:cubeTiles(direction,Size())) {
        if(!tris.IsNull()) {
            const int start=tris->VertexNumber();
            for(const auto& p:tile.corners) tris->AddVertex(p,tile.normal);
            for(int i:{1,2,3,1,3,4}) tris->AddEdge(start+i);
        }
        nodes+=4;triangles+=2;
    }
}
void ViewCube::ComputeSelection(const occ::handle<SelectMgr_Selection>& selection,int mode) {
    if(mode!=0) return;
    for(int i=0;i<26;++i) {
        const auto orientation=static_cast<V3d_TypeOfOrientation>(i);
        const occ::handle<AIS_ViewCubeOwner> owner=new AIS_ViewCubeOwner(this,orientation);
        for(const auto& tile:cubeTiles(orientation,Size())) {
            const occ::handle<Graphic3d_ArrayOfTriangles> triangles=new Graphic3d_ArrayOfTriangles(4,6);
            for(const auto& point:tile.corners) triangles->AddVertex(point);
            for(int index:{1,2,3,1,3,4}) triangles->AddEdge(index);
            selection->Add(new TileSensitive(owner,triangles,tile.normal));
        }
    }
}
void ViewCube::Compute(const occ::handle<PrsMgr_PresentationManager>& manager,
                       const occ::handle<Prs3d_Presentation>& presentation,int mode) {
    AIS_ViewCube::Compute(manager,presentation,mode);
    if(mode!=0) return;
    // Small vector X/Y/Z +/- labels: works with the existing FreeType-OFF install.
    occ::handle<Graphic3d_ArrayOfSegments> lines=new Graphic3d_ArrayOfSegments(128);
    for(int i=V3d_Xpos;i<=V3d_Zneg;++i) {
        const auto dir=V3d::GetProjAxis(static_cast<V3d_TypeOfOrientation>(i));
        const gp_Dir up=i==V3d_Zpos?gp::DY():i==V3d_Zneg?-gp::DY():gp::DZ();
        const auto right=up.Crossed(dir);
        const gp_Pnt center(dir.XYZ()*(Size()/2+0.3));
        const auto line=[&](double x,double y,double a,double b) {
            lines->AddVertex(center.Translated((gp_Vec(right)*x+gp_Vec(up)*y)*(Size()/84)));
            lines->AddVertex(center.Translated((gp_Vec(right)*a+gp_Vec(up)*b)*(Size()/84)));
        };
        if(i==V3d_Xpos || i==V3d_Xneg) {line(-8,-5,-2,5);line(-8,5,-2,-5);}
        else if(i==V3d_Ypos || i==V3d_Yneg) {line(-8,5,-5,0);line(-2,5,-5,0);line(-5,0,-5,-5);}
        else {line(-8,5,-2,5);line(-2,5,-8,-5);line(-8,-5,-2,-5);}
        line(2,0,8,0);
        if(i<=V3d_Zpos) line(5,-3,5,3);
    }
    const auto group=presentation->NewGroup();
    const occ::handle<Prs3d_LineAspect> aspect=new Prs3d_LineAspect(Quantity_NOC_BLACK,Aspect_TOL_SOLID,1.5);
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(lines);
}
}
