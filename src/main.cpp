#include "core.hpp"
#include <adwaita.h>
#include <gtk/gtk.h>
#include <poppler.h>
#include <cairo-pdf.h>
#include <libintl.h>
#include <locale.h>
#include <algorithm>
#include <cmath>
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
    GtkWidget *window=nullptr,*preview=nullptr,*status=nullptr,*page_label=nullptr;
    GtkWidget *layout=nullptr,*orientation=nullptr,*paper=nullptr,*copies=nullptr,*duplex=nullptr,*printer=nullptr,*reverse_backs=nullptr,*manual_fronts=nullptr,*manual_backs=nullptr,*preview_pass=nullptr,*duplex_hint=nullptr,*calibration_front=nullptr,*calibration_back=nullptr;
    ObjectPtr<PopplerDocument> doc;
    std::string path;
    std::vector<std::string> printer_names;
    int page=0;
    bool manual_fronts_sent=false;

};
static void status(State* s,const std::string& message) { gtk_label_set_text(GTK_LABEL(s->status),message.c_str()); }
static std::vector<lps::Side> layout_sides(State* s);
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
}

static void preview_draw(GtkDrawingArea*,cairo_t* cr,int width,int height,gpointer data) {
    auto* s=static_cast<State*>(data);
    cairo_set_source_rgb(cr,.88,.89,.91);
    cairo_paint(cr);
    if(!s->doc || width<=40 || height<=40) return;
    const auto sides=visible_sides(s);
    if(sides.empty() || s->page<0 || static_cast<size_t>(s->page)>=sides.size()) return;
    auto [pw,ph]=output_size(s);
    if(pw<=0 || ph<=0) return;
    const double zoom=std::max(.01,std::min((width-40.0)/pw,(height-40.0)/ph));
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
    const double cellw=pw/static_cast<double>(cols), cellh=ph/static_cast<double>(rows);
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
            const double scale=std::min(cellw/w,cellh/h);
            const size_t row=j/cols;
            const double cx=static_cast<double>(j%cols)*cellw, cy=static_cast<double>(row)*cellh;
            cairo_save(cr);
            cairo_rectangle(cr,cx,cy,cellw,cellh);
            cairo_clip(cr);
            cairo_translate(cr,cx+(cellw-w*scale)/2,cy+(cellh-h*scale)/2);
            cairo_scale(cr,scale,scale);
            poppler_page_render_for_printing(page.get(),cr);
            cairo_restore(cr);
        }
    }
    cairo_restore(cr);
}
static void update_page(State* s) {
    if(!s->doc) return;
    const auto sides=visible_sides(s);
    const int count=static_cast<int>(sides.size());
    s->page=std::clamp(s->page,0,std::max(0,count-1));
    const unsigned pass=s->preview_pass?
        gtk_drop_down_get_selected(GTK_DROP_DOWN(s->preview_pass)):0;
    const bool manual=s->duplex &&
        gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex))==3;
    const std::string prefix=manual && pass==1?"Front":manual && pass==2?"Back":"Sheet";
    const std::string caption=count==0?"No sides in this pass":
        prefix+" "+std::to_string(s->page+1)+" / "+std::to_string(count);
    gtk_label_set_text(GTK_LABEL(s->page_label),caption.c_str());
    if(manual) refresh_duplex_hint(s);
    gtk_widget_queue_draw(s->preview);
}
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
    if(!uri) {status(s,err?err->message:"Invalid path");g_clear_error(&err);return;}
    ObjectPtr<PopplerDocument> doc(poppler_document_new_from_file(uri.get(),nullptr,&err));
    if(!doc) {status(s,err?err->message:"Could not open PDF");g_clear_error(&err);return;}
    s->doc=std::move(doc);s->path=path;s->page=0;
    s->manual_fronts_sent=false;
    if(s->manual_backs) gtk_widget_set_sensitive(s->manual_backs,FALSE);
    status(s,std::filesystem::path(path).filename().string());
    update_page(s);
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
    auto* chooser=gtk_file_chooser_native_new("Open PDF",GTK_WINDOW(s->window),GTK_FILE_CHOOSER_ACTION_OPEN,"Open","Cancel");
    // Explicitly retain the window for asynchronous file-chooser callbacks.
    g_object_set_data_full(G_OBJECT(chooser),"owner-window",g_object_ref(s->window),
                           [](gpointer owner){g_object_unref(owner);});
    GtkFileFilter* filter=gtk_file_filter_new();
    gtk_file_filter_set_name(filter,"PDF documents");
    gtk_file_filter_add_mime_type(filter,"application/pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser),filter);
    g_signal_connect(chooser,"response",G_CALLBACK(chooser_response),nullptr);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}
static void page_back(GtkButton*,gpointer data){auto* s=static_cast<State*>(data);s->page--;update_page(s);}
static void page_next(GtkButton*,gpointer data){auto* s=static_cast<State*>(data);s->page++;update_page(s);}
static std::vector<lps::Side> layout_sides(State* s) {
    size_t pages=static_cast<size_t>(poppler_document_get_n_pages(s->doc.get()));
    unsigned selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->layout));
    if(selected==1)return lps::booklet(pages);
    if(selected>=2)return lps::n_up(pages,selected==2?2:selected==3?4:selected==4?6:selected==5?9:16);
    return lps::n_up(pages,1);
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
    if(!s->doc){status(s,"Open a PDF first");return;}
    try {
        auto target=std::filesystem::path(s->path);
        target.replace_filename(target.stem().string()+"-print-studio.pdf");
        auto [w,h]=output_size(s);
        lps::export_pdf(s->path,target.string(),layout_sides(s),w,h);
        status(s,"Exported: "+target.string());
    }catch(const std::exception& e){status(s,e.what());}
}
static bool selected_printer(State* s,std::string& name) {
    const auto selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->printer));
    if(selected>=s->printer_names.size()) {status(s,"Select a printer");return false;}
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
        lps::export_pdf(s->path,target.string(),sides,w,h);
        const int id=lps::print_pdf(target.string(),printer_name,
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->copies)),binding);
        status(s,message+" (CUPS job "+std::to_string(id)+")");
        return true;
    }catch(const std::exception& e){status(s,e.what());return false;}
}
static void print_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->doc){status(s,"Open a PDF first");return;}
    const auto selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex));
    if(selected==3) {
        status(s,"Manual duplex: click Print fronts first, then reload paper and click Print backs");
        return;
    }
    const auto binding=selected==1?lps::PrintSides::LongEdge:
                       selected==2?lps::PrintSides::ShortEdge:lps::PrintSides::Simplex;
    print_selected(s,layout_sides(s),binding,"Print job submitted");
}
static void manual_fronts_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->doc){status(s,"Open a PDF first");return;}
    if(gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex))!=3) return;
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
        lps::export_pdf(s->path,target.string(),selected,w,h);
        const int id=lps::print_pdf(target.string(),printer_name,
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->copies)),lps::PrintSides::Simplex);
        s->manual_fronts_sent=true;
        gtk_widget_set_sensitive(s->manual_backs,TRUE);
        status(s,"Fronts submitted (job "+std::to_string(id)+"). Wait for printing to finish, reload paper, then click Print backs.");
    }catch(const std::exception& e){status(s,e.what());}
}
static void manual_backs_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->doc || !s->manual_fronts_sent ||
       gtk_drop_down_get_selected(GTK_DROP_DOWN(s->duplex))!=3) return;
    try {
        const auto sides=layout_sides(s);
        const auto plan=lps::manual_duplex(sides.size(),
            gtk_check_button_get_active(GTK_CHECK_BUTTON(s->reverse_backs)));
        if(plan.backs.empty()){status(s,"No back sides required");return;}
        const auto selected=lps::select_sides(sides,plan.backs);
        if(print_selected(s,selected,lps::PrintSides::Simplex,"Back sides submitted; check paper orientation")) {
            s->manual_fronts_sent=false;
            gtk_widget_set_sensitive(s->manual_backs,FALSE);
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
    s->manual_fronts_sent=false;
    gtk_widget_set_sensitive(s->manual_backs,FALSE);
    gtk_widget_set_visible(s->preview_pass,manual);
    gtk_widget_set_visible(s->duplex_hint,manual);
    gtk_widget_set_visible(s->calibration_front,manual);
    gtk_widget_set_visible(s->calibration_back,manual);
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
    auto* side=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    gtk_widget_set_size_request(side,285,-1);
    gtk_widget_set_margin_start(side,18);gtk_widget_set_margin_end(side,18);
    gtk_widget_set_margin_top(side,18);
    gtk_box_append(GTK_BOX(body),side);
    gtk_box_append(GTK_BOX(side),label("Page layout"));
    const char* layouts[]={tr("Normal"),tr("Booklet"),tr("2 pages / sheet"),tr("4 pages / sheet"),tr("6 pages / sheet"),tr("9 pages / sheet"),tr("16 pages / sheet"),nullptr};
    s->layout=gtk_drop_down_new_from_strings(layouts);
    gtk_box_append(GTK_BOX(side),s->layout);
    gtk_box_append(GTK_BOX(side),label("Orientation"));
    const char* orientations[]={tr("Automatic"),tr("Portrait"),tr("Landscape"),nullptr};
    s->orientation=gtk_drop_down_new_from_strings(orientations);
    gtk_box_append(GTK_BOX(side),s->orientation);
    gtk_box_append(GTK_BOX(side),label("Paper size"));
    const char* papers[]={tr("A4"),tr("Letter"),tr("Legal"),nullptr};
    s->paper=gtk_drop_down_new_from_strings(papers);
    gtk_box_append(GTK_BOX(side),s->paper);
    g_signal_connect(s->layout,"notify::selected",G_CALLBACK(layout_changed),s);
    g_signal_connect(s->orientation,"notify::selected",G_CALLBACK(layout_changed),s);
    g_signal_connect(s->paper,"notify::selected",G_CALLBACK(layout_changed),s);
    gtk_box_append(GTK_BOX(side),label("Printer"));
    s->printer_names=lps::printers();
    auto def=lps::default_printer();
    ObjectPtr<GtkStringList> models(gtk_string_list_new(nullptr));
    unsigned default_index=0;
    for(size_t i=0;i<s->printer_names.size();i++) {
        gtk_string_list_append(models.get(),s->printer_names[i].c_str());
        if(s->printer_names[i]==def)default_index=static_cast<unsigned>(i);
    }
    s->printer=gtk_drop_down_new(G_LIST_MODEL(models.get()),nullptr);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(s->printer),s->printer_names.empty()?GTK_INVALID_LIST_POSITION:default_index);
    gtk_box_append(GTK_BOX(side),s->printer);
    gtk_box_append(GTK_BOX(side),label("Copies"));
    s->copies=gtk_spin_button_new_with_range(1,999,1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->copies),1);
    gtk_box_append(GTK_BOX(side),s->copies);
    gtk_box_append(GTK_BOX(side),label("Duplex mode"));
    const char* duplex_modes[]={tr("One-sided"),tr("Automatic (long edge)"),tr("Automatic (short edge)"),tr("Manual (two passes)"),nullptr};
    s->duplex=gtk_drop_down_new_from_strings(duplex_modes);
    gtk_box_append(GTK_BOX(side),s->duplex);
    s->reverse_backs=gtk_check_button_new_with_label("Reverse back-side order");
    gtk_widget_set_visible(s->reverse_backs,FALSE);
    gtk_box_append(GTK_BOX(side),s->reverse_backs);
    s->manual_fronts=gtk_button_new_with_label("1. Print fronts");
    gtk_widget_set_visible(s->manual_fronts,FALSE);
    g_signal_connect(s->manual_fronts,"clicked",G_CALLBACK(manual_fronts_clicked),s);
    gtk_box_append(GTK_BOX(side),s->manual_fronts);
    s->manual_backs=gtk_button_new_with_label("2. Print backs (after reloading paper)");
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
    s->calibration_front=gtk_button_new_with_label("Calibration: print front");
    gtk_widget_set_visible(s->calibration_front,FALSE);
    g_signal_connect(s->calibration_front,"clicked",G_CALLBACK(calibration_clicked),s);
    gtk_box_append(GTK_BOX(side),s->calibration_front);
    s->calibration_back=gtk_button_new_with_label("Calibration: print back");
    gtk_widget_set_visible(s->calibration_back,FALSE);
    gtk_widget_set_sensitive(s->calibration_back,FALSE);
    g_signal_connect(s->calibration_back,"clicked",G_CALLBACK(calibration_clicked),s);
    gtk_box_append(GTK_BOX(side),s->calibration_back);
    auto* export_button=gtk_button_new_with_label("Export PDF");
    gtk_widget_add_css_class(export_button,"suggested-action");
    g_signal_connect(export_button,"clicked",G_CALLBACK(export_clicked),s);
    gtk_box_append(GTK_BOX(side),export_button);
    auto* print_button=gtk_button_new_with_label("Print");
    g_signal_connect(print_button,"clicked",G_CALLBACK(print_clicked),s);
    gtk_box_append(GTK_BOX(side),print_button);
    auto* main=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
    gtk_widget_set_hexpand(main,TRUE);
    gtk_box_append(GTK_BOX(body),main);
    s->preview=gtk_drawing_area_new();
    gtk_widget_set_hexpand(s->preview,TRUE);gtk_widget_set_vexpand(s->preview,TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s->preview),preview_draw,s,nullptr);
    gtk_box_append(GTK_BOX(main),s->preview);
    auto* nav=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_widget_set_halign(nav,GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(nav,10);
    gtk_box_append(GTK_BOX(main),nav);
    auto* prev=gtk_button_new_with_label("Previous");
    g_signal_connect(prev,"clicked",G_CALLBACK(page_back),s);
    gtk_box_append(GTK_BOX(nav),prev);
    s->page_label=label("No document");
    gtk_box_append(GTK_BOX(nav),s->page_label);
    auto* next=gtk_button_new_with_label("Next");
    g_signal_connect(next,"clicked",G_CALLBACK(page_next),s);
    gtk_box_append(GTK_BOX(nav),next);
    s->status=label("Open a PDF to begin");
    gtk_widget_set_margin_start(s->status,16);
    gtk_widget_set_margin_bottom(s->status,8);
    gtk_box_append(GTK_BOX(outer),s->status);
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
