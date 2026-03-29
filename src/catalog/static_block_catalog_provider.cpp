#include "gr4cp/catalog/static_block_catalog_provider.hpp"

namespace gr4cp::catalog {

std::vector<domain::BlockDescriptor> StaticBlockCatalogProvider::list() const {
    return {
        {
            .id = "blocks.analog.wfm_rcv",
            .name = "WFM Receive",
            .category = "Analog",
            .summary = "Demodulates a wideband FM complex input stream",
            .inputs = {{"in", "complex"}},
            .outputs = {{"out", "float"}},
            .parameters =
                {
                    {"quad_rate", "float", true, nullptr, "Quadrature input sample rate"},
                    {"audio_decimation", "int", false, 10, "Audio decimation factor"},
                },
        },
        {
            .id = "blocks.fft.fft_vcc",
            .name = "FFT",
            .category = "FFT",
            .summary = "Computes a complex FFT over vector input frames",
            .inputs = {{"in", "complex_vector"}},
            .outputs = {{"out", "complex_vector"}},
            .parameters =
                {
                    {"fft_size", "int", true, nullptr, "Transform size"},
                    {"forward", "bool", false, true, "Use forward transform when true"},
                },
        },
        {
            .id = "blocks.filters.fir_filter_ccf",
            .name = "FIR Filter",
            .category = "Filters",
            .summary = "Applies float taps to a complex input stream",
            .inputs = {{"in", "complex"}},
            .outputs = {{"out", "complex"}},
            .parameters =
                {
                    {"decimation", "int", false, 1, "Optional decimation factor"},
                    {"taps", "float_vector", true, nullptr, "Filter taps"},
                },
        },
        {
            .id = "blocks.math.add_ff",
            .name = "Add",
            .category = "Math",
            .summary = "Adds two float streams",
            .inputs = {{"in0", "float"}, {"in1", "float"}},
            .outputs = {{"out", "float"}},
            .parameters =
                {
                    {"scale", "float", false, 1.0, "Optional output scaling"},
                },
        },
        {
            .id = "blocks.sinks.null_sink",
            .name = "Null Sink",
            .category = "Sinks",
            .summary = "Consumes input samples and discards them",
            .inputs = {{"in", "any"}},
            .outputs = {},
            .parameters =
                {
                    {"type", "string", true, nullptr, "Accepted input sample type"},
                },
        },
        {
            .id = "blocks.sources.signal_source_f",
            .name = "Signal Source",
            .category = "Sources",
            .summary = "Generates a float waveform source",
            .inputs = {},
            .outputs = {{"out", "float"}},
            .parameters =
                {
                    {"sample_rate", "float", true, nullptr, "Output sample rate"},
                    {"waveform", "string", false, std::string("sine"), "Waveform shape"},
                    {"frequency", "float", false, 1000.0, "Signal frequency in Hz"},
                },
        },
    };
}

}  // namespace gr4cp::catalog
