#pragma once
#include <AIS_InteractiveObject.hxx>
#include <string>

namespace inspector {
// Screen-space presentation only. Lengths refer to source-local millimetres.
class ScaleBar : public AIS_InteractiveObject {
public:
    bool update(double millimetresPerPixel,double pixelRatio,bool perspective);
    double lengthMm() const { return lengthMm_; }
    double lengthPixels() const { return lengthPixels_; }
    const std::string& label() const { return label_; }
    void Compute(const occ::handle<PrsMgr_PresentationManager>&,
                 const occ::handle<Prs3d_Presentation>&,int) override;
    void ComputeSelection(const occ::handle<SelectMgr_Selection>&,int) override {}
private:
    double lengthMm_=0,lengthPixels_=0,pixelRatio_=1;
    std::string label_;
};
}
