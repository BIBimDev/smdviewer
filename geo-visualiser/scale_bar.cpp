#include "scale_bar.hpp"
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_LineAspect.hxx>
#include <QString>
#include <array>
#include <cmath>
#include <map>

namespace inspector {
bool ScaleBar::update(double mmPerPixel,double ratio,bool perspective) {
    if(!std::isfinite(mmPerPixel) || mmPerPixel<=0) return false;
    const double maximum=150*ratio*mmPerPixel;
    const double decade=std::pow(10.0,std::floor(std::log10(maximum)));
    const double length=(maximum>=5*decade?5:maximum>=2*decade?2:1)*decade;
    const double pixels=length/mmPerPixel;
    const auto label=(QString::number(length>=1000?length/1000:length,'g',6)+
                      (length>=1000?" m":" mm")+(perspective?" at target":"")).toStdString();
    if(lengthMm_==length && lengthPixels_==pixels && pixelRatio_==ratio && label_==label) return false;
    lengthMm_=length;lengthPixels_=pixels;pixelRatio_=ratio;label_=label;
    SetInfiniteState(true);
    SetAutoHilight(false);
    SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_2d,
        Aspect_TOTP_RIGHT_LOWER,NCollection_Vec2<int>(int(std::round(18*ratio)),int(std::round(18*ratio)))));
    SetToUpdate();
    return true;
}
void ScaleBar::Compute(const occ::handle<PrsMgr_PresentationManager>&,
                       const occ::handle<Prs3d_Presentation>& presentation,int) {
    // Small monoline bitmap glyphs avoid depending on OCCT FreeType support
    // (the accepted local visualization build has FreeType disabled).
    static const std::map<char,std::array<int,7>> glyphs={
        {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
        {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
        {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
        {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
        {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
        {'.',{0,0,0,0,0,12,12}}, {'-',{0,0,0,31,0,0,0}},
        {'+',{0,4,4,31,4,4,0}}, {'m',{0,0,26,21,21,21,21}},
        {'a',{0,0,14,1,15,17,15}}, {'t',{8,8,30,8,8,9,6}},
        {'r',{0,0,22,25,16,16,16}}, {'g',{0,0,15,17,15,1,14}},
        {'e',{0,0,14,17,31,16,14}}
    };
    const double cell=1.5*pixelRatio_;
    occ::handle<Graphic3d_ArrayOfSegments> lines=new Graphic3d_ArrayOfSegments(int(label_.size()*70+6));
    const auto line=[&](double x,double y,double xx,double yy) {
        lines->AddVertex(gp_Pnt(x,y,0));lines->AddVertex(gp_Pnt(xx,yy,0));
    };
    line(-lengthPixels_,0,0,0);
    line(-lengthPixels_,-3*pixelRatio_,-lengthPixels_,4*pixelRatio_);
    line(0,-3*pixelRatio_,0,4*pixelRatio_);
    double x=-double(label_.size()*6-1)*cell;
    for(char c:label_) {
        if(const auto glyph=glyphs.find(c);glyph!=glyphs.end())
            for(int row=0;row<7;++row) for(int col=0;col<5;++col)
                if(glyph->second[row] & (1<<(4-col)))
                    line(x+col*cell, (14+6*1.5-row*1.5)*pixelRatio_,
                         x+(col+0.85)*cell, (14+6*1.5-row*1.5)*pixelRatio_);
        x+=6*cell;
    }
    for(bool halo:{true,false}) {
        const auto group=presentation->NewGroup();
        const Quantity_Color colour=halo?Quantity_Color(0.035,0.045,0.06,Quantity_TOC_RGB):
                                          Quantity_Color(0.8,0.83,0.87,Quantity_TOC_RGB);
        group->SetGroupPrimitivesAspect((new Prs3d_LineAspect(colour,Aspect_TOL_SOLID,
            (halo?3.5:1.5)*pixelRatio_))->Aspect());
        group->AddPrimitiveArray(lines);
    }
}
}
