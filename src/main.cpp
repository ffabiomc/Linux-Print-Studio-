#include "core.hpp"
#include <adwaita.h>
#include <gtk/gtk.h>
#include <poppler.h>
#include <cairo-pdf.h>
#include <libintl.h>
#include <locale.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr const char* translation_domain="linux-print-studio";
const char* tr(const char* message) { return dgettext(translation_domain,message); }
template<typename T> struct ObjectUnref {
    void operator()(T* ptr) const noexcept { if(ptr) g_object_unref(ptr); }
};
template<typename T> using ObjectPtr=std::unique_ptr<T,ObjectUnref<T>>;
struct GFree {
    void operator()(gchar* ptr) const noexcept { g_free(ptr); }
};
using OwnedString=std::unique_ptr<gchar,GFree>;
}
struct State {
    GtkWidget *window=nullptr,*preview=nullptr,*status=nullptr,*page_label=nullptr,*prev_button=nullptr,*next_button=nullptr,*print_button=nullptr,*export_button=nullptr,*document_info=nullptr,*zoom=nullptr,*scroller=nullptr;
    GtkWidget *scale_control=nullptr,*margin_control=nullptr,*center_control=nullptr,*color_control=nullptr,*quality_control=nullptr,*printer_details=nullptr,*page_mode=nullptr,*page_range=nullptr,*layout=nullptr,*orientation=nullptr,*paper=nullptr,*copies=nullptr,*duplex=nullptr,*printer=nullptr,*reverse_backs=nullptr,*manual_fronts=nullptr,*manual_backs=nullptr,*preview_pass=nullptr,*duplex_hint=nullptr,*calibration_front=nullptr,*calibration_back=nullptr;
    ObjectPtr<PopplerDocument> doc;
    std::string path;
    std::vector<std::string> printer_names;
    std::vector<std::string> printer_descriptions;
    int page=0;
    double zoom_factor=1.0;
    bool fit_width=false;
    int last_view_width=0,last_view_height=0;
    bool manual_fronts_sent=false;
    bool reload_confirmed=false;
    std::string manual_printer;
    std::string manual_document;
    GtkWidget *reload_check=nullptr,*wizard_step=nullptr,*feed_direction=nullptr,*feed_label=nullptr,*calibration_save=nullptr;
    bool page_selection_valid=true;
    lps::RenderOptions render_options;
    std::vector<size_t> selected_page_numbers;

};
static void status(State* s,const std::string& message) { gtk_label_set_text(GTK_LABEL(s->status),message.c_str()); }
static std::vector<lps::Side> layout_sides(State* s);
static void advanced_changed(GObject*,GParamSpec*,gpointer);
static void refresh_manual_wizard(State* s);
static void reset_manual_wizard(State* s);
static std::pair<double,double> output_size(State* s);
static std::vector<lps::Side> visible_sides(State* s) {
    const auto all=layout_sides(s);
    if(!s->preview_pass || !s->duplex ||
       gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex))!=3) return all;
    const unsigned pass=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->preview_pass));
    if(pass==0 || all.empty()) return all;
    const auto plan=lps::manual_duplex(all.size(),
        gtk_check_button_get_active(GTK_CHECK_BUTTON(s->reverse_backs)));
    const auto& indices=pass==1?plan.fronts:plan.backs;
    if(indices.empty()) return {};
    return lps::select_sides(all,indices);
}
static std::string duplex_profile_path() {
    return std::string(g_get_user_config_dir())+"/linux-print-studio/duplex-profiles.ini";
}
static std::string selected_printer_name(State* s) {
    if(!s->printer) return {};
    const unsigned index=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->printer));
    return index<s->printer_names.size()?s->printer_names[index]:std::string{};
}
static void save_duplex_profile(State* s) {
    const auto printer=selected_printer_name(s);
    if(printer.empty()) {status(s,tr("Select a printer"));return;}
    const auto path=duplex_profile_path();
    std::unique_ptr<GKeyFile,decltype(&g_key_file_free)> file(g_key_file_new(),g_key_file_free);
    g_key_file_load_from_file(file.get(),path.c_str(),G_KEY_FILE_NONE,nullptr);
    g_key_file_set_boolean(file.get(),printer.c_str(),"reverse-backs",
        gtk_check_button_get_active(GTK_CHECK_BUTTON(s->reverse_backs)));
    g_key_file_set_integer(file.get(),printer.c_str(),"feed-direction",
        static_cast<int>(gtk_drop_down_get_selected(GTK_DROP_DOWN(s->feed_direction))));
    const auto directory=std::filesystem::path(path).parent_path();
    std::error_code error;
    std::filesystem::create_directories(directory,error);
    if(error) {status(s,error.message());return;}
    GError* gerror=nullptr;
    if(!g_key_file_save_to_file(file.get(),path.c_str(),&gerror)) {
        status(s,gerror?gerror->message:tr("Could not save calibration"));
        g_clear_error(&gerror);return;
    }
    status(s,tr("Calibration saved for this printer"));
}
static void load_duplex_profile(State* s) {
    if(!s->feed_direction || !s->reverse_backs) return;
    ObjectPtr<GKeyFile> file(g_key_file_new());
    const auto path=duplex_profile_path();
    const auto printer=selected_printer_name(s);
    if(printer.empty() || !g_key_file_load_from_file(file.get(),path.c_str(),G_KEY_FILE_NONE,nullptr)) return;
    if(g_key_file_has_key(file.get(),printer.c_str(),"reverse-backs",nullptr))
        gtk_check_button_set_active(GTK_CHECK_BUTTON(s->reverse_backs),
            g_key_file_get_boolean(file.get(),printer.c_str(),"reverse-backs",nullptr));
    if(g_key_file_has_key(file.get(),printer.c_str(),"feed-direction",nullptr)) {
        const int direction=g_key_file_get_integer(file.get(),printer.c_str(),"feed-direction",nullptr);
        if(direction>=0 && direction<=2)
            gtk_drop_down_set_selected(GTK_DROP_DOWN(s->feed_direction),static_cast<guint>(direction));
    }
}
static void reset_manual_wizard(State* s) {
    s->manual_fronts_sent=false;
    s->reload_confirmed=false;
    s->manual_printer.clear();
    s->manual_document.clear();
    if(s->reload_check) gtk_check_button_set_active(GTK_CHECK_BUTTON(s->reload_check),FALSE);
    if(s->manual_backs) gtk_widget_set_sensitive(s->manual_backs,FALSE);
    refresh_manual_wizard(s);
}
static void refresh_manual_wizard(State* s) {
    if(!s->wizard_step) return;
    const bool manual=s->duplex && gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex))==3;
    if(!manual) return;
    const bool valid=s->doc && s->page_selection_valid && !selected_printer_name(s).empty();
    const auto all=s->doc && s->page_selection_valid?layout_sides(s):std::vector<lps::Side>{};
    const bool has_backs=all.size()>1;
    if(s->manual_fronts) gtk_widget_set_sensitive(s->manual_fronts,valid && !s->manual_fronts_sent);
    if(s->reload_check) gtk_widget_set_sensitive(s->reload_check,s->manual_fronts_sent && has_backs);
    if(s->manual_backs) gtk_widget_set_sensitive(s->manual_backs,
        s->manual_fronts_sent && s->reload_confirmed && has_backs &&
        valid && selected_printer_name(s)==s->manual_printer && s->path==s->manual_document);
    const char* step=!valid?tr("Select a PDF and printer to begin"):
        !s->manual_fronts_sent?tr("Step 1 of 3: Print the front sides"):
        !has_backs?tr("Only front sides are needed"):
        !s->reload_confirmed?tr("Step 2 of 3: Reload paper and confirm"):
        tr("Step 3 of 3: Print the back sides");
    gtk_label_set_text(GTK_LABEL(s->wizard_step),step);
    if(s->duplex_hint && s->manual_fronts_sent && has_backs) {
        const unsigned direction=s->feed_direction?
            gtk_drop_down_get_selected(GTK_DROP_DOWN(s->feed_direction)):0;
        const char* guidance=direction==1?
            tr("After the front pass finishes, reload the stack with its top edge entering first, as verified by calibration."):
            direction==2?
            tr("After the front pass finishes, reload the stack with its bottom edge entering first, as verified by calibration."):
            tr("Paper orientation is not calibrated. Use the front/back calibration sheets before printing a large job.");
        gtk_label_set_text(GTK_LABEL(s->duplex_hint),guidance);
    }
}
static void reload_toggled(GtkCheckButton* check,gpointer data) {
    auto* s=static_cast<State*>(data);
    s->reload_confirmed=gtk_check_button_get_active(check);
    refresh_manual_wizard(s);
}
static void calibration_save_clicked(GtkButton*,gpointer data) {
    save_duplex_profile(static_cast<State*>(data));
}
static void refresh_duplex_hint(State* s) {
    if(!s->duplex_hint || !s->doc) return;
    const auto all=layout_sides(s);
    const auto plan=lps::manual_duplex(all.size(),
        gtk_check_button_get_active(GTK_CHECK_BUTTON(s->reverse_backs)));
    const std::string message="Manual duplex: "+std::to_string(plan.fronts.size())+
        " front side(s), "+std::to_string(plan.backs.size())+
        " back side(s). Print fronts first; wait for the printer to finish. "
        "Reload paper according to your printer's feed direction, then print backs. "
        "Test with two sheets before a large job.";
    gtk_label_set_text(GTK_LABEL(s->duplex_hint),message.c_str());
    refresh_manual_wizard(s);
}

static void resize_preview(State* s) {
    if(!s->scroller || !s->preview) return;
    const int vw=gtk_widget_get_width(s->scroller);
    const int vh=gtk_widget_get_height(s->scroller);
    if(vw<50 || vh<50) return;
    const int available_w=std::max(50,vw-40);
    const int available_h=std::max(50,vh-40);
    if(!s->doc) {
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(s->preview),vw);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(s->preview),vh);
        return;
    }
    const auto [pw,ph]=output_size(s);
    if(pw<=0 || ph<=0) return;
    const double fit=std::min(available_w/pw,available_h/ph);
    const double scale=(s->fit_width?available_w/pw:fit)*s->zoom_factor;
    const int desired_w=std::max(vw,static_cast<int>(std::ceil(pw*scale))+40);
    const int desired_h=std::max(vh,static_cast<int>(std::ceil(ph*scale))+40);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(s->preview),desired_w);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(s->preview),desired_h);
}
static gboolean preview_resize_tick(GtkWidget*,GdkFrameClock*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->scroller) return G_SOURCE_CONTINUE;
    const int w=gtk_widget_get_width(s->scroller);
    const int h=gtk_widget_get_height(s->scroller);
    if(w!=s->last_view_width || h!=s->last_view_height) {
        s->last_view_width=w;s->last_view_height=h;
        resize_preview(s);
    }
    return G_SOURCE_CONTINUE;
}
static void preview_draw(GtkDrawingArea*,cairo_t* cr,int width,int height,gpointer data) {
    auto* s=static_cast<State*>(data);
    cairo_set_source_rgb(cr,.88,.89,.91);
    cairo_paint(cr);
    if(!s->doc) {
        cairo_set_source_rgb(cr,.42,.46,.51);
        cairo_set_line_width(cr,3);
        const double cx=width/2.0,cy=height/2.0-28;
        cairo_rectangle(cr,cx-25,cy-35,50,70);cairo_stroke(cr);
        cairo_move_to(cr,cx-13,cy-12);cairo_line_to(cr,cx+13,cy-12);
        cairo_move_to(cr,cx-13,cy);cairo_line_to(cr,cx+13,cy);
        cairo_move_to(cr,cx-13,cy+12);cairo_line_to(cr,cx+7,cy+12);cairo_stroke(cr);
        cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr,15);
        const char* hint=tr("Open a PDF to preview and configure printing");
        cairo_text_extents_t ext{};
        cairo_text_extents(cr,hint,&ext);
        cairo_move_to(cr,cx-ext.width/2-ext.x_bearing,cy+67);
        cairo_show_text(cr,hint);
        return;
    }
    if(width<=40 || height<=40) return;
    const auto sides=visible_sides(s);
    if(sides.empty() || s->page<0 || static_cast<size_t>(s->page)>=sides.size()) return;
    auto [pw,ph]=output_size(s);
    if(pw<=0 || ph<=0) return;
    const int vw=s->scroller?gtk_widget_get_width(s->scroller):width;
    const int vh=s->scroller?gtk_widget_get_height(s->scroller):height;
    const double fitted=std::max(.01,std::min((vw-40.0)/pw,(vh-40.0)/ph));
    const double zoom=std::max(.01,(s->fit_width?(vw-40.0)/pw:fitted)*s->zoom_factor);
    const double ox=(width-pw*zoom)/2.0, oy=(height-ph*zoom)/2.0;
    cairo_save(cr);
    cairo_translate(cr,ox,oy);
    cairo_scale(cr,zoom,zoom);
    cairo_set_source_rgb(cr,1,1,1);
    cairo_rectangle(cr,0,0,pw,ph);
    cairo_fill(cr);
    const auto& side=sides[static_cast<size_t>(s->page)];
    const size_t count=side.size();
    const size_t cols=(count==2 ? (pw>=ph ? 2 : 1) :
                       count==6 ? (pw>=ph ? 3 : 2) :
                       static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count)))));
    const size_t rows=(count+cols-1)/cols;
    const double margin=s->render_options.margin_mm*72.0/25.4;
    const double cellw=(pw-2*margin)/static_cast<double>(cols);
    const double cellh=(ph-2*margin)/static_cast<double>(rows);
    for(size_t j=0;j<count;j++) {
        const auto& selected_page=side[j];
        if(!selected_page.has_value() || selected_page.value()==0) continue;
        const size_t page_index=selected_page.value()-1;
        if(page_index>=static_cast<size_t>(poppler_document_get_n_pages(s->doc.get()))) continue;
        ObjectPtr<PopplerPage> page(poppler_document_get_page(s->doc.get(),static_cast<int>(page_index)));
        if(!page) continue;
        double w=0,h=0;
        poppler_page_get_size(page.get(),&w,&h);
        if(std::isfinite(w) && std::isfinite(h) && w>0 && h>0) {
            const double scale=std::min(cellw/w,cellh/h)*s->render_options.scale_percent/100.0;
            const size_t row=j/cols;
            const double cx=margin+static_cast<double>(j%cols)*cellw, cy=margin+static_cast<double>(row)*cellh;
            cairo_save(cr);
            cairo_rectangle(cr,cx,cy,cellw,cellh);
            cairo_clip(cr);
            cairo_translate(cr,cx+(s->render_options.centered?(cellw-w*scale)/2:0),
                cy+(s->render_options.centered?(cellh-h*scale)/2:0));
            cairo_scale(cr,scale,scale);
            if(s->render_options.grayscale) {
                const int iw=std::clamp(static_cast<int>(std::ceil(w)),1,14400);
                const int ih=std::clamp(static_cast<int>(std::ceil(h)),1,14400);
                struct SurfaceDestroy {void operator()(cairo_surface_t* p) const noexcept {if(p)cairo_surface_destroy(p);}};
                struct ContextDestroy {void operator()(cairo_t* p) const noexcept {if(p)cairo_destroy(p);}};
                std::unique_ptr<cairo_surface_t,SurfaceDestroy> image(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,iw,ih));
                if(cairo_surface_status(image.get())==CAIRO_STATUS_SUCCESS) {
                    std::unique_ptr<cairo_t,ContextDestroy> image_cr(cairo_create(image.get()));
                    cairo_set_source_rgb(image_cr.get(),1,1,1);cairo_paint(image_cr.get());
                    poppler_page_render_for_printing(page.get(),image_cr.get());
                    cairo_surface_flush(image.get());
                    auto* pixels=cairo_image_surface_get_data(image.get());
                    const int stride=cairo_image_surface_get_stride(image.get());
                    for(int yy=0;yy<ih;yy++) {
                        auto* row=reinterpret_cast<uint32_t*>(pixels+static_cast<std::ptrdiff_t>(yy)*stride);
                        for(int xx=0;xx<iw;xx++) {
                            const uint32_t px=row[xx];
                            const unsigned a=(px>>24)&255,r=(px>>16)&255,g=(px>>8)&255,b=px&255;
                            const unsigned gray=(54*r+183*g+19*b+128)/256;
                            row[xx]=(a<<24)|(gray<<16)|(gray<<8)|gray;
                        }
                    }
                    cairo_surface_mark_dirty(image.get());
                    cairo_set_source_surface(cr,image.get(),0,0);cairo_paint(cr);
                }
            } else poppler_page_render_for_printing(page.get(),cr);
            cairo_restore(cr);
        }
    }
    cairo_restore(cr);
}
static void update_page(State* s) {
    if(!s->doc) {
        if(s->prev_button) gtk_widget_set_sensitive(s->prev_button,FALSE);
        if(s->next_button) gtk_widget_set_sensitive(s->next_button,FALSE);
        if(s->print_button) gtk_widget_set_sensitive(s->print_button,FALSE);
        if(s->export_button) gtk_widget_set_sensitive(s->export_button,FALSE);
        if(s->document_info) gtk_label_set_text(GTK_LABEL(s->document_info),tr("No document"));
        if(s->preview) gtk_widget_queue_draw(s->preview);
        return;
    }
    const auto sides=visible_sides(s);
    const int count=static_cast<int>(sides.size());
    s->page=std::clamp(s->page,0,std::max(0,count-1));
    const unsigned pass=s->preview_pass?
        gtk_drop_down_get_selected(GTK_DROP_DOWN(s->preview_pass)):0;
    const bool manual=s->duplex &&
        gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex))==3;
    const std::string prefix=manual && pass==1?tr("Front"):manual && pass==2?tr("Back"):tr("Sheet");
    const std::string caption=count==0?tr("No sides in this pass"):
        prefix+" "+std::to_string(s->page+1)+" / "+std::to_string(count);
    gtk_label_set_text(GTK_LABEL(s->page_label),caption.c_str());
    if(s->prev_button) gtk_widget_set_sensitive(s->prev_button,s->page>0);
    if(s->next_button) gtk_widget_set_sensitive(s->next_button,s->page+1<count);
    if(s->export_button) gtk_widget_set_sensitive(s->export_button,s->page_selection_valid);
    if(s->print_button) gtk_widget_set_sensitive(s->print_button,s->page_selection_valid && !s->printer_names.empty());
    if(s->document_info) {
        const int pages=poppler_document_get_n_pages(s->doc.get());
        const auto [w,h]=output_size(s);
        const std::string info=std::to_string(pages)+" "+tr("pages")+" · "+
            std::to_string(count)+" "+tr("sheets")+" · "+
            std::to_string(static_cast<int>(std::round(w*25.4/72)))+" × "+
            std::to_string(static_cast<int>(std::round(h*25.4/72)))+" mm";
        gtk_label_set_text(GTK_LABEL(s->document_info),info.c_str());
    }
    if(manual) refresh_duplex_hint(s);
    resize_preview(s);
    gtk_widget_queue_draw(s->preview);
}
static void printer_changed(GObject*,GParamSpec*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->printer_details) return;
    const unsigned selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->printer));
    const bool valid=selected<s->printer_names.size();
    gtk_label_set_text(GTK_LABEL(s->printer_details),
        valid?s->printer_descriptions[selected].c_str():tr("No printers configured"));
    if(s->print_button) gtk_widget_set_sensitive(s->print_button,valid && static_cast<bool>(s->doc) && s->page_selection_valid);
    if(s->manual_fronts) gtk_widget_set_sensitive(s->manual_fronts,valid && static_cast<bool>(s->doc));
    if(s->calibration_front) gtk_widget_set_sensitive(s->calibration_front,valid);
    if(s->wizard_step) refresh_manual_wizard(s);
}
static void refresh_printers(State* s) {
    const unsigned old_index=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->printer));
    const std::string previous=old_index<s->printer_names.size()?s->printer_names[old_index]:"";
    const auto destinations=lps::printer_details();
    s->printer_names.clear();
    s->printer_descriptions.clear();
    ObjectPtr<GtkStringList> models(gtk_string_list_new(nullptr));
    const std::string preferred=previous.empty()?lps::default_printer():previous;
    unsigned selected=GTK_INVALID_LIST_POSITION;
    for(const auto& item:destinations) {
        const unsigned index=static_cast<unsigned>(s->printer_names.size());
        s->printer_names.push_back(item.name);
        s->printer_descriptions.push_back(item.description);
        gtk_string_list_append(models.get(),item.name.c_str());
        if(item.name==preferred) selected=index;
    }
    if(selected==GTK_INVALID_LIST_POSITION && !s->printer_names.empty()) selected=0;
    gtk_drop_down_set_model(GTK_DROP_DOWN(s->printer),G_LIST_MODEL(models.get()));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(s->printer),selected);
    printer_changed(nullptr,nullptr,s);
    load_duplex_profile(s);
    reset_manual_wizard(s);
}
static void refresh_printers_clicked(GtkButton*,gpointer data) {
    refresh_printers(static_cast<State*>(data));
}
static void page_selection_changed(GObject*,GParamSpec*,gpointer data) {
    auto* s=static_cast<State*>(data);
    const unsigned mode=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->page_mode));
    gtk_widget_set_visible(s->page_range,mode==3);
    if(!s->doc) return;
    const size_t total=static_cast<size_t>(poppler_document_get_n_pages(s->doc.get()));
    std::vector<size_t> pages;
    try {
        if(mode==3) pages=lps::parse_page_range(gtk_editable_get_text(GTK_EDITABLE(s->page_range)),total);
        else for(size_t n=1;n<=total;n++) if(mode==0 || (mode==1 && n%2==0) || (mode==2 && n%2==1)) pages.push_back(n);
        if(pages.empty()) throw std::invalid_argument("No pages match the selection");
    } catch(const std::exception&) {
        s->page_selection_valid=false;
        gtk_widget_add_css_class(s->page_range,"error");
        status(s,tr("Invalid page range; use e.g. 1-5, 8, 12-15"));
        if(s->print_button) gtk_widget_set_sensitive(s->print_button,FALSE);
        if(s->export_button) gtk_widget_set_sensitive(s->export_button,FALSE);
        return;
    }
    s->page_selection_valid=true;
    gtk_widget_remove_css_class(s->page_range,"error");
    s->selected_page_numbers=std::move(pages);
    s->page=0;
    reset_manual_wizard(s);
    update_page(s);
}
static void page_range_changed(GtkEditable*,gpointer data) {
    page_selection_changed(nullptr,nullptr,data);
}
static void advanced_changed(GObject*,GParamSpec*,gpointer data) {
    auto* s=static_cast<State*>(data);
    s->render_options.scale_percent=gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->scale_control));
    s->render_options.margin_mm=gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->margin_control));
    s->render_options.centered=gtk_check_button_get_active(GTK_CHECK_BUTTON(s->center_control));
    s->render_options.grayscale=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->color_control))==1;
    if(s->doc) update_page(s);
}
static void advanced_spin_changed(GtkSpinButton*,gpointer data) {advanced_changed(nullptr,nullptr,data);}
static void advanced_toggle_changed(GtkCheckButton*,gpointer data) {advanced_changed(nullptr,nullptr,data);}
static void layout_changed(GObject*,GParamSpec*,gpointer data) {
    auto* s=static_cast<State*>(data);
    s->page=0;
    s->manual_fronts_sent=false;
    if(s->manual_backs) gtk_widget_set_sensitive(s->manual_backs,FALSE);
    update_page(s);
}
static void load_pdf(State* s,const char* path) {
    GError* err=nullptr;
    OwnedString uri(g_filename_to_uri(path,nullptr,&err));
    if(!uri) {status(s,err?err->message:tr("Invalid path"));g_clear_error(&err);return;}
    ObjectPtr<PopplerDocument> doc(poppler_document_new_from_file(uri.get(),nullptr,&err));
    if(!doc) {status(s,err?err->message:tr("Could not open PDF"));g_clear_error(&err);return;}
    s->doc=std::move(doc);s->path=path;s->page=0;
    s->manual_fronts_sent=false;
    if(s->manual_backs) gtk_widget_set_sensitive(s->manual_backs,FALSE);
    status(s,std::filesystem::path(path).filename().string());
    page_selection_changed(nullptr,nullptr,s);
}
static void chooser_response(GtkNativeDialog* dialog,int response,gpointer) {
    // The dialog owns a reference to the window until the response callback ends.
    // This keeps the State stored on the window alive even if the user closes it.
    auto* owner=static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(dialog),"owner-window"));
    auto* s=owner?static_cast<State*>(g_object_get_data(G_OBJECT(owner),"state")):nullptr;
    if(s && response==GTK_RESPONSE_ACCEPT) {
        ObjectPtr<GFile> file(gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog)));
        if(file) {
            OwnedString path(g_file_get_path(file.get()));
            if(path) load_pdf(s,path.get());
        }
    }
    g_object_unref(dialog);
}
static void open_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    auto* chooser=gtk_file_chooser_native_new(tr("Open PDF"),GTK_WINDOW(s->window),GTK_FILE_CHOOSER_ACTION_OPEN,tr("Open"),tr("Cancel"));
    // Explicitly retain the window for asynchronous file-chooser callbacks.
    g_object_set_data_full(G_OBJECT(chooser),"owner-window",g_object_ref(s->window),
                           [](gpointer owner){g_object_unref(owner);});
    GtkFileFilter* filter=gtk_file_filter_new();
    gtk_file_filter_set_name(filter,tr("PDF documents"));
    gtk_file_filter_add_mime_type(filter,"application/pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser),filter);
    g_signal_connect(chooser,"response",G_CALLBACK(chooser_response),nullptr);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}
static gboolean dropped_file(GtkDropTarget*,const GValue* value,double,double,gpointer data) {
    auto* s=static_cast<State*>(data);
    GFile* file=G_FILE(g_value_get_object(value));
    if(!file) return FALSE;
    OwnedString path(g_file_get_path(file));
    if(!path) {status(s,tr("Only local PDF files are supported"));return FALSE;}
    const auto name=std::string(path.get());
    if(name.size()<4 || g_ascii_strcasecmp(name.c_str()+name.size()-4,".pdf")!=0) {
        status(s,tr("Drop a PDF file"));return FALSE;
    }
    load_pdf(s,path.get());
    return TRUE;
}
static void zoom_changed(GtkDropDown* dropdown,GParamSpec*,gpointer data) {
    auto* s=static_cast<State*>(data);
    const unsigned index=gtk_drop_down_get_selected(dropdown);
    s->fit_width=index==1;
    s->zoom_factor=index==2?.5:index==3?.75:index==4?1.0:index==5?1.25:index==6?1.5:index==7?2.0:1.0;
    resize_preview(s);
    gtk_widget_queue_draw(s->preview);
}
static void page_back(GtkButton*,gpointer data){auto* s=static_cast<State*>(data);s->page--;update_page(s);}
static void page_next(GtkButton*,gpointer data){auto* s=static_cast<State*>(data);s->page++;update_page(s);}
static std::vector<lps::Side> layout_sides(State* s) {
    const size_t pages=s->selected_page_numbers.empty()?
        static_cast<size_t>(poppler_document_get_n_pages(s->doc.get())):s->selected_page_numbers.size();
    unsigned selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->layout));
    const auto layout=selected==1?lps::booklet(pages):
        lps::n_up(pages,selected>=2?(selected==2?2:selected==3?4:selected==4?6:selected==5?9:16):1);
    return s->selected_page_numbers.empty()?layout:lps::remap_pages(layout,s->selected_page_numbers);
}
static std::pair<double,double> paper_size(State* s) {
    switch(gtk_drop_down_get_selected(GTK_DROP_DOWN(s->paper))) {
        case 1: return {612.0,792.0};
        case 2: return {612.0,1008.0};
        default: return {595.2756,841.8898};
    }
}
static std::pair<double,double> output_size(State* s) {
    auto [w,h]=paper_size(s);
    unsigned orientation=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->orientation));
    unsigned layout=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->layout));
    if(orientation==2 || (orientation==0 && (layout==1 || layout==2))) std::swap(w,h);
    return {w,h};
}
static void export_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->doc){status(s,tr("Open a PDF first"));return;}
    if(!s->page_selection_valid) {status(s,tr("Invalid page range"));return;}
    try {
        auto target=std::filesystem::path(s->path);
        target.replace_filename(target.stem().string()+"-print-studio.pdf");
        auto [w,h]=output_size(s);
        lps::export_pdf(s->path,target.string(),layout_sides(s),w,h,s->render_options);
        status(s,"Exported: "+target.string());
    }catch(const std::exception& e){status(s,e.what());}
}
static bool selected_printer(State* s,std::string& name) {
    const auto selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->printer));
    if(selected>=s->printer_names.size()) {status(s,tr("Select a printer"));return false;}
    name=s->printer_names[selected];
    return true;
}
static bool print_selected(State* s,const std::vector<lps::Side>& sides,lps::PrintSides binding,const std::string& message) {
    std::string printer_name;
    if(!selected_printer(s,printer_name)) return false;
    try {
        const auto target=std::filesystem::temp_directory_path()/
            ("linux-print-studio-"+std::to_string(g_get_real_time())+".pdf");
        const auto [w,h]=output_size(s);
        lps::export_pdf(s->path,target.string(),sides,w,h,s->render_options);
        const int id=lps::print_pdf(target.string(),printer_name,
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->copies)),binding,
            static_cast<int>(gtk_drop_down_get_selected(GTK_DROP_DOWN(s->quality_control))));
        status(s,message+" (CUPS job "+std::to_string(id)+")");
        return true;
    }catch(const std::exception& e){status(s,e.what());return false;}
}
static void print_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->doc){status(s,tr("Open a PDF first"));return;}
    if(!s->page_selection_valid) {status(s,tr("Invalid page range"));return;}
    const auto selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex));
    if(selected==3) {
        status(s,tr("Manual duplex: click Print fronts first, then reload paper and click Print backs"));
        return;
    }
    const auto binding=selected==1?lps::PrintSides::LongEdge:
                       selected==2?lps::PrintSides::ShortEdge:lps::PrintSides::Simplex;
    print_selected(s,layout_sides(s),binding,tr("Print job submitted"));
}
static void manual_fronts_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->doc){status(s,tr("Open a PDF first"));return;}
    if(gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex))!=3 || !s->page_selection_valid) return;
    std::string printer_name;
    if(!selected_printer(s,printer_name)) return;
    try {
        const auto sides=layout_sides(s);
        const auto plan=lps::manual_duplex(sides.size(),
            gtk_check_button_get_active(GTK_CHECK_BUTTON(s->reverse_backs)));
        const auto selected=lps::select_sides(sides,plan.fronts);
        const auto target=std::filesystem::temp_directory_path()/
            ("linux-print-studio-fronts-"+std::to_string(g_get_real_time())+".pdf");
        const auto [w,h]=output_size(s);
        lps::export_pdf(s->path,target.string(),selected,w,h,s->render_options);
        const int id=lps::print_pdf(target.string(),printer_name,
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->copies)),lps::PrintSides::Simplex,
            static_cast<int>(gtk_drop_down_get_selected(GTK_DROP_DOWN(s->quality_control))));
        s->manual_fronts_sent=true;
        s->manual_printer=printer_name;
        s->manual_document=s->path;
        s->reload_confirmed=false;
        if(s->reload_check) gtk_check_button_set_active(GTK_CHECK_BUTTON(s->reload_check),FALSE);
        refresh_manual_wizard(s);
        status(s,"Fronts submitted (job "+std::to_string(id)+"). Wait for printing to finish, reload paper, then click Print backs.");
    }catch(const std::exception& e){status(s,e.what());}
}
static void manual_backs_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->doc || !s->manual_fronts_sent || !s->reload_confirmed ||
       !s->page_selection_valid || s->path!=s->manual_document ||
       selected_printer_name(s)!=s->manual_printer ||
       gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex))!=3) return;
    try {
        const auto sides=layout_sides(s);
        const auto plan=lps::manual_duplex(sides.size(),
            gtk_check_button_get_active(GTK_CHECK_BUTTON(s->reverse_backs)));
        if(plan.backs.empty()){status(s,tr("No back sides required"));return;}
        const auto selected=lps::select_sides(sides,plan.backs);
        if(print_selected(s,selected,lps::PrintSides::Simplex,tr("Back sides submitted; check paper orientation"))) {
            reset_manual_wizard(s);
        }
    }catch(const std::exception& e){status(s,e.what());}
}
// Print a physical alignment target; printer feed direction must be verified by the user.
static void calibration_clicked(GtkButton* button,gpointer data) {
    auto* s=static_cast<State*>(data);
    std::string printer_name;
    if(!selected_printer(s,printer_name)) return;
    const bool back=GTK_WIDGET(button)==s->calibration_back;
    try {
        const auto [w,h]=output_size(s);
        const auto target=std::filesystem::temp_directory_path()/
            ("linux-print-studio-calibration-"+std::to_string(g_get_real_time())+".pdf");
        struct SurfaceDestroy {
            void operator()(cairo_surface_t* p) const noexcept {if(p) cairo_surface_destroy(p);}
        };
        struct ContextDestroy {
            void operator()(cairo_t* p) const noexcept {if(p) cairo_destroy(p);}
        };
        std::unique_ptr<cairo_surface_t,SurfaceDestroy> surface(
            cairo_pdf_surface_create(target.string().c_str(),w,h));
        if(!surface || cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS)
            throw std::runtime_error("Unable to create calibration PDF");
        std::unique_ptr<cairo_t,ContextDestroy> cr(cairo_create(surface.get()));
        if(!cr || cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS)
            throw std::runtime_error("Unable to draw calibration PDF");
        cairo_set_source_rgb(cr.get(),1,1,1);
        cairo_paint(cr.get());
        cairo_set_source_rgb(cr.get(),0,0,0);
        cairo_set_line_width(cr.get(),2);
        cairo_rectangle(cr.get(),24,24,w-48,h-48);
        cairo_stroke(cr.get());
        cairo_select_font_face(cr.get(),"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr.get(),22);
        cairo_move_to(cr.get(),42,85);
        cairo_show_text(cr.get(),back?"CALIBRATION - BACK":"CALIBRATION - FRONT");
        cairo_set_font_size(cr.get(),13);
        cairo_move_to(cr.get(),42,115);
        cairo_show_text(cr.get(),"TOP EDGE - keep this mark visible");
        cairo_move_to(cr.get(),42,h-65);
        cairo_show_text(cr.get(),back?"BACK / BOTTOM":"FRONT / BOTTOM");
        // Distinct corner markers make rotation and feed reversal observable.
        cairo_set_font_size(cr.get(),30);
        cairo_move_to(cr.get(),45,175);
        cairo_show_text(cr.get(),back?"B1":"F1");
        cairo_move_to(cr.get(),w-100,175);
        cairo_show_text(cr.get(),back?"B2":"F2");
        cairo_move_to(cr.get(),45,h-110);
        cairo_show_text(cr.get(),back?"B3":"F3");
        cairo_move_to(cr.get(),w-100,h-110);
        cairo_show_text(cr.get(),back?"B4":"F4");
        cairo_show_page(cr.get());
        if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS)
            throw std::runtime_error("Calibration drawing failed");
        cr.reset();
        cairo_surface_finish(surface.get());
        if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS)
            throw std::runtime_error("Calibration PDF write failed");
        surface.reset();
        const int id=lps::print_pdf(target.string(),printer_name,1,lps::PrintSides::Simplex);
        if(!back) {
            gtk_widget_set_sensitive(s->calibration_back,TRUE);
            status(s,"Calibration front submitted (job "+std::to_string(id)+
                "). Wait until printed; reload the same sheet, then print calibration back.");
        } else {
            status(s,"Calibration back submitted (job "+std::to_string(id)+
                "). Inspect F1-F4 and B1-B4 to determine feed direction. No automatic detection.");
        }
    }catch(const std::exception& e){status(s,e.what());}
}
static void duplex_changed(GObject*,GParamSpec*,gpointer data) {
    auto* s=static_cast<State*>(data);
    const bool manual=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex))==3;
    gtk_widget_set_visible(s->reverse_backs,manual);
    gtk_widget_set_visible(s->manual_fronts,manual);
    gtk_widget_set_visible(s->manual_backs,manual);
    reset_manual_wizard(s);
    gtk_widget_set_visible(s->preview_pass,manual);
    gtk_widget_set_visible(s->duplex_hint,manual);
    gtk_widget_set_visible(s->calibration_front,manual);
    gtk_widget_set_visible(s->calibration_back,manual);
    if(s->reload_check) gtk_widget_set_visible(s->reload_check,manual);
    if(s->wizard_step) gtk_widget_set_visible(s->wizard_step,manual);
    if(s->feed_direction) gtk_widget_set_visible(s->feed_direction,manual);
    if(s->feed_label) gtk_widget_set_visible(s->feed_label,manual);
    if(s->calibration_save) gtk_widget_set_visible(s->calibration_save,manual);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(s->preview_pass),0);
    s->page=0;
    update_page(s);
}
static void preview_pass_changed(GObject*,GParamSpec*,gpointer data) {
    auto* s=static_cast<State*>(data);
    s->page=0;
    update_page(s);
}
static void reverse_backs_changed(GtkCheckButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    s->manual_fronts_sent=false;
    gtk_widget_set_sensitive(s->manual_backs,FALSE);
    s->page=0;
    update_page(s);
}
static GtkWidget* label(const char* text){auto* w=gtk_label_new(tr(text));gtk_widget_set_halign(w,GTK_ALIGN_START);return w;}
static void activate(GApplication* application,gpointer) {
    auto state=std::make_unique<State>();
    auto* s=state.get();
    s->window=adw_application_window_new(GTK_APPLICATION(application));
    gtk_window_set_title(GTK_WINDOW(s->window),tr("Linux Print Studio"));
    gtk_window_set_default_size(GTK_WINDOW(s->window),1080,720);
    g_object_set_data_full(G_OBJECT(s->window),"state",state.release(),[](gpointer p){std::unique_ptr<State> owned(static_cast<State*>(p));});
    auto* outer=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(s->window),outer);
    auto* header=adw_header_bar_new();
    gtk_box_append(GTK_BOX(outer),header);
    auto* open=gtk_button_new_with_label(tr("Open PDF"));
    g_signal_connect(open,"clicked",G_CALLBACK(open_clicked),s);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header),open);
    auto* body=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    gtk_widget_set_vexpand(body,TRUE);
    gtk_box_append(GTK_BOX(outer),body);
    auto* sidebar_scroll=gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sidebar_scroll,320,-1);
    gtk_box_append(GTK_BOX(body),sidebar_scroll);
    auto* side=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    gtk_widget_set_margin_start(side,18);gtk_widget_set_margin_end(side,18);
    gtk_widget_set_margin_top(side,18);gtk_widget_set_margin_bottom(side,18);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scroll),side);
    gtk_box_append(GTK_BOX(side),label(tr("Page layout")));
    const char* layouts[]={tr("Normal"),tr("Booklet"),tr("2 pages / sheet"),tr("4 pages / sheet"),tr("6 pages / sheet"),tr("9 pages / sheet"),tr("16 pages / sheet"),nullptr};
    s->layout=gtk_drop_down_new_from_strings(layouts);
    gtk_box_append(GTK_BOX(side),s->layout);
    gtk_box_append(GTK_BOX(side),label(tr("Pages to print")));
    const char* page_modes[]={tr("All pages"),tr("Even pages"),tr("Odd pages"),tr("Custom range"),nullptr};
    s->page_mode=gtk_drop_down_new_from_strings(page_modes);
    gtk_box_append(GTK_BOX(side),s->page_mode);
    s->page_range=gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->page_range),"1-5, 8, 12-15");
    gtk_widget_set_visible(s->page_range,FALSE);
    gtk_box_append(GTK_BOX(side),s->page_range);
    g_signal_connect(s->page_mode,"notify::selected",G_CALLBACK(page_selection_changed),s);
    g_signal_connect(s->page_range,"changed",G_CALLBACK(page_range_changed),s);
    gtk_box_append(GTK_BOX(side),label(tr("Orientation")));
    const char* orientations[]={tr("Automatic"),tr("Portrait"),tr("Landscape"),nullptr};
    s->orientation=gtk_drop_down_new_from_strings(orientations);
    gtk_box_append(GTK_BOX(side),s->orientation);
    gtk_box_append(GTK_BOX(side),label(tr("Paper size")));
    const char* papers[]={tr("A4"),tr("Letter"),tr("Legal"),nullptr};
    s->paper=gtk_drop_down_new_from_strings(papers);
    gtk_box_append(GTK_BOX(side),s->paper);
    g_signal_connect(s->layout,"notify::selected",G_CALLBACK(layout_changed),s);
    g_signal_connect(s->orientation,"notify::selected",G_CALLBACK(layout_changed),s);
    g_signal_connect(s->paper,"notify::selected",G_CALLBACK(layout_changed),s);
    auto* advanced=gtk_expander_new(tr("Advanced settings"));
    auto* advanced_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
    gtk_expander_set_child(GTK_EXPANDER(advanced),advanced_box);
    gtk_box_append(GTK_BOX(side),advanced);
    gtk_box_append(GTK_BOX(advanced_box),label(tr("Scale (%)")));
    s->scale_control=gtk_spin_button_new_with_range(10,200,5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->scale_control),100);
    gtk_box_append(GTK_BOX(advanced_box),s->scale_control);
    gtk_box_append(GTK_BOX(advanced_box),label(tr("Margins (mm)")));
    s->margin_control=gtk_spin_button_new_with_range(0,50,1);
    gtk_box_append(GTK_BOX(advanced_box),s->margin_control);
    s->center_control=gtk_check_button_new_with_label(tr("Center content"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(s->center_control),TRUE);
    gtk_box_append(GTK_BOX(advanced_box),s->center_control);
    gtk_box_append(GTK_BOX(advanced_box),label(tr("Color mode")));
    const char* color_modes[]={tr("Color"),tr("Grayscale"),nullptr};
    s->color_control=gtk_drop_down_new_from_strings(color_modes);
    gtk_box_append(GTK_BOX(advanced_box),s->color_control);
    gtk_box_append(GTK_BOX(advanced_box),label(tr("Print quality")));
    const char* quality_modes[]={tr("Printer default"),tr("Draft"),tr("Normal quality"),tr("High quality"),nullptr};
    s->quality_control=gtk_drop_down_new_from_strings(quality_modes);
    gtk_box_append(GTK_BOX(advanced_box),s->quality_control);
    g_signal_connect(s->scale_control,"value-changed",G_CALLBACK(advanced_spin_changed),s);
    g_signal_connect(s->margin_control,"value-changed",G_CALLBACK(advanced_spin_changed),s);
    g_signal_connect(s->center_control,"toggled",G_CALLBACK(advanced_toggle_changed),s);
    g_signal_connect(s->color_control,"notify::selected",G_CALLBACK(advanced_changed),s);
    gtk_box_append(GTK_BOX(side),label(tr("Printer")));
    const char* empty_printers[]={nullptr};
    s->printer=gtk_drop_down_new_from_strings(empty_printers);
    auto* printer_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
    gtk_widget_set_hexpand(s->printer,TRUE);
    gtk_box_append(GTK_BOX(printer_row),s->printer);
    auto* refresh=gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(refresh,tr("Refresh printers"));
    gtk_box_append(GTK_BOX(printer_row),refresh);
    gtk_box_append(GTK_BOX(side),printer_row);
    s->printer_details=label(tr("No printers configured"));
    gtk_label_set_wrap(GTK_LABEL(s->printer_details),TRUE);
    gtk_box_append(GTK_BOX(side),s->printer_details);
    g_signal_connect(s->printer,"notify::selected",G_CALLBACK(printer_changed),s);
    g_signal_connect(refresh,"clicked",G_CALLBACK(refresh_printers_clicked),s);
    refresh_printers(s);
    gtk_box_append(GTK_BOX(side),label(tr("Copies")));
    s->copies=gtk_spin_button_new_with_range(1,999,1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->copies),1);
    gtk_box_append(GTK_BOX(side),s->copies);
    gtk_box_append(GTK_BOX(side),label(tr("Duplex mode")));
    const char* duplex_modes[]={tr("One-sided"),tr("Automatic (long edge)"),tr("Automatic (short edge)"),tr("Manual (two passes)"),nullptr};
    s->duplex=gtk_drop_down_new_from_strings(duplex_modes);
    gtk_box_append(GTK_BOX(side),s->duplex);
    s->reverse_backs=gtk_check_button_new_with_label(tr("Reverse back-side order"));
    gtk_widget_set_visible(s->reverse_backs,FALSE);
    gtk_box_append(GTK_BOX(side),s->reverse_backs);
    s->wizard_step=gtk_label_new(tr("Step 1 of 3: Print the front sides"));
    gtk_label_set_wrap(GTK_LABEL(s->wizard_step),TRUE);
    gtk_label_set_xalign(GTK_LABEL(s->wizard_step),0);
    gtk_widget_set_visible(s->wizard_step,FALSE);
    gtk_box_append(GTK_BOX(side),s->wizard_step);
    s->manual_fronts=gtk_button_new_with_label(tr("1. Print fronts"));
    gtk_widget_set_visible(s->manual_fronts,FALSE);
    g_signal_connect(s->manual_fronts,"clicked",G_CALLBACK(manual_fronts_clicked),s);
    gtk_box_append(GTK_BOX(side),s->manual_fronts);
    s->reload_check=gtk_check_button_new_with_label(tr("Paper reloaded and printer has finished"));
    gtk_widget_set_visible(s->reload_check,FALSE);
    gtk_widget_set_sensitive(s->reload_check,FALSE);
    g_signal_connect(s->reload_check,"toggled",G_CALLBACK(reload_toggled),s);
    gtk_box_append(GTK_BOX(side),s->reload_check);
    s->manual_backs=gtk_button_new_with_label(tr("3. Print backs"));
    gtk_widget_set_visible(s->manual_backs,FALSE);
    gtk_widget_set_sensitive(s->manual_backs,FALSE);
    g_signal_connect(s->manual_backs,"clicked",G_CALLBACK(manual_backs_clicked),s);
    gtk_box_append(GTK_BOX(side),s->manual_backs);
    g_signal_connect(s->duplex,"notify::selected",G_CALLBACK(duplex_changed),s);
    g_signal_connect(s->reverse_backs,"toggled",G_CALLBACK(reverse_backs_changed),s);
    const char* passes[]={tr("All sheets"),tr("Front pass"),tr("Back pass"),nullptr};
    s->preview_pass=gtk_drop_down_new_from_strings(passes);
    gtk_widget_set_visible(s->preview_pass,FALSE);
    g_signal_connect(s->preview_pass,"notify::selected",G_CALLBACK(preview_pass_changed),s);
    gtk_box_append(GTK_BOX(side),s->preview_pass);
    s->duplex_hint=gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(s->duplex_hint),TRUE);
    gtk_label_set_xalign(GTK_LABEL(s->duplex_hint),0.0f);
    gtk_widget_set_visible(s->duplex_hint,FALSE);
    gtk_box_append(GTK_BOX(side),s->duplex_hint);
    s->calibration_front=gtk_button_new_with_label(tr("Calibration: print front"));
    gtk_widget_set_visible(s->calibration_front,FALSE);
    g_signal_connect(s->calibration_front,"clicked",G_CALLBACK(calibration_clicked),s);
    gtk_box_append(GTK_BOX(side),s->calibration_front);
    s->calibration_back=gtk_button_new_with_label(tr("Calibration: print back"));
    gtk_widget_set_visible(s->calibration_back,FALSE);
    gtk_widget_set_sensitive(s->calibration_back,FALSE);
    g_signal_connect(s->calibration_back,"clicked",G_CALLBACK(calibration_clicked),s);
    gtk_box_append(GTK_BOX(side),s->calibration_back);
    s->feed_label=label(tr("Paper reload orientation (calibrate first)"));
    gtk_widget_set_visible(s->feed_label,FALSE);
    gtk_box_append(GTK_BOX(side),s->feed_label);
    const char* feeds[]={tr("Not calibrated"),tr("Top edge first"),tr("Bottom edge first"),nullptr};
    s->feed_direction=gtk_drop_down_new_from_strings(feeds);
    gtk_widget_set_visible(s->feed_direction,FALSE);
    gtk_box_append(GTK_BOX(side),s->feed_direction);
    s->calibration_save=gtk_button_new_with_label(tr("Save calibration for printer"));
    gtk_widget_set_visible(s->calibration_save,FALSE);
    g_signal_connect(s->calibration_save,"clicked",G_CALLBACK(calibration_save_clicked),s);
    gtk_box_append(GTK_BOX(side),s->calibration_save);
    load_duplex_profile(s);
    auto* export_button=gtk_button_new_with_label(tr("Export PDF"));
    s->export_button=export_button;
    gtk_widget_add_css_class(export_button,"suggested-action");
    g_signal_connect(export_button,"clicked",G_CALLBACK(export_clicked),s);
    gtk_box_append(GTK_BOX(side),export_button);
    auto* print_button=gtk_button_new_with_label(tr("Print"));
    s->print_button=print_button;
    g_signal_connect(print_button,"clicked",G_CALLBACK(print_clicked),s);
    gtk_box_append(GTK_BOX(side),print_button);
    auto* main=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
    gtk_widget_set_hexpand(main,TRUE);
    gtk_box_append(GTK_BOX(body),main);
    s->scroller=gtk_scrolled_window_new();
    gtk_widget_set_hexpand(s->scroller,TRUE);
    gtk_widget_set_vexpand(s->scroller,TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s->scroller),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
    s->preview=gtk_drawing_area_new();
    gtk_widget_set_hexpand(s->preview,TRUE);
    gtk_widget_set_vexpand(s->preview,TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s->scroller),s->preview);
    gtk_widget_add_tick_callback(s->scroller,preview_resize_tick,s,nullptr);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s->preview),preview_draw,s,nullptr);
    gtk_box_append(GTK_BOX(main),s->scroller);
    auto* drop=gtk_drop_target_new(G_TYPE_FILE,GDK_ACTION_COPY);
    g_signal_connect(drop,"drop",G_CALLBACK(dropped_file),s);
    gtk_widget_add_controller(s->preview,GTK_EVENT_CONTROLLER(drop));
    auto* zoom_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_widget_set_halign(zoom_row,GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(main),zoom_row);
    gtk_box_append(GTK_BOX(zoom_row),label(tr("Zoom")));
    const char* zoom_options[]={tr("Fit page"),tr("Fit width"),"50%","75%","100%","125%","150%","200%",nullptr};
    s->zoom=gtk_drop_down_new_from_strings(zoom_options);
    gtk_box_append(GTK_BOX(zoom_row),s->zoom);
    g_signal_connect(s->zoom,"notify::selected",G_CALLBACK(zoom_changed),s);
    s->document_info=label(tr("No document"));
    gtk_box_append(GTK_BOX(zoom_row),s->document_info);
    auto* nav=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_widget_set_halign(nav,GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(nav,10);
    gtk_box_append(GTK_BOX(main),nav);
    auto* prev=gtk_button_new_with_label(tr("Previous"));
    s->prev_button=prev;
    g_signal_connect(prev,"clicked",G_CALLBACK(page_back),s);
    gtk_box_append(GTK_BOX(nav),prev);
    s->page_label=label(tr("No document"));
    gtk_box_append(GTK_BOX(nav),s->page_label);
    auto* next=gtk_button_new_with_label(tr("Next"));
    s->next_button=next;
    g_signal_connect(next,"clicked",G_CALLBACK(page_next),s);
    gtk_box_append(GTK_BOX(nav),next);
    s->status=label(tr("Open a PDF to begin"));
    gtk_widget_set_margin_start(s->status,16);
    gtk_widget_set_margin_bottom(s->status,8);
    gtk_box_append(GTK_BOX(outer),s->status);
    update_page(s);
    gtk_window_present(GTK_WINDOW(s->window));
}
int main(int argc,char** argv) {
    setlocale(LC_ALL,"");
    bindtextdomain("linux-print-studio",LPS_LOCALE_DIR);
    bind_textdomain_codeset("linux-print-studio","UTF-8");
    ObjectPtr<AdwApplication> app(adw_application_new("io.github.ffabiomc.LinuxPrintStudio",G_APPLICATION_DEFAULT_FLAGS));
    g_signal_connect(app.get(),"activate",G_CALLBACK(activate),nullptr);
    int result=g_application_run(G_APPLICATION(app.get()),argc,argv);
    return result;
}
