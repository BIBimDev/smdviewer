#pragma once
#include <AIS_InteractiveObject.hxx>

namespace inspector {
// World-anchored overlay decoration. Pixel lengths are presentation units,
// never source millimetres; no sensitive entities or selection modes are built.
class OriginMarker : public AIS_InteractiveObject {
public:
    OriginMarker();
    void setPixelRatio(double);
    void Compute(const occ::handle<PrsMgr_PresentationManager>&,
                 const occ::handle<Prs3d_Presentation>&, int) override;
    void ComputeSelection(const occ::handle<SelectMgr_Selection>&, int) override {}
private:
    double pixelRatio_ = 1;
};
}
