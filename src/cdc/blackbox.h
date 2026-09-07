#ifndef OPENCDC_CDC_BLACKBOX_H
#define OPENCDC_CDC_BLACKBOX_H

#include <string>
#include <vector>

namespace opencdc::cdc {

struct BlackBoxProperty {
    bool has_synchronizer = false;
    bool has_gray_encoding = false;
    bool has_async_fifo = false;
    bool has_handshake = false;
    bool is_safe_crossing = false;
    std::string description;
};

struct BlackBoxModel {
    std::string module_name;
    std::string vendor;
    BlackBoxProperty properties;
};

class BlackBoxRegistry {
   public:
    BlackBoxRegistry();

    void add_model(const BlackBoxModel& model);
    bool is_black_box(const std::string& module_name) const;
    const BlackBoxModel* find(const std::string& module_name) const;

    const std::vector<BlackBoxModel>& models() const {
        return models_;
    }

   private:
    std::vector<BlackBoxModel> models_;
};

}  // namespace opencdc::cdc

#endif  // OPENCDC_CDC_BLACKBOX_H
