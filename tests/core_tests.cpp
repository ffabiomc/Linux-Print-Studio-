#include "core.hpp"
#include <cstdlib>
#include <iostream>
#define assert(condition) do { if(!(condition)) { std::cerr << "Test failed: " #condition << " at line " << __LINE__ << '\n'; return EXIT_FAILURE; } } while(false)
#include <stdexcept>
int main() {
    const auto selected=lps::parse_page_range("1-3, 7, 3, 9-10",10);
    assert((selected==std::vector<size_t>{1,2,3,7,9,10}));
    const auto mapped=lps::remap_pages(lps::n_up(selected.size(),2),selected);
    assert(mapped.size()==3 && mapped[0][0]==1 && mapped[1][1]==7 && mapped[2][1]==10);
    const auto booklet_mapped=lps::remap_pages(lps::booklet(6),selected);
    assert(booklet_mapped[0][1]==1 && !booklet_mapped[0][0]);
    for(const char* invalid:{"","0","11","2-1","1-","1,,2","1,","x","1-3-4"}) {
        bool rejected=false;
        try {lps::parse_page_range(invalid,10);}catch(const std::invalid_argument&){rejected=true;}
        assert(rejected);
    }
    lps::validate_render_options({},595,842);
    lps::RenderOptions advanced;
    advanced.scale_percent=125;advanced.margin_mm=12;advanced.centered=false;advanced.grayscale=true;
    lps::validate_render_options(advanced,595,842);
    bool bad_render=false;
    advanced.scale_percent=201;
    try {lps::validate_render_options(advanced,595,842);}catch(const std::invalid_argument&){bad_render=true;}
    assert(bad_render);
    advanced.scale_percent=100;advanced.margin_mm=50;
    bad_render=false;
    try {lps::validate_render_options(advanced,100,100);}catch(const std::invalid_argument&){bad_render=true;}
    assert(bad_render);
    auto b=lps::booklet(8);
    assert(b.size()==4 && b[0][0]==8 && b[0][1]==1);
    assert(b[1][0]==2 && b[1][1]==7);
    auto padded=lps::booklet(6);
    assert(!padded[0][0] && padded[0][1]==1);
    auto n=lps::n_up(5,4);
    assert(n.size()==2 && n[1][0]==5 && !n[1][1]);
    auto d=lps::manual_duplex(8,true);
    assert(d.fronts[0]==1 && d.backs[0]==8 && d.backs[3]==2);
    const auto manual=lps::manual_duplex(5,false);
    assert(manual.fronts.size()==3 && manual.backs.size()==2);
    assert(manual.fronts[0]==1 && manual.fronts[2]==5);
    const auto all=lps::n_up(9,2);
    const auto fronts=lps::select_sides(all,manual.fronts);
    const auto backs=lps::select_sides(all,manual.backs);
    assert(fronts.size()==3 && backs.size()==2);
    assert(fronts[0][0]==1 && fronts[1][0]==5 && fronts[2][0]==9);
    assert(backs[0][0]==3 && backs[1][0]==7);
    bool failed=false;
    try {lps::select_sides(all,{0});}catch(const std::out_of_range&){failed=true;}
    assert(failed);
    failed=false;try {lps::n_up(2,3);}catch(const std::invalid_argument&){failed=true;}
    assert(failed);
    failed=false;
    try { lps::export_pdf("missing.pdf","out.pdf",lps::n_up(1,1),-1,842); }
    catch(const std::invalid_argument&) { failed=true; }
    assert(failed);
    failed=false;
    try { lps::export_pdf("missing.pdf","out.pdf",lps::n_up(1,1),1e100,842); }
    catch(const std::invalid_argument&) { failed=true; }
    assert(failed);
}
