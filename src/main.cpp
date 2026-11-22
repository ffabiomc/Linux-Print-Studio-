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
    GtkWidget *layout=nullptr,*copies=nullptr,*duplex=nullptr,*printer=nullptr;
    PopplerDocument* doc=nullptr;
    std::string path;
    std::vector<std::string> printer_names;
    int page=0;
    ~State(){if(doc)g_object_unref(doc);}
};
static void status(State* s,const std::string& message) { gtk_label_set_text(GTK_LABEL(s->status),message.c_str()); }
static void preview_draw(GtkDrawingArea*,cairo_t* cr,int width,int height,gpointer data) {
    auto* s=static_cast<State*>(data);
    cairo_set_source_rgb(cr,.88,.89,.91);cairo_paint(cr);
    if(!s->doc) return;
    PopplerPage* page=poppler_document_get_page(s->doc,s->page);
    if(!page)return;
    double pw=0,ph=0;poppler_page_get_size(page,&pw,&ph);
    if(pw>0&&ph>0) {
        double scale=std::min((width-40.0)/pw,(height-40.0)/ph);
        scale=std::max(scale,.01);
        double x=(width-pw*scale)/2,y=(height-ph*scale)/2;
        cairo_set_source_rgb(cr,1,1,1);cairo_rectangle(cr,x,y,pw*scale,ph*scale);cairo_fill(cr);
        cairo_save(cr);cairo_translate(cr,x,y);cairo_scale(cr,scale,scale);
        poppler_page_render(page,cr);cairo_restore(cr);
    }
    g_object_unref(page);
}
static void update_page(State* s) {
    if(!s->doc)return;
    int count=poppler_document_get_n_pages(s->doc);
    s->page=std::clamp(s->page,0,std::max(0,count-1));
    gtk_label_set_text(GTK_LABEL(s->page_label),("Page "+std::to_string(s->page+1)+" / "+std::to_string(count)).c_str());
    gtk_widget_queue_draw(s->preview);
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
    if(selected>=2)return lps::n_up(pages,selected==2?2:selected==3?4:9);
    return lps::n_up(pages,1);
}
static void export_clicked(GtkButton*,gpointer data) {
    auto* s=static_cast<State*>(data);
    if(!s->doc){status(s,"Open a PDF first");return;}
    try {
        auto target=std::filesystem::path(s->path);
        target.replace_filename(target.stem().string()+"-print-studio.pdf");
        lps::export_pdf(s->path,target.string(),layout_sides(s));
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
        lps::export_pdf(s->path,target.string(),layout_sides(s));
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
    s->window=adw_application_window_new(ADW_APPLICATION(application));
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
    const char* layouts[]={"Normal","Booklet","2 pages / sheet","4 pages / sheet","9 pages / sheet",nullptr};
    s->layout=gtk_drop_down_new_from_strings(layouts);
    gtk_box_append(GTK_BOX(side),s->layout);
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
