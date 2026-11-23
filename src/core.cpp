#include "core.hpp"
#include <poppler.h>
#include <cairo-pdf.h>
#include <cups/cups.h>
#include <glib.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <string_view>
#include <cmath>
#include <memory>
#include <cstdint>
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

std::vector<size_t> parse_page_range(const std::string& expression,size_t total_pages) {
    if(total_pages==0 || total_pages>100000) throw std::invalid_argument("Invalid page count");
    std::vector<size_t> result;
    std::vector<bool> seen(total_pages+1,false);
    const auto parse=[total_pages](std::string_view value)->size_t {
        while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        size_t number=0;
        if(value.empty()) throw std::invalid_argument("Invalid page range");
        auto [end,err]=std::from_chars(value.data(),value.data()+value.size(),number);
        if(err!=std::errc{} || end!=value.data()+value.size() || number<1 || number>total_pages)
            throw std::invalid_argument("Page outside document or invalid range");
        return number;
    };
    size_t start=0;
    while(start<expression.size()) {
        const size_t comma=expression.find(',',start);
        const auto part=std::string_view(expression).substr(start,comma==std::string::npos?std::string::npos:comma-start);
        const auto dash=part.find('-');
        const size_t first=parse(part.substr(0,dash));
        const size_t last=dash==std::string_view::npos?first:parse(part.substr(dash+1));
        if(last<first) throw std::invalid_argument("Descending page range");
        for(size_t n=first;n<=last;n++) if(!seen[n]) {seen[n]=true;result.push_back(n);}
        if(comma==std::string::npos) break;
        start=comma+1;
    }
    if(expression.empty() || expression.back()==',' || result.empty()) throw std::invalid_argument("Empty page range");
    return result;
}
std::vector<Side> remap_pages(const std::vector<Side>& sides,const std::vector<size_t>& pages) {
    std::vector<Side> mapped=sides;
    for(auto& side:mapped) for(auto& slot:side) if(slot) {
        if(*slot<1 || *slot>pages.size()) throw std::out_of_range("Invalid page mapping");
        slot=pages[*slot-1];
    }
    return mapped;
}

std::vector<Side> n_up(size_t pages, size_t per_sheet) {
    if (!pages || pages>100000 || (per_sheet!=1 && per_sheet!=2 && per_sheet!=4 && per_sheet!=6 && per_sheet!=9 && per_sheet!=16)) throw std::invalid_argument("Invalid N-up layout");
    std::vector<Side> out;
    for(size_t first=0;first<pages;first+=per_sheet) {
        Side side(per_sheet);
        for(size_t slot=0;slot<per_sheet && first+slot<pages;slot++) {
            side[slot]=first+slot+1;
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
std::vector<PrinterInfo> printer_details() {
    Dests dests;
    dests.count=cupsGetDests(&dests.data);
    std::vector<PrinterInfo> result;
    for(int i=0;i<dests.count;i++) {
        const auto& dest=dests.data[i];
        if(!dest.name) continue;
        const char* model=cupsGetOption("printer-make-and-model",dest.num_options,dest.options);
        const char* location=cupsGetOption("printer-location",dest.num_options,dest.options);
        const char* state=cupsGetOption("printer-state",dest.num_options,dest.options);
        const char* accepting=cupsGetOption("printer-is-accepting-jobs",dest.num_options,dest.options);
        const std::string state_text=state && std::string(state)=="5"?"Stopped":
            state && std::string(state)=="4"?"Processing":"Idle";
        std::string description=model && *model?model:dest.name;
        description+=" · "+state_text;
        if(accepting && std::string(accepting)=="false") description+=" · Not accepting jobs";
        if(location && *location) description+=" · "+std::string(location);
        result.push_back({dest.name,description});
    }
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
void validate_render_options(const RenderOptions& o,double pw,double ph) {
    if(!std::isfinite(o.scale_percent) || o.scale_percent<10 || o.scale_percent>200 ||
       !std::isfinite(o.margin_mm) || o.margin_mm<0 || o.margin_mm>50 ||
       o.margin_mm*72.0/25.4*2>=std::min(pw,ph))
        throw std::invalid_argument("Invalid scale or margins");
}
void export_pdf(const std::string& input,const std::string& output,const std::vector<Side>& sides,double pw,double ph,const RenderOptions& options) {
    validate_render_options(options,pw,ph);
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
        const double margin=options.margin_mm*72.0/25.4;
        const double usable_w=pw-2*margin, usable_h=ph-2*margin;
        const double cellw=usable_w/static_cast<double>(cols);
        const double cellh=usable_h/static_cast<double>(rows);
        cairo_set_source_rgb(cr.get(),1,1,1);
        cairo_paint(cr.get());
        const int document_pages=poppler_document_get_n_pages(d.document.get());
        for(size_t j=0;j<count;j++) {
            const auto& page_number=side[j];
            if(!page_number.has_value()) continue;
            const size_t number=page_number.value();
            if(number<1 || number>static_cast<size_t>(document_pages)) continue;
            GPtr<PopplerPage> page(poppler_document_get_page(d.document.get(),static_cast<int>(number-1)));
            if(!page) continue;
            double w=0,h=0;
            poppler_page_get_size(page.get(),&w,&h);
            if(!std::isfinite(w) || !std::isfinite(h) || w<=0 || h<=0) continue;
            const double scale=std::min(cellw/w,cellh/h)*options.scale_percent/100.0;
            const double cellx=margin+static_cast<double>(j%cols)*cellw;
            const size_t row=j/cols;
            const double celly=margin+static_cast<double>(row)*cellh;
            const double x=cellx+(options.centered?(cellw-w*scale)/2.0:0.0);
            const double y=celly+(options.centered?(cellh-h*scale)/2.0:0.0);
            cairo_save(cr.get());
            cairo_rectangle(cr.get(),cellx,celly,cellw,cellh);
            cairo_clip(cr.get());
            cairo_translate(cr.get(),x,y);
            cairo_scale(cr.get(),scale,scale);
            if(options.grayscale) {
                // Render the page to an ARGB32 image, convert RGB channels, then composite.
                const int iw=std::clamp(static_cast<int>(std::ceil(w)),1,14400);
                const int ih=std::clamp(static_cast<int>(std::ceil(h)),1,14400);
                std::unique_ptr<cairo_surface_t,SurfaceDeleter> image(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,iw,ih));
                if(cairo_surface_status(image.get())!=CAIRO_STATUS_SUCCESS) throw std::runtime_error("Grayscale image allocation failed");
                std::unique_ptr<cairo_t,ContextDeleter> image_cr(cairo_create(image.get()));
                cairo_set_source_rgb(image_cr.get(),1,1,1);cairo_paint(image_cr.get());
                poppler_page_render_for_printing(page.get(),image_cr.get());
                cairo_surface_flush(image.get());
                auto* pixels=cairo_image_surface_get_data(image.get());
                const int stride=cairo_image_surface_get_stride(image.get());
                for(int yy=0;yy<ih;yy++) {
                    auto* row=reinterpret_cast<uint32_t*>(pixels+yy*stride);
                    for(int xx=0;xx<iw;xx++) {
                        const uint32_t px=row[xx];
                        const unsigned a=(px>>24)&255,red=(px>>16)&255,green=(px>>8)&255,blue=px&255;
                        const unsigned gray=(54*red+183*green+19*blue+128)/256;
                        row[xx]=(a<<24)|(gray<<16)|(gray<<8)|gray;
                    }
                }
                cairo_surface_mark_dirty(image.get());
                cairo_set_source_surface(cr.get(),image.get(),0,0);
                cairo_paint(cr.get());
            } else poppler_page_render_for_printing(page.get(),cr.get());
            cairo_restore(cr.get());
        }
        cairo_show_page(cr.get());
    }
    cairo_status_t status=cairo_status(cr.get());
    cr.reset();cairo_surface_finish(surface.get());
    if(status==CAIRO_STATUS_SUCCESS) status=cairo_surface_status(surface.get());
    if(status!=CAIRO_STATUS_SUCCESS) throw std::runtime_error("PDF export failed");
}
std::vector<Side> select_sides(const std::vector<Side>& sides,const std::vector<size_t>& indices) {
    if(sides.empty() || indices.empty()) throw std::invalid_argument("Empty manual duplex selection");
    std::vector<Side> result;
    result.reserve(indices.size());
    for(size_t index:indices) {
        if(index==0 || index>sides.size()) throw std::out_of_range("Invalid manual duplex sheet side");
        result.push_back(sides[index-1]);
    }
    return result;
}
int print_pdf(const std::string& path,const std::string& printer,int copies,PrintSides sides) {
    if(printer.empty() || copies<1 || copies>999) throw std::invalid_argument("Invalid print options");
    Options options;
    options.count=cupsAddOption("copies",std::to_string(copies).c_str(),options.count,&options.data);
    const char* binding=sides==PrintSides::LongEdge?"two-sided-long-edge":
                        sides==PrintSides::ShortEdge?"two-sided-short-edge":"one-sided";
    options.count=cupsAddOption("sides",binding,options.count,&options.data);
    int id=cupsPrintFile(printer.c_str(),path.c_str(),"Linux Print Studio",options.count,options.data);
    if(id<=0) throw std::runtime_error(cupsLastErrorString());
    return id;
}
}
