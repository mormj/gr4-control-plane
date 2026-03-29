#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace gr4cp::cli {

int run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err);

}  // namespace gr4cp::cli
