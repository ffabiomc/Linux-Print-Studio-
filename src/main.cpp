#include "core.hpp"
#include <adwaita.h>
#include <gtk/gtk.h>
#include <poppler.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct State {
    GtkWidget *window=nullptr,*preview=nullptr,*status=nullptr,*page_label=nullptr;
    GtkWidget *layout=nullptr,*orientation=nullptr,*paper=nullptr,*copies=nullptr,*duplex=nullptr,*printer=nullptr;
    PopplerDocument* doc=nullptr;
    std::string path;
    std::vector<std::string> printer_names;
    int page=0;
    ~State(){if(doc)g_object_unref(doc);}
};
static void status(State* s,const std::string& message) { gtk_label_set_text(GTK_LABEL(s->status),message.c_str()); }
static std::vector<lps::Side> layout_sides(State* s);
static std::pair<double,double> output_size(State* s);
static void preview_draw(GtkDrawingArea*,cairo_t* cr,int width,int height,gpointer data) {
    auto* s=static_cast<State*>(data);
    cairo_set_source_rgb(cr,.88,.89,.91);
    cairo_paint(cr);
    if(!s->doc || width<=40 || height<=40) return;
    const auto sides=layout_sides(s);
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
    const double cellw=pw/cols, cellh=ph/rows;
    for(size_t j=0;j<count;j++) {
        if(!side[j]) continue;
        const size_t page_index=*side[j]-1;
        if(page_index>=static_cast<size_t>(poppler_document_get_n_pages(s->doc))) continue;
        PopplerPage* page=poppler_document_get_page(s->doc,static_cast<int>(page_index));
        if(!page) continue;
        double w=0,h=0;
        poppler_page_get_size(page,&w,&h);
        if(w>0 && h>0) {
            const double scale=std::min(cellw/w,cellh/h);
            const double cx=(j%cols)*cellw, cy=(j/cols)*cellh;
            cairo_save(cr);
            cairo_rectangle(cr,cx,cy,cellw,cellh);
            cairo_clip(cr);
            cairo_translate(cr,cx+(cellw-w*scale)/2,cy+(cellh-h*scale)/2);
            cairo_scale(cr,scale,scale);
            poppler_page_render_for_printing(page,cr);
            cairo_restore(cr);
        }
        g_object_unref(page);
    }
    cairo_restore(cr);
}
static void update_page(State* s) {
    if(!s->doc) return;
    const auto sides=layout_sides(s);
    const int count=static_cast<int>(sides.size());
    s->page=std::clamp(s->page,0,std::max(0,count-1));
    gtk_label_set_text(GTK_LABEL(s->page_label),
        ("Sheet "+std::to_string(s->page+1)+" / "+std::to_string(count)).c_str());
    gtk_widget_queue_draw(s->preview);
}
static void layout_changed(GObject*,GParamSpec*,gpointer data) {
    auto* s=static_cast<State*>(data);
    s->page=0;
    update_page(s);
}
static void load_pdf(State* s,const char* path) {
    GError* err=nullptr;
    gchar* uri=g_filename_to_uri(path,nullptr,&err);
    if(!uri) {status(s,err?err->message:"Invalid path");g_clear_error(&err);return;}
    PopplerDocument* doc=poppler_document_new_from_file(uri,nullptr,&err);
    g_free(uri);
    if(!doc) {status(s,err?err->message:"Could not open PDF");g_clear_error(&err);return;}
    if(s->doc)g_object_unref(s->doc);
    s->doc=doc;s->path=path;s->page=0;
    status(s,std::filesystem::path(path).filename().string());
    update_page(s);
}
static void chooser_response(GtkNativeDialog* dialog,int response,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(response==GTK_RESPONSE_ACCEPT) {
        GFile* file=gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if(file) {
            char* path=g_file_get_path(file);
            if(path) {load_pdf(s,path);g_free(path);}
            g_object_unref(file);
        }
    }
    g_object_unref(dialog);
}
static void open_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    auto* chooser=gtk_file_chooser_native_new("Open PDF",GTK_WINDOW(s->window),GTK_FILE_CHOOSER_ACTION_OPEN,"Open","Cancel");
    GtkFileFilter* filter=gtk_file_filter_new();
    gtk_file_filter_set_name(filter,"PDF documents");
    gtk_file_filter_add_mime_type(filter,"application/pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser),filter);
    g_signal_connect(chooser,"response",G_CALLBACK(chooser_response),s);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}
static void page_back(GtkButton*,gpointer data){auto* s=static_cast<State*>(data);s->page--;update_page(s);}
static void page_next(GtkButton*,gpointer data){auto* s=static_cast<State*>(data);s->page++;update_page(s);}
static std::vector<lps::Side> layout_sides(State* s) {
    size_t pages=static_cast<size_t>(poppler_document_get_n_pages(s->doc));
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
static void print_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->doc){status(s,"Open a PDF first");return;}
    auto selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->printer));
    if(selected>=s->printer_names.size()){status(s,"Select a printer");return;}
    try {
        auto target=std::filesystem::temp_directory_path()/("linux-print-studio-"+std::to_string(g_get_real_time())+".pdf");
        auto [w,h]=output_size(s);
        lps::export_pdf(s->path,target.string(),layout_sides(s),w,h);
        int id=lps::print_pdf(target.string(),s->printer_names[selected],
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->copies)),
            gtk_check_button_get_active(GTK_CHECK_BUTTON(s->duplex)));
        status(s,"CUPS job "+std::to_string(id)+" submitted");
        // CUPS may still need the spooled file; keep it until the system cleans /tmp.
    }catch(const std::exception& e){status(s,e.what());}
}
static GtkWidget* label(const char* text){auto* w=gtk_label_new(text);gtk_widget_set_halign(w,GTK_ALIGN_START);return w;}
static void activate(GApplication* application,gpointer) {
    auto* s=new State();
    s->window=adw_application_window_new(GTK_APPLICATION(application));
    gtk_window_set_title(GTK_WINDOW(s->window),"Linux Print Studio");
    gtk_window_set_default_size(GTK_WINDOW(s->window),1080,720);
    g_object_set_data_full(G_OBJECT(s->window),"state",s,[](gpointer p){delete static_cast<State*>(p);});
    auto* outer=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(s->window),outer);
    auto* header=adw_header_bar_new();
    gtk_box_append(GTK_BOX(outer),header);
    auto* open=gtk_button_new_with_label("Open PDF");
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
    const char* layouts[]={"Normal","Booklet","2 pages / sheet","4 pages / sheet","6 pages / sheet","9 pages / sheet","16 pages / sheet",nullptr};
    s->layout=gtk_drop_down_new_from_strings(layouts);
    gtk_box_append(GTK_BOX(side),s->layout);
    gtk_box_append(GTK_BOX(side),label("Orientation"));
    const char* orientations[]={"Automatic","Portrait","Landscape",nullptr};
    s->orientation=gtk_drop_down_new_from_strings(orientations);
    gtk_box_append(GTK_BOX(side),s->orientation);
    gtk_box_append(GTK_BOX(side),label("Paper size"));
    const char* papers[]={"A4","Letter","Legal",nullptr};
    s->paper=gtk_drop_down_new_from_strings(papers);
    gtk_box_append(GTK_BOX(side),s->paper);
    g_signal_connect(s->layout,"notify::selected",G_CALLBACK(layout_changed),s);
    g_signal_connect(s->orientation,"notify::selected",G_CALLBACK(layout_changed),s);
    g_signal_connect(s->paper,"notify::selected",G_CALLBACK(layout_changed),s);
    gtk_box_append(GTK_BOX(side),label("Printer"));
    s->printer_names=lps::printers();
    auto def=lps::default_printer();
    GtkStringList* models=gtk_string_list_new(nullptr);
    unsigned default_index=0;
    for(size_t i=0;i<s->printer_names.size();i++) {
        gtk_string_list_append(models,s->printer_names[i].c_str());
        if(s->printer_names[i]==def)default_index=static_cast<unsigned>(i);
    }
    s->printer=gtk_drop_down_new(G_LIST_MODEL(models),nullptr);
    g_object_unref(models);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(s->printer),s->printer_names.empty()?GTK_INVALID_LIST_POSITION:default_index);
    gtk_box_append(GTK_BOX(side),s->printer);
    gtk_box_append(GTK_BOX(side),label("Copies"));
    s->copies=gtk_spin_button_new_with_range(1,999,1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->copies),1);
    gtk_box_append(GTK_BOX(side),s->copies);
    s->duplex=gtk_check_button_new_with_label("Automatic duplex (long edge)");
    gtk_box_append(GTK_BOX(side),s->duplex);
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
    auto* app=adw_application_new("io.github.ffabiomc.LinuxPrintStudio",G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app,"activate",G_CALLBACK(activate),nullptr);
    int result=g_application_run(G_APPLICATION(app),argc,argv);
    g_object_unref(app);
    return result;
}
