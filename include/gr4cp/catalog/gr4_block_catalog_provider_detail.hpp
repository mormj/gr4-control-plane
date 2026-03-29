#pragma once

#include <string>
#include <string_view>

#include <gnuradio-4.0/Tag.hpp>

namespace gr4cp::catalog::detail {

std::string derive_category_from_block_id(std::string_view id);
std::string derive_category_from_metadata(const gr::property_map& meta, std::string_view fallback_id);

}  // namespace gr4cp::catalog::detail
