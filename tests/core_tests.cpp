#include "core.hpp"
#include <cassert>
#include <stdexcept>
int main() {
    auto b=lps::booklet(8);
    assert(b.size()==4 && b[0][0]==8 && b[0][1]==1);
    assert(b[1][0]==2 && b[1][1]==7);
    auto padded=lps::booklet(6);
    assert(!padded[0][0] && padded[0][1]==1);
    auto n=lps::n_up(5,4);
    assert(n.size()==2 && n[1][0]==5 && !n[1][1]);
    auto d=lps::manual_duplex(8,true);
    assert(d.fronts[0]==1 && d.backs[0]==8 && d.backs[3]==2);
    bool failed=false;try {lps::n_up(2,3);}catch(const std::invalid_argument&){failed=true;}
    assert(failed);
}
