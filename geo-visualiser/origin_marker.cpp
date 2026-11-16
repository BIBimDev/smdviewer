#include "origin_marker.hpp"
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_LineAspect.hxx>

namespace inspector {
OriginMarker::OriginMarker() {
    SetInfiniteState(true);
    SetAutoHilight(false);
    SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_ZoomPers,gp_Pnt(0,0,0)));
    // Default depth-tested model layer, never an always-on-top overlay.
    SetZLayer(Graphic3d_ZLayerId_Default);
}
void OriginMarker::setPixelRatio(double ratio) { pixelRatio_=ratio; SetToUpdate(); }
void OriginMarker::Compute(const occ::handle<PrsMgr_PresentationManager>&,
                           const occ::handle<Prs3d_Presentation>& presentation,int) {
    const Quantity_Color colors[]={{0.58,0.31,0.30,Quantity_TOC_RGB},
                                  {0.33,0.52,0.37,Quantity_TOC_RGB},
                                  {0.34,0.44,0.62,Quantity_TOC_RGB}};
    for(int axis=0;axis<3;++axis) {
        gp_XYZ tip(0,0,0),back(0,0,0),side(0,0,0);
        tip.SetCoord(axis+1,36*pixelRatio_);
        back.SetCoord(axis+1,29*pixelRatio_);
        side.SetCoord((axis+1)%3+1,3.5*pixelRatio_);
        occ::handle<Graphic3d_ArrayOfSegments> lines=new Graphic3d_ArrayOfSegments(6);
        lines->AddVertex(gp_Pnt(0,0,0)); lines->AddVertex(gp_Pnt(tip));
        lines->AddVertex(gp_Pnt(tip)); lines->AddVertex(gp_Pnt(back+side));
        lines->AddVertex(gp_Pnt(tip)); lines->AddVertex(gp_Pnt(back-side));
        const auto group=presentation->NewGroup();
        group->SetGroupPrimitivesAspect((new Prs3d_LineAspect(colors[axis],Aspect_TOL_SOLID,1.5*pixelRatio_))->Aspect());
        group->AddPrimitiveArray(lines);
    }
}
}
