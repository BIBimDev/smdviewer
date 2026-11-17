#pragma once
#include <AIS_InteractiveObject.hxx>
#include <TopoDS_Shape.hxx>

namespace inspector {
// Presentations only: these never construct sensitive entities. They consume
// display copies, never the authoritative source geometry.
class FaceHighlight : public AIS_InteractiveObject {
public:
    explicit FaceHighlight(const TopoDS_Shape&);
    void Compute(const occ::handle<PrsMgr_PresentationManager>&,
                 const occ::handle<Prs3d_Presentation>&,int) override;
    void ComputeSelection(const occ::handle<SelectMgr_Selection>&,int) override {}
private:
    TopoDS_Shape face_;
};
class ContextOutline : public AIS_InteractiveObject {
public:
    ContextOutline(const TopoDS_Shape&,bool reduction);
    void Compute(const occ::handle<PrsMgr_PresentationManager>&,
                 const occ::handle<Prs3d_Presentation>&,int) override;
    void ComputeSelection(const occ::handle<SelectMgr_Selection>&,int) override {}
private:
    TopoDS_Shape shape_;
    occ::handle<Prs3d_Drawer> halo_;
};
}
