#pragma once

#include <array>

#include "esp_err.h"
#include "input_router.hpp"
#include "lvgl.h"
#include "telemetry_store.hpp"

namespace rmh {

class DashboardUi {
public:
    esp_err_t start();
    void show_page(DashboardPage page, std::uint8_t selected_card,
                   bool animate = true);
    void refresh(const TelemetryStore& telemetry, bool phone_online,
                 bool ble_connected, bool usb_connected);
    [[nodiscard]] bool ready() const {
        return display_ != nullptr && content_ != nullptr &&
               status_label_ != nullptr && page_label_ != nullptr;
    }

private:
    void build_shell();
    void build_overview();
    void build_detail();
    lv_obj_t* create_card(int x, int y, const char* title, bool selected,
                          std::size_t index);
    void update_overview(const TelemetryStore& telemetry, bool phone_online,
                         bool ble_connected, bool usb_connected);
    void update_detail(const TelemetryStore& telemetry, bool phone_online,
                       bool ble_connected, bool usb_connected);
    void apply_slide();

    lv_display_t* display_{nullptr};
    lv_obj_t* content_{nullptr};
    lv_obj_t* status_label_{nullptr};
    lv_obj_t* page_label_{nullptr};
    std::array<lv_obj_t*, 6> value_labels_{};
    std::array<lv_obj_t*, 6> subtitle_labels_{};
    lv_obj_t* detail_value_{nullptr};
    lv_obj_t* detail_note_{nullptr};
    lv_obj_t* chart_{nullptr};
    lv_chart_series_t* chart_primary_{nullptr};
    lv_chart_series_t* chart_secondary_{nullptr};
    DashboardPage page_{DashboardPage::Overview};
    std::uint8_t selected_card_{0};
    int slide_direction_{1};
};

}  // namespace rmh
