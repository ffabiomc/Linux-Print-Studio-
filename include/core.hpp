#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstddef>

namespace lps {
using Slot = std::optional<size_t>;
using Side = std::vector<Slot>;
std::vector<Side> n_up(size_t pages, size_t per_sheet);
std::vector<Side> booklet(size_t pages);
std::vector<Side> poster(size_t pages, size_t columns, size_t rows);
struct DuplexPlan { std::vector<size_t> fronts, backs; };
DuplexPlan manual_duplex(size_t sides, bool reverse_backs);
std::vector<std::string> printers();
std::string default_printer();
int page_count(const std::string& path);
void export_pdf(const std::string& input, const std::string& output,
                const std::vector<Side>& sides, double paper_width=595.0, double paper_height=842.0);
int print_pdf(const std::string& path, const std::string& printer, int copies, bool duplex);
}
