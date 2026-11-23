#include "core.hpp"
#include <poppler.h>
#include <cairo-pdf.h>
#include <cups/cups.h>
#include <glib.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace lps {
namespace {
template<class T> struct GDeleter { void operator()(T* p) const noexcept { if(p) g_object_unref(p); } };
template<class T> using GPtr=std::unique_ptr<T,GDeleter<T>>;
struct FreeDeleter { void operator()(gchar* p) const noexcept { g_free(p); } };
struct SurfaceDeleter { void operator()(cairo_surface_t* p) const noexcept { if(p) cairo_surface_destroy(p); } };
struct ContextDeleter { void operator()(cairo_t* p) const noexcept { if(p) cairo_destroy(p); } };
struct Dests { cups_dest_t* data=nullptr; int count=0; ~Dests(){cupsFreeDests(count,data);} };
struct Options { cups_option_t* data=nullptr; int count=0; ~Options(){cupsFreeOptions(count,data);} };
}

std::vector<Side> n_up(size_t pages, size_t per_sheet) {
    if (!pages || (per_sheet!=1 && per_sheet!=2 && per_sheet!=4 && per_sheet!=6 && per_sheet!=9 && per_sheet!=16)) throw std::invalid_argument("Invalid N-up layout");
    std::vector<Side> out;
    for(size_t first=1;first<=pages;) {
        Side side(per_sheet);
        for(size_t slot=0;slot<per_sheet && first<=pages;slot++,first++) {
            side[slot]=first;
        }
        out.push_back(std::move(side));
    }
    return out;
}
std::vector<Side> booklet(size_t pages) {
    if(!pages || pages>100000) throw std::invalid_argument("Invalid page count");
    size_t padded=(pages+3)/4*4;
    auto slot=[pages](size_t n)->Slot { return n<=pages ? Slot(n) : std::nullopt; };
    std::vector<Side> out;
    for(size_t i=0;i<padded/4;i++) {
        out.push_back({slot(padded-2*i),slot(1+2*i)});
        out.push_back({slot(2+2*i),slot(padded-1-2*i)});
    }
    return out;
}
std::vector<Side> poster(size_t pages,size_t columns,size_t rows) {
    if(!pages || !columns || !rows || columns>8 || rows>8) throw std::invalid_argument("Invalid poster layout");
    std::vector<Side> out;
    for(size_t p=1;p<=pages;p++) for(size_t i=0;i<columns*rows;i++) out.push_back({p});
    return out;
}
DuplexPlan manual_duplex(size_t sides,bool reverse_backs) {
    if(!sides || sides>100000) throw std::invalid_argument("Invalid sides");
    DuplexPlan result;
    for(size_t i=1;i<=sides;i++) (i%2?result.fronts:result.backs).push_back(i);
    if(reverse_backs) std::reverse(result.backs.begin(),result.backs.end());
    return result;
}
std::vector<std::string> printers() {
    Dests dests;
    dests.count=cupsGetDests(&dests.data);
    std::vector<std::string> result;
    for(int i=0;i<dests.count;i++) if(dests.data[i].name) result.emplace_back(dests.data[i].name);
    return result;
}
std::string default_printer() {
    const char* p=cupsGetDefault();
    return p?p:"";
}
struct Doc {
    GPtr<PopplerDocument> document;
    explicit Doc(const std::string& path) {
        GError* error=nullptr;
        std::unique_ptr<gchar,FreeDeleter> uri(g_filename_to_uri(path.c_str(),nullptr,&error));
        if(!uri) {
            std::string message=error?error->message:"Invalid path";
            g_clear_error(&error); throw std::runtime_error(message);
        }
        document.reset(poppler_document_new_from_file(uri.get(),nullptr,&error));
        if(!document) {
            std::string message=error?error->message:"Unable to open PDF";
            g_clear_error(&error); throw std::runtime_error(message);
        }
    }
};
int page_count(const std::string& path) { Doc d(path); return poppler_document_get_n_pages(d.document.get()); }
void export_pdf(const std::string& input,const std::string& output,const std::vector<Side>& sides,double pw,double ph) {
    if(sides.empty() || !std::isfinite(pw) || !std::isfinite(ph) || pw<=0 || ph<=0 || pw>14400 || ph>14400 || input==output) throw std::invalid_argument("Invalid export");
    Doc d(input);
    std::unique_ptr<cairo_surface_t,SurfaceDeleter> surface(cairo_pdf_surface_create(output.c_str(),pw,ph));
    if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) throw std::runtime_error("PDF surface failed");
    std::unique_ptr<cairo_t,ContextDeleter> cr(cairo_create(surface.get()));
    if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) throw std::runtime_error("Cairo context failed");
    for(const auto& side:sides) {
        if(side.empty()) continue;
        size_t count=side.size();
        size_t cols=(count==2 ? (pw>=ph ? 2 : 1) :
                     count==6 ? (pw>=ph ? 3 : 2) :
                     static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count)))));
        size_t rows=(count+cols-1)/cols;
        double cellw=pw/cols, cellh=ph/rows;
        cairo_set_source_rgb(cr.get(),1,1,1);cairo_paint(cr.get());
        for(size_t j=0;j<count;j++) {
            if(!side[j]) continue;
            if(*side[j]<1 || *side[j]>static_cast<size_t>(poppler_document_get_n_pages(d.document.get()))) continue;
            GPtr<PopplerPage> page(poppler_document_get_page(d.document.get(),static_cast<int>(*side[j]-1)));
            if(!page) continue;
            double w=0,h=0;poppler_page_get_size(page.get(),&w,&h);
            if(w>0 && h>0) {
                double scale=std::min(cellw/w,cellh/h);
                double x=(j%cols)*cellw+(cellw-w*scale)/2;
                double y=(j/cols)*cellh+(cellh-h*scale)/2;
                cairo_save(cr.get());cairo_rectangle(cr.get(),(j%cols)*cellw,(j/cols)*cellh,cellw,cellh);cairo_clip(cr.get());
                cairo_translate(cr.get(),x,y);cairo_scale(cr.get(),scale,scale);
                poppler_page_render_for_printing(page.get(),cr.get());cairo_restore(cr.get());
            }
        }
        cairo_show_page(cr.get());
    }
    cairo_status_t status=cairo_status(cr.get());
    cr.reset();cairo_surface_finish(surface.get());
    if(status==CAIRO_STATUS_SUCCESS) status=cairo_surface_status(surface.get());
    if(status!=CAIRO_STATUS_SUCCESS) throw std::runtime_error("PDF export failed");
}
int print_pdf(const std::string& path,const std::string& printer,int copies,bool duplex) {
    if(printer.empty() || copies<1 || copies>999) throw std::invalid_argument("Invalid print options");
    Options options;
    options.count=cupsAddOption("copies",std::to_string(copies).c_str(),options.count,&options.data);
    if(duplex) options.count=cupsAddOption("sides","two-sided-long-edge",options.count,&options.data);
    int id=cupsPrintFile(printer.c_str(),path.c_str(),"Linux Print Studio",options.count,options.data);
    if(id<=0) throw std::runtime_error(cupsLastErrorString());
    return id;
}
}
