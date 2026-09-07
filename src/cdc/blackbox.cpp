#include "cdc/blackbox.h"

namespace opencdc::cdc {

BlackBoxRegistry::BlackBoxRegistry() {
    // Xilinx XPM CDC primitives
    BlackBoxModel xpm_cdc_array_single;
    xpm_cdc_array_single.module_name = "xpm_cdc_array_single";
    xpm_cdc_array_single.vendor = "xilinx";
    xpm_cdc_array_single.properties.has_synchronizer = true;
    xpm_cdc_array_single.properties.is_safe_crossing = true;
    xpm_cdc_array_single.properties.description = "Xilinx XPM: single-array CDC synchronizer";
    models_.push_back(xpm_cdc_array_single);

    BlackBoxModel xpm_cdc_gray;
    xpm_cdc_gray.module_name = "xpm_cdc_gray";
    xpm_cdc_gray.vendor = "xilinx";
    xpm_cdc_gray.properties.has_gray_encoding = true;
    xpm_cdc_gray.properties.has_synchronizer = true;
    xpm_cdc_gray.properties.is_safe_crossing = true;
    xpm_cdc_gray.properties.description = "Xilinx XPM: gray-coded CDC";
    models_.push_back(xpm_cdc_gray);

    BlackBoxModel xpm_cdc_handshake;
    xpm_cdc_handshake.module_name = "xpm_cdc_handshake";
    xpm_cdc_handshake.vendor = "xilinx";
    xpm_cdc_handshake.properties.has_handshake = true;
    xpm_cdc_handshake.properties.has_synchronizer = true;
    xpm_cdc_handshake.properties.is_safe_crossing = true;
    xpm_cdc_handshake.properties.description = "Xilinx XPM: handshake CDC";
    models_.push_back(xpm_cdc_handshake);

    BlackBoxModel xpm_cdc_async_rst;
    xpm_cdc_async_rst.module_name = "xpm_cdc_async_rst";
    xpm_cdc_async_rst.vendor = "xilinx";
    xpm_cdc_async_rst.properties.has_synchronizer = true;
    xpm_cdc_async_rst.properties.is_safe_crossing = true;
    xpm_cdc_async_rst.properties.description = "Xilinx XPM: async reset synchronizer";
    models_.push_back(xpm_cdc_async_rst);

    BlackBoxModel xpm_fifo_async;
    xpm_fifo_async.module_name = "xpm_fifo_async";
    xpm_fifo_async.vendor = "xilinx";
    xpm_fifo_async.properties.has_async_fifo = true;
    xpm_fifo_async.properties.has_gray_encoding = true;
    xpm_fifo_async.properties.has_synchronizer = true;
    xpm_fifo_async.properties.is_safe_crossing = true;
    xpm_fifo_async.properties.description = "Xilinx XPM: async FIFO with gray-coded pointers";
    models_.push_back(xpm_fifo_async);

    // Intel/Altera CDC primitives
    BlackBoxModel alt_cdc_single;
    alt_cdc_single.module_name = "alt_cdc_single";
    alt_cdc_single.vendor = "intel";
    alt_cdc_single.properties.has_synchronizer = true;
    alt_cdc_single.properties.is_safe_crossing = true;
    alt_cdc_single.properties.description = "Intel CDC: single-bit synchronizer";
    models_.push_back(alt_cdc_single);

    BlackBoxModel alt_cdc_bus;
    alt_cdc_bus.module_name = "alt_cdc_bus";
    alt_cdc_bus.vendor = "intel";
    alt_cdc_bus.properties.has_handshake = true;
    alt_cdc_bus.properties.has_synchronizer = true;
    alt_cdc_bus.properties.is_safe_crossing = true;
    alt_cdc_bus.properties.description = "Intel CDC: bus synchronizer with handshake";
    models_.push_back(alt_cdc_bus);

    BlackBoxModel dcfifo;
    dcfifo.module_name = "dcfifo";
    dcfifo.vendor = "intel";
    dcfifo.properties.has_async_fifo = true;
    dcfifo.properties.has_gray_encoding = true;
    dcfifo.properties.has_synchronizer = true;
    dcfifo.properties.is_safe_crossing = true;
    dcfifo.properties.description = "Intel: dual-clock FIFO";
    models_.push_back(dcfifo);

    BlackBoxModel dcfifo_mixed_widths;
    dcfifo_mixed_widths.module_name = "dcfifo_mixed_widths";
    dcfifo_mixed_widths.vendor = "intel";
    dcfifo_mixed_widths.properties.has_async_fifo = true;
    dcfifo_mixed_widths.properties.has_gray_encoding = true;
    dcfifo_mixed_widths.properties.has_synchronizer = true;
    dcfifo_mixed_widths.properties.is_safe_crossing = true;
    dcfifo_mixed_widths.properties.description = "Intel: dual-clock FIFO with mixed widths";
    models_.push_back(dcfifo_mixed_widths);

    // ARM CDC cells
    BlackBoxModel arm_cdc_sync;
    arm_cdc_sync.module_name = "ARM_CDC_SYNC";
    arm_cdc_sync.vendor = "arm";
    arm_cdc_sync.properties.has_synchronizer = true;
    arm_cdc_sync.properties.is_safe_crossing = true;
    arm_cdc_sync.properties.description = "ARM: CDC synchronizer";
    models_.push_back(arm_cdc_sync);
}

void BlackBoxRegistry::add_model(const BlackBoxModel& model) {
    models_.push_back(model);
}

bool BlackBoxRegistry::is_black_box(const std::string& module_name) const {
    return find(module_name) != nullptr;
}

const BlackBoxModel* BlackBoxRegistry::find(const std::string& module_name) const {
    for (const auto& m : models_) {
        if (m.module_name == module_name)
            return &m;
    }
    return nullptr;
}

}  // namespace opencdc::cdc
