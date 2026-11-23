#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstddef>

namespace lps {
using Slot = std::optional<size_t>;
using Side = std::vector<Slot>;
std::vector<size_t> parse_page_range(const std::string& expression, size_t total_pages);
std::vector<Side> remap_pages(const std::vector<Side>& sides, const std::vector<size_t>& pages);
std::vector<Side> n_up(size_t pages, size_t per_sheet);
std::vector<Side> booklet(size_t pages);
std::vector<Side> poster(size_t pages, size_t columns, size_t rows);
struct DuplexPlan { std::vector<size_t> fronts, backs; };
DuplexPlan manual_duplex(size_t sides, bool reverse_backs);
struct PrinterInfo { std::string name, description; };
std::vector<PrinterInfo> printer_details();
std::vector<std::string> printers();
std::string default_printer();
int page_count(const std::string& path);
struct RenderOptions {
    double scale_percent=100.0;
    double margin_mm=0.0;
    bool centered=true;
    bool grayscale=false;
};
void validate_render_options(const RenderOptions& options,double paper_width,double paper_height);
void export_pdf(const std::string& input, const std::string& output,
                const std::vector<Side>& sides, double paper_width=595.0, double paper_height=842.0, const RenderOptions& options={});
enum class PrintSides { Simplex, LongEdge, ShortEdge };
int print_pdf(const std::string& path, const std::string& printer, int copies, PrintSides sides);
std::vector<Side> select_sides(const std::vector<Side>& sides, const std::vector<size_t>& indices);
}
