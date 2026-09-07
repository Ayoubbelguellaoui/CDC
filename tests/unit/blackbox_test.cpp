#include "cdc/blackbox.h"

#include <gtest/gtest.h>

using namespace opencdc::cdc;

TEST(BlackBoxTest, BuiltInModelsLoaded) {
    BlackBoxRegistry registry;
    EXPECT_FALSE(registry.models().empty());
    EXPECT_GE(registry.models().size(), 5u);
}

TEST(BlackBoxTest, XilinxModelsPresent) {
    BlackBoxRegistry registry;
    EXPECT_TRUE(registry.is_black_box("xpm_cdc_array_single"));
    EXPECT_TRUE(registry.is_black_box("xpm_cdc_gray"));
    EXPECT_TRUE(registry.is_black_box("xpm_cdc_handshake"));
    EXPECT_TRUE(registry.is_black_box("xpm_cdc_async_rst"));
    EXPECT_TRUE(registry.is_black_box("xpm_fifo_async"));
}

TEST(BlackBoxTest, IntelModelsPresent) {
    BlackBoxRegistry registry;
    EXPECT_TRUE(registry.is_black_box("alt_cdc_single"));
    EXPECT_TRUE(registry.is_black_box("alt_cdc_bus"));
    EXPECT_TRUE(registry.is_black_box("dcfifo"));
    EXPECT_TRUE(registry.is_black_box("dcfifo_mixed_widths"));
}

TEST(BlackBoxTest, ARMModelsPresent) {
    BlackBoxRegistry registry;
    EXPECT_TRUE(registry.is_black_box("ARM_CDC_SYNC"));
}

TEST(BlackBoxTest, UnknownModuleNotBlackBox) {
    BlackBoxRegistry registry;
    EXPECT_FALSE(registry.is_black_box("my_custom_module"));
    EXPECT_FALSE(registry.is_black_box(""));
}

TEST(BlackBoxTest, FindReturnsModel) {
    BlackBoxRegistry registry;
    auto* model = registry.find("xpm_cdc_gray");
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->module_name, "xpm_cdc_gray");
    EXPECT_EQ(model->vendor, "xilinx");
    EXPECT_TRUE(model->properties.has_gray_encoding);
    EXPECT_TRUE(model->properties.is_safe_crossing);
}

TEST(BlackBoxTest, FindUnknownReturnsNull) {
    BlackBoxRegistry registry;
    EXPECT_EQ(registry.find("unknown"), nullptr);
}

TEST(BlackBoxTest, CustomModelAdded) {
    BlackBoxRegistry registry;
    BlackBoxModel custom;
    custom.module_name = "my_cdc_sync";
    custom.vendor = "custom";
    custom.properties.has_synchronizer = true;
    custom.properties.is_safe_crossing = true;
    registry.add_model(custom);

    EXPECT_TRUE(registry.is_black_box("my_cdc_sync"));
    auto* model = registry.find("my_cdc_sync");
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->vendor, "custom");
}
