#ifndef __UI_EXTERNAL_APP_SCANNER_H__
#define __UI_EXTERNAL_APP_SCANNER_H__

#include "ui.hpp"
#include "ui_widget.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "ui_freq_field.hpp"
#include "ui_textentry.hpp"
#include "ui_menu.hpp"
#include "message.hpp"
#include "receiver_model.hpp"
#include <vector>
#include <string>

namespace ui::external_app::ext_scanner {

struct FrequencyRange {
    rf::Frequency start;
    rf::Frequency end;
    std::string name;
    bool enabled;
    
    FrequencyRange(rf::Frequency s, rf::Frequency e, std::string n = "", bool en = true)
        : start(s), end(e), name(n), enabled(en) {}
};

class RangeEditorView : public View {
public:
    RangeEditorView(NavigationView& nav, std::vector<FrequencyRange>& ranges, size_t index, bool is_new);
    
    RangeEditorView(const RangeEditorView&) = delete;
    RangeEditorView& operator=(const RangeEditorView&) = delete;
    
    void focus() override;
    std::string title() const override { return is_new_ ? "Add Range" : "Edit Range"; }

private:
    NavigationView& nav_;
    std::vector<FrequencyRange>& ranges_;
    size_t range_index_;
    bool is_new_;
    std::string temp_name;
    
    Labels labels {
        {{ 1*8, 2*16 }, "Name:", Color::light_grey()},
        {{ 1*8, 6*16 }, "Start:", Color::light_grey()},
        {{ 1*8, 8*16 }, "End:", Color::light_grey()}
    };
    
    Button button_edit_name {{ 1*8, 3*16, 20*8, 2*16 }, ""};
    FrequencyField field_start {{ 8*8, 6*16 }};
    FrequencyField field_end {{ 8*8, 8*16 }};
    
    Checkbox checkbox_enabled {{ 1*8, 10*16 }, 8, "Enabled"};
    Button button_save {{ 1*8, 12*16, 8*8, 2*16 }, "Save"};
    Button button_delete {{ 11*8, 12*16, 8*8, 2*16 }, "Delete"};
    Button button_cancel {{ 21*8, 12*16, 8*8, 2*16 }, "Cancel"};
    
    void save_range();
    void update_name_button();
};

class RangeManagerView : public View {
public:
    RangeManagerView(NavigationView& nav, std::vector<FrequencyRange>& ranges);
    
    RangeManagerView(const RangeManagerView&) = delete;
    RangeManagerView& operator=(const RangeManagerView&) = delete;
    
    void focus() override;
    void on_show() override;
    std::string title() const override { return "Manage Ranges"; }

private:
    NavigationView& nav_;
    std::vector<FrequencyRange>& ranges_;
    size_t selected_index = 0;
    
    Labels labels {{{ 1*8, 1*16 }, "Scan Ranges:", Color::light_grey()}};
    MenuView menu_view {{ 0, 2*16, 240, 12*16 }};
    Button button_add {{ 1*8, 15*16, 13*8, 2*16 }, "Add New"};
    Button button_back {{ 16*8, 15*16, 13*8, 2*16 }, "Back"};
    
    void refresh_list();
    void add_range();
    void edit_range(size_t index);
};

class ScannerAppView : public View {
public:
    ScannerAppView(NavigationView& nav);
    ~ScannerAppView();
    
    ScannerAppView(const ScannerAppView&) = delete;
    ScannerAppView& operator=(const ScannerAppView&) = delete;
    
    void focus() override;
    void on_show() override;
    std::string title() const override { return "Scanner"; }

private:
    NavigationView& nav_;
    std::vector<FrequencyRange> scan_ranges;
    size_t current_range_index = 0;
    rf::Frequency current_freq = 0;
    int32_t squelch_threshold = -100;  // More reasonable default for RF
    uint32_t min_signal_width_mhz = 4;  // 4 MHz default
    uint32_t max_signal_width_mhz = 8;  // 8 MHz default
    bool is_scanning = false;
    bool is_paused = false;
    
    // Spectrum capture settings
    static constexpr uint32_t SPECTRUM_SLICE_WIDTH = 20000000;  // 20 MHz chunks
    static constexpr size_t SPECTRUM_BINS = 256;  // FFT bins per chunk
    
    // Cycle scanning variables
    bool in_scan_cycle = true;
    rf::Frequency best_freq_in_cycle = 0;
    int32_t best_rssi_in_cycle = -999;
    size_t start_range_index = 0;
    rf::Frequency start_freq = 0;
    
    // Current chunk scanning
    rf::Frequency current_chunk_center = 0;
    size_t current_chunk_in_range = 0;
    size_t total_chunks_in_range = 0;
    
    // Signal tracking from spectrum data
    rf::Frequency widest_signal_width = 0;
    rf::Frequency widest_signal_freq = 0;
    int32_t widest_signal_rssi = -999;
    uint32_t dome_signals_count = 0;       // Signals with FM dome shape (FPV drones)
    bool threat_detected = false;          // True when FPV drone found - triggers stop at cycle end
    
    // Cross-chunk signal tracking (for signals spanning multiple 20 MHz chunks)
    bool signal_at_chunk_end = false;      // Signal continues to end of previous chunk
    size_t chunk_end_signal_start_bin = 0; // Where it started in previous chunk
    uint8_t chunk_end_peak_power = 0;      // Peak power in previous chunk portion
    rf::Frequency pending_signal_freq = 0; // Center freq of pending cross-chunk signal
    
    // Spectrum data FIFO
    ChannelSpectrumFIFO* fifo{nullptr};
    
    Labels labels {
        {{ 0*8, 1*16 }, "Ranges:", Color::light_grey()},
        {{ 0*8, 4*16 }, "Threshold:", Color::light_grey()},
        {{ 0*8, 6*16 }, "BW Min:", Color::light_grey()},
        {{ 0*8, 7*16 }, "BW Max:", Color::light_grey()},
        {{ 0*8, 8*16 }, "Mode:", Color::light_grey()}
    };
    
    Text text_range_count {{ 9*8, 1*16, 20*8, 16 }, "0 ranges"};
    Button button_manage_ranges {{ 1*8, 2*16, 15*8, 2*16 }, "Manage Ranges"};
    NumberField field_threshold {{ 12*8, 4*16 }, 4, {-120, -20}, 1, ' '};
    Text text_threshold_unit {{ 17*8, 4*16, 3*8, 16 }, "dBm"};
    NumberField field_bw_min {{ 9*8, 6*16 }, 3, {1, 100}, 1, ' '};
    Text text_bw_min_unit {{ 12*8, 6*16, 3*8, 16 }, "MHz"};
    NumberField field_bw_max {{ 9*8, 7*16 }, 3, {1, 100}, 1, ' '};
    Text text_bw_max_unit {{ 12*8, 7*16, 3*8, 16 }, "MHz"};
    Text text_mode {{ 6*8, 8*16, 23*8, 16 }, "Spectrum (20MHz)"};

    Button button_scan_start {{ 1*8, 10*16, 8*8, 16 }, "START"};
    Button button_pause_resume {{ 10*8, 10*16, 8*8, 16 }, "PAUSE"};
    Button button_scan_stop {{ 19*8, 10*16, 8*8, 16 }, "STOP"};
    Text text_current_range {{ 0*8, 11*16, 30*8, 16 }, "Range: ---"};
    Text text_current_freq {{ 0*8, 12*16, 30*8, 16 }, "Chunk: ---"};
    Text text_rssi {{ 0*8, 13*16, 30*8, 16 }, "RSSI: ---"};
    Text text_widest {{ 0*8, 14*16, 30*8, 16 }, "Widest: ---"};
    Text text_dome_signals {{ 0*8, 15*16, 30*8, 16 }, "FPV Threats: 0"};
    Text text_status {{ 0*8, 16*16, 30*8, 16 }, "Status: Idle"};
    
    MessageHandlerRegistration message_handler_spectrum_config {
        Message::ID::ChannelSpectrumConfig,
        [this](const Message* const p) {
            const auto message = *reinterpret_cast<const ChannelSpectrumConfigMessage*>(p);
            this->fifo = message.fifo;
        }
    };
    
    MessageHandlerRegistration message_handler_frame_sync {
        Message::ID::DisplayFrameSync,
        [this](const Message* const) {
            if (this->fifo) {
                ChannelSpectrum channel_spectrum;
                while (fifo->out(channel_spectrum)) {
                    this->on_channel_spectrum(channel_spectrum);
                }
            }
        }
    };
    void on_channel_spectrum(const ChannelSpectrum& spectrum);
    bool analyze_fm_dome_shape(const ChannelSpectrum& spectrum, size_t start_bin, size_t end_bin);
    void start_scanning();
    void pause_scanning();
    void resume_scanning();
    void stop_scanning();
    void scan_next_chunk();
    void move_to_next_range();
    void tune_to_chunk_center(rf::Frequency center_freq);
    void process_spectrum_bins(const ChannelSpectrum& spectrum);
    void on_signal_found(rf::Frequency freq, int32_t rssi, rf::Frequency width);
    void update_display();
    void update_range_count();
    void calculate_chunk_count();
    void load_default_ranges();
    void play_alert_tone();
    void stop_alert_tone();
};

void initialize_app(ui::NavigationView& nav);

} // namespace ui::external_app::ext_scanner

#endif