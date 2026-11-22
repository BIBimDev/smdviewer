#include "decorations.hpp"
#include <AIS_DisplayMode.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Prs3d_IsoAspect.hxx>
#include <StdPrs_ShadedShape.hxx>
#include <StdPrs_WFShape.hxx>

namespace inspector {
FaceHighlight::FaceHighlight(const TopoDS_Shape& face) : face_(face) {
    SetInfiniteState(true);
    SetAutoHilight(false);
    auto drawer=Attributes();
    drawer->SetShadingAspect(new Prs3d_ShadingAspect());
    drawer->ShadingAspect()->SetColor(Quantity_Color(1.0,0.60,0.035,Quantity_TOC_RGB));
    drawer->ShadingAspect()->SetTransparency(0.48);
    drawer->ShadingAspect()->Aspect()->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    drawer->SetFaceBoundaryDraw(true);
    drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(Quantity_NOC_YELLOW,Aspect_TOL_SOLID,2));
}
void FaceHighlight::Compute(const occ::handle<PrsMgr_PresentationManager>&,
                            const occ::handle<Prs3d_Presentation>& presentation,int) {
    // OCCT triangulates the bounded face, including concave and inner wires.
    // Never build a fan from vertices or shade the face's unbounded plane.
    StdPrs_ShadedShape::Add(presentation,face_,Attributes(),StdPrs_Volume_Opened);
}
namespace {
void outlineStyle(const occ::handle<Prs3d_Drawer>& drawer,const Quantity_Color& color,double width) {
    drawer->SetWireAspect(new Prs3d_LineAspect(color,Aspect_TOL_DASH,width));
    drawer->SetUnFreeBoundaryAspect(new Prs3d_LineAspect(color,Aspect_TOL_DASH,width));
    drawer->SetFreeBoundaryAspect(new Prs3d_LineAspect(color,Aspect_TOL_DASH,width));
    drawer->SetIsoOnPlane(false);
    drawer->SetUIsoAspect(new Prs3d_IsoAspect(color,Aspect_TOL_SOLID,1,0));
    drawer->SetVIsoAspect(new Prs3d_IsoAspect(color,Aspect_TOL_SOLID,1,0));
}
}
ContextOutline::ContextOutline(const TopoDS_Shape& shape,bool reduction) : shape_(shape),halo_(new Prs3d_Drawer()) {
    SetInfiniteState(true);
    SetAutoHilight(false);
    SetDisplayMode(AIS_WireFrame);
    outlineStyle(halo_,Quantity_Color(0.025,0.03,0.04,Quantity_TOC_RGB),3.5);
    outlineStyle(Attributes(),reduction?Quantity_Color(0.52,0.36,0.38,Quantity_TOC_RGB):
                                      Quantity_Color(0.43,0.48,0.54,Quantity_TOC_RGB),1.8);
}
void ContextOutline::Compute(const occ::handle<PrsMgr_PresentationManager>&,
                             const occ::handle<Prs3d_Presentation>& presentation,int) {
    // Matching dashed strokes: dark edging contrasts with concrete, the light
    // core contrasts with the background. Neither pass fills or writes depth.
    StdPrs_WFShape::Add(presentation,shape_,halo_);
    StdPrs_WFShape::Add(presentation,shape_,Attributes());
}
}
