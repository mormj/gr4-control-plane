#include "gr4cp/cli/cli.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string_view> args;
    args.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0U);

    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    return gr4cp::cli::run(args, std::cout, std::cerr);
}
