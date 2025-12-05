#include "ui_external_app_scanner.hpp"
#include "portapack.hpp"
#include "baseband_api.hpp"
#include "radio.hpp"
#include "audio.hpp"
#include "utility.hpp"
#include "string_format.hpp"
#include "ui_textentry.hpp"
#include "spi_image.hpp"
#include "external_app.hpp"
#include <cmath>
#include <algorithm>

using namespace portapack;

namespace ui::external_app::ext_scanner {

// ==========================================
// RangeEditorView Implementation
// (Keep existing implementation - unchanged)
// ==========================================

RangeEditorView::RangeEditorView(NavigationView& nav, std::vector<FrequencyRange>& ranges, size_t index, bool is_new)
    : nav_(nav), ranges_(ranges), range_index_(index), is_new_(is_new), temp_name("")
{
    // Set frequency field steps to 1 MHz for faster adjustment
    field_start.set_step(1000000);  // 1 MHz
    field_end.set_step(1000000);    // 1 MHz
    
    add_children({
        &labels,
        &button_edit_name,
        &field_start,
        &field_end,
        &checkbox_enabled,
        &button_save,
        &button_delete,
        &button_cancel
    });
    
    // Hide delete button for new ranges
    if (is_new_) {
        button_delete.hidden(true);
    }
    
    if (!is_new_ && range_index_ < ranges_.size()) {
        auto& range = ranges_[range_index_];
        temp_name = range.name;
        field_start.set_value(range.start);
        field_end.set_value(range.end);
        checkbox_enabled.set_value(range.enabled);
    } else {
        temp_name = "New Range";
        field_start.set_value(100000000);
        field_end.set_value(200000000);
        checkbox_enabled.set_value(true);
    }
    
    update_name_button();
    
    button_edit_name.on_select = [this](Button&) {
        text_prompt(nav_, temp_name, 20, ENTER_KEYBOARD_MODE_ALPHA,
            [this](std::string& s) {
                temp_name = s;
                update_name_button();
            }
        );
    };
    
    button_save.on_select = [this](Button&) {
        save_range();
    };
    
    button_delete.on_select = [this](Button&) {
        if (range_index_ < ranges_.size()) {
            ranges_.erase(ranges_.begin() + range_index_);
        }
        nav_.pop();
    };
    
    button_cancel.on_select = [this](Button&) {
        // If it's a new range that wasn't saved, remove it
        if (is_new_ && range_index_ < ranges_.size()) {
            ranges_.erase(ranges_.begin() + range_index_);
        }
        nav_.pop();
    };
}

void RangeEditorView::focus() {
    button_edit_name.focus();
}

void RangeEditorView::update_name_button() {
    button_edit_name.set_text(temp_name);
}

void RangeEditorView::save_range() {
    if (range_index_ >= ranges_.size()) return;
    
    auto& range = ranges_[range_index_];
    range.name = temp_name;
    range.start = field_start.value();
    range.end = field_end.value();
    range.enabled = checkbox_enabled.value();
    
    nav_.pop();
}

// ==========================================
// RangeManagerView Implementation
// (Keep existing implementation - unchanged)
// ==========================================

RangeManagerView::RangeManagerView(NavigationView& nav, std::vector<FrequencyRange>& ranges)
    : nav_(nav), ranges_(ranges)
{
    add_children({
        &labels,
        &menu_view,
        &button_add,
        &button_back
    });
    
    button_add.on_select = [this](Button&) {
        add_range();
    };
    
    button_back.on_select = [this](Button&) {
        nav_.pop();
    };
}

void RangeManagerView::focus() {
    button_add.focus();
}

void RangeManagerView::on_show() {
    refresh_list();
}

void RangeManagerView::refresh_list() {
    menu_view.clear();
    
    for (size_t i = 0; i < ranges_.size(); i++) {
        auto& range = ranges_[i];
        std::string item_text = range.name + " " +
            to_string_short_freq(range.start) + "-" +
            to_string_short_freq(range.end) +
            (range.enabled ? " [ON]" : " [OFF]");
        
        menu_view.add_item({
            item_text,
            ui::Color::white(),
            nullptr,
            [this, i](KeyEvent) {
                edit_range(i);
            }
        });
    }
}

void RangeManagerView::add_range() {
    ranges_.push_back(FrequencyRange(100000000, 200000000, "New Range"));
    nav_.push<RangeEditorView>(ranges_, ranges_.size() - 1, true);
}

void RangeManagerView::edit_range(size_t index) {
    if (index < ranges_.size()) {
        nav_.push<RangeEditorView>(ranges_, index, false);
    }
}

// ==========================================
// ScannerAppView Implementation
// ==========================================

ScannerAppView::ScannerAppView(NavigationView& nav)
    : nav_(nav),
      scan_ranges{}
{
    // Load baseband image early (like Looking Glass does)
    baseband::run_image(portapack::spi_flash::image_tag_wideband_spectrum);
    
    add_children({
        &labels,
        &text_range_count,
        &button_manage_ranges,
        &field_threshold,
        &text_threshold_unit,
        &field_bw_min,
        &text_bw_min_unit,
        &field_bw_max,
        &text_bw_max_unit,
        &text_mode,
        &button_scan_start,
        &button_pause_resume,
        &button_scan_stop,
        &text_current_range,
        &text_current_freq,
        &text_rssi,
        &text_widest,
        &text_dome_signals,
        &text_status
    });
    
    // Initially hide PAUSE and STOP buttons
    button_pause_resume.hidden(true);
    button_scan_stop.hidden(true);
}

void ScannerAppView::on_show() {
    // Initialize data structures with default ranges
    if (scan_ranges.empty()) {
        load_default_ranges();
    }
    update_range_count();
    
    // Set initial values
    field_threshold.set_value(squelch_threshold);
    field_bw_min.set_value(min_signal_width_mhz);
    field_bw_max.set_value(max_signal_width_mhz);
    
    // Set up callbacks
    field_threshold.on_change = [this](int32_t v) {
        squelch_threshold = v;
    };
    
    field_bw_min.on_change = [this](int32_t v) {
        min_signal_width_mhz = v;
        if (min_signal_width_mhz > max_signal_width_mhz) {
            max_signal_width_mhz = min_signal_width_mhz;
            field_bw_max.set_value(max_signal_width_mhz);
        }
    };
    
    field_bw_max.on_change = [this](int32_t v) {
        max_signal_width_mhz = v;
        if (max_signal_width_mhz < min_signal_width_mhz) {
            min_signal_width_mhz = max_signal_width_mhz;
            field_bw_min.set_value(min_signal_width_mhz);
        }
    };
    
    button_manage_ranges.on_select = [this](Button&) {
        nav_.push<RangeManagerView>(scan_ranges);
    };
    
    button_scan_start.on_select = [this](Button&) {
        start_scanning();
    };
    
    button_pause_resume.on_select = [this](Button&) {
        if (is_paused) {
            resume_scanning();
        } else {
            pause_scanning();
        }
    };
    
    button_scan_stop.on_select = [this](Button&) {
        stop_scanning();
    };
}

ScannerAppView::~ScannerAppView() {
    stop_scanning();
    audio::output::stop();  // Stop audio in destructor
    receiver_model.disable();
    baseband::shutdown();
}

void ScannerAppView::focus() {
    // Focus on the appropriate button based on current state
    if (is_scanning || is_paused) {
        button_pause_resume.focus();
    } else {
        button_scan_start.focus();
    }
}

void ScannerAppView::load_default_ranges() {
    // Default ranges for drone detection
    scan_ranges.push_back(FrequencyRange(900000000, 1900000000, "900-1900MHz"));
    scan_ranges.push_back(FrequencyRange(2900000000, 4000000000, "2.9-4.0GHz"));
    scan_ranges.push_back(FrequencyRange(4900000000, 6000000000, "4.9-6.0GHz"));
}

void ScannerAppView::update_range_count() {
    text_range_count.set(to_string_dec_uint(scan_ranges.size()) + " ranges");
}

void ScannerAppView::calculate_chunk_count() {
    if (current_range_index >= scan_ranges.size()) {
        total_chunks_in_range = 0;
        return;
    }
    
    auto& range = scan_ranges[current_range_index];
    rf::Frequency range_width = range.end - range.start;
    total_chunks_in_range = (range_width + SPECTRUM_SLICE_WIDTH - 1) / SPECTRUM_SLICE_WIDTH;
}

void ScannerAppView::start_scanning() {
    if (is_scanning) return;
    
    if (scan_ranges.empty()) {
        text_status.set("ERROR: No ranges");
        return;
    }
    
    // Check if at least one range is enabled
    bool has_enabled = false;
    for (auto& range : scan_ranges) {
        if (range.enabled) {
            has_enabled = true;
            break;
        }
    }
    
    if (!has_enabled) {
        text_status.set("ERROR: No enabled");
        return;
    }
    
    is_scanning = true;
    
    // Find first enabled range
    current_range_index = 0;
    while (current_range_index < scan_ranges.size() && !scan_ranges[current_range_index].enabled) {
        current_range_index++;
    }
    
    if (current_range_index >= scan_ranges.size()) {
        text_status.set("ERROR: No enabled");
        is_scanning = false;
        return;
    }
    
    // Continuous scanning mode (never stop, just loop through ranges)
    in_scan_cycle = false;  // Disable cycle locking behavior
    best_freq_in_cycle = 0;
    best_rssi_in_cycle = -999;
    start_range_index = current_range_index;
    
    // Initialize signal tracking
    widest_signal_width = 0;
    widest_signal_freq = 0;
    widest_signal_rssi = -999;
    dome_signals_count = 0;
    threat_detected = false;
    signal_at_chunk_end = false;
    text_widest.set("Widest: ---");
    text_dome_signals.set("FPV Threats: 0");
    
    // Calculate chunk info for current range
    calculate_chunk_count();
    current_chunk_in_range = 0;
    
    // Calculate first chunk center
    auto& first_range = scan_ranges[current_range_index];
    current_chunk_center = first_range.start + (SPECTRUM_SLICE_WIDTH / 2);
    
    // Update button visibility
    button_scan_start.hidden(true);
    button_scan_start.set_focusable(false);
    
    button_pause_resume.hidden(false);
    button_pause_resume.set_focusable(true);
    button_pause_resume.set_text("PAUSE");
    
    button_scan_stop.hidden(false);
    button_scan_stop.set_focusable(true);
    
    set_dirty();
    text_status.set("Status: Scanning");
    
    // Configure receiver and spectrum capture
    receiver_model.set_sampling_rate(SPECTRUM_SLICE_WIDTH);
    receiver_model.set_baseband_bandwidth(SPECTRUM_SLICE_WIDTH);
    receiver_model.enable();
    
    // Enable audio for alert beeps
    audio::set_rate(audio::Rate::Hz_24000);
    audio::output::start();
    
    // Configure spectrum capture (baseband already loaded in constructor)
    baseband::set_spectrum(SPECTRUM_SLICE_WIDTH, 0);
    
    // Tune to first chunk (use direct radio API for speed)
    tune_to_chunk_center(current_chunk_center);
    
    // Start spectrum streaming
    baseband::spectrum_streaming_start();
    
    update_display();
}

void ScannerAppView::pause_scanning() {
    if (!is_scanning || is_paused) return;
    
    is_paused = true;
    
    button_scan_start.hidden(true);
    button_scan_start.set_focusable(false);
    
    button_pause_resume.set_text("RESUME");
    button_pause_resume.hidden(false);
    button_pause_resume.set_focusable(true);
    
    button_scan_stop.hidden(false);
    button_scan_stop.set_focusable(true);
    
    set_dirty();
    text_status.set("Status: Paused");
}

void ScannerAppView::resume_scanning() {
    if (!is_scanning || !is_paused) return;
    
    is_paused = false;
    
    button_scan_start.hidden(true);
    button_scan_start.set_focusable(false);
    
    button_pause_resume.set_text("PAUSE");
    button_pause_resume.hidden(false);
    button_pause_resume.set_focusable(true);
    
    button_scan_stop.hidden(false);
    button_scan_stop.set_focusable(true);
    
    set_dirty();
    text_status.set("Status: Scanning");
}

void ScannerAppView::stop_scanning() {
    if (!is_scanning) return;
    
    is_scanning = false;
    is_paused = false;
    
    // Stop streaming and disable receiver
    baseband::spectrum_streaming_stop();
    receiver_model.disable();
    // Note: Keep audio running for alert beeps!
    // Audio will be stopped in destructor
    
    button_scan_start.hidden(false);
    button_scan_start.set_focusable(true);
    
    button_pause_resume.hidden(true);
    button_pause_resume.set_focusable(false);
    
    button_scan_stop.hidden(true);
    button_scan_stop.set_focusable(false);
    
    set_dirty();
    text_status.set("Status: Stopped");
}

void ScannerAppView::scan_next_chunk() {
    if (current_range_index >= scan_ranges.size()) return;
    
    current_chunk_in_range++;
    
    auto& current_range = scan_ranges[current_range_index];
    
    if (current_chunk_in_range >= total_chunks_in_range) {
        // Finished current range, move to next
        move_to_next_range();
        return;
    }
    
    // Calculate next chunk center
    current_chunk_center = current_range.start + 
        (SPECTRUM_SLICE_WIDTH / 2) + 
        (current_chunk_in_range * SPECTRUM_SLICE_WIDTH);
    
    // Ensure we don't exceed range end
    if (current_chunk_center - (SPECTRUM_SLICE_WIDTH / 2) > current_range.end) {
        move_to_next_range();
        return;
    }
    
    tune_to_chunk_center(current_chunk_center);
    baseband::spectrum_streaming_start();  // Restart streaming after retune
    update_display();
}

void ScannerAppView::move_to_next_range() {
    current_range_index++;
    
    // Skip disabled ranges
    while (current_range_index < scan_ranges.size() && !scan_ranges[current_range_index].enabled) {
        current_range_index++;
    }
    
    if (current_range_index >= scan_ranges.size()) {
        // Completed one full scan cycle
        
        if (threat_detected) {
            // FPV DRONE DETECTED - STOP AND ALERT!
            text_status.set("*** FPV DRONE " + to_string_dec_uint(dome_signals_count) + " ***");
            
            // Play long alert beep BEFORE stopping (so audio is still active)
            baseband::request_audio_beep(1000, 24000, 500);
            
            // Now stop scanning
            stop_scanning();
            return;  // Stop here - user must manually restart
        }
        
        // No threats - continue scanning
        current_range_index = 0;
        while (current_range_index < scan_ranges.size() && !scan_ranges[current_range_index].enabled) {
            current_range_index++;
        }
        
        text_status.set("Status: Cycle " + to_string_dec_uint(dome_signals_count) + " (clear)");
    }
    
    // Start scanning new range
    if (current_range_index < scan_ranges.size()) {
        calculate_chunk_count();
        current_chunk_in_range = 0;
        
        auto& new_range = scan_ranges[current_range_index];
        current_chunk_center = new_range.start + (SPECTRUM_SLICE_WIDTH / 2);
        
        tune_to_chunk_center(current_chunk_center);
        baseband::spectrum_streaming_start();  // Restart streaming after retune
        update_display();
    }
}

void ScannerAppView::tune_to_chunk_center(rf::Frequency center_freq) {
    // Use direct radio tuning like Looking Glass (faster, doesn't save to persistent memory)
    radio::set_tuning_frequency(center_freq);
}

void ScannerAppView::on_channel_spectrum(const ChannelSpectrum& spectrum) {
    if (!is_scanning || is_paused) return;
    
    // Stop streaming while processing
    baseband::spectrum_streaming_stop();
    
    // Process the spectrum data
    process_spectrum_bins(spectrum);
    
    // Move to next chunk (which will restart streaming)
    scan_next_chunk();
}

void ScannerAppView::process_spectrum_bins(const ChannelSpectrum& spectrum) {
    // FFT spectrum parameters
    constexpr rf::Frequency bin_width = SPECTRUM_SLICE_WIDTH / SPECTRUM_BINS;  // ~78 kHz per bin
    constexpr int32_t NOISE_FLOOR_DBM = -120;  // Typical RF receiver noise floor
    constexpr int32_t MAX_SIGNAL_DBM = 0;      // Maximum signal power (1 mW)
    constexpr uint8_t RAW_MIN = 0;             // spectrum.db[] minimum value
    constexpr uint8_t RAW_MAX = 255;           // spectrum.db[] maximum value
    
    // Track signal runs (consecutive bins above threshold)
    uint8_t max_power_in_chunk = RAW_MIN;
    
    // Convert dBm threshold to raw FFT value (0-255)
    // Linear mapping: NOISE_FLOOR_DBM → 0, MAX_SIGNAL_DBM → 255
    int32_t dbm_range = MAX_SIGNAL_DBM - NOISE_FLOOR_DBM;  // 120 dB
    int32_t threshold_raw = ((squelch_threshold - NOISE_FLOOR_DBM) * RAW_MAX) / dbm_range;
    if (threshold_raw < RAW_MIN) threshold_raw = RAW_MIN;
    if (threshold_raw > RAW_MAX) threshold_raw = RAW_MAX;
    
    // Check if previous chunk had signal at its end (cross-chunk signal)
    size_t signal_start_bin = 0;
    size_t signal_end_bin = 0;
    bool in_signal = false;
    uint8_t signal_peak_power = 0;
    size_t signal_peak_bin = 0;
    
    // If previous chunk ended with a signal, check if it continues here
    if (signal_at_chunk_end) {
        // Check first few bins to see if signal continues
        bool continues = false;
        for (size_t bin = 0; bin < 10 && bin < SPECTRUM_BINS; bin++) {
            if (spectrum.db[bin] > threshold_raw) {
                continues = true;
                break;
            }
        }
        
        if (continues) {
            // Signal spans chunks! Start tracking from bin 0
            in_signal = true;
            signal_start_bin = 0;
            signal_peak_power = chunk_end_peak_power;  // Carry over peak from previous chunk
            signal_peak_bin = 0;
        }
        signal_at_chunk_end = false;  // Reset flag
    }
    
    for (size_t bin = 0; bin < SPECTRUM_BINS; bin++) {
        // Get raw power value (0-255 from FFT)
        uint8_t power_raw = spectrum.db[bin];
        
        if (power_raw > max_power_in_chunk) {
            max_power_in_chunk = power_raw;
        }
        
        if (power_raw > threshold_raw) {
            if (!in_signal) {
                // Start of new signal
                in_signal = true;
                signal_start_bin = bin;
                signal_peak_power = power_raw;
                signal_peak_bin = bin;
            } else {
                // Continue signal
                if (power_raw > signal_peak_power) {
                    signal_peak_power = power_raw;
                    signal_peak_bin = bin;
                }
            }
            signal_end_bin = bin;
        } else {
            if (in_signal) {
                // End of signal - analyze it
                rf::Frequency signal_width = (signal_end_bin - signal_start_bin + 1) * bin_width;
                
                // If signal started at bin 0, it's a cross-chunk signal - add previous chunk portion
                bool is_cross_chunk = (signal_start_bin == 0);
                if (is_cross_chunk) {
                    // Add approximate width from previous chunk
                    // Conservative estimate: signal extended from where it started to end of chunk
                    rf::Frequency prev_chunk_portion = (SPECTRUM_BINS - chunk_end_signal_start_bin) * bin_width;
                    signal_width += prev_chunk_portion;
                }
                
                rf::Frequency signal_freq = current_chunk_center - (SPECTRUM_SLICE_WIDTH / 2) + (signal_peak_bin * bin_width);
                
                // Check if signal width matches our criteria
                rf::Frequency min_width = min_signal_width_mhz * 1000000;
                rf::Frequency max_width = max_signal_width_mhz * 1000000;
                
                // Convert raw FFT value back to dBm for display
                int32_t power_dbm = NOISE_FLOOR_DBM + 
                    (signal_peak_power * (MAX_SIGNAL_DBM - NOISE_FLOOR_DBM) / RAW_MAX);
                
                if (signal_width >= min_width && signal_width <= max_width) {
                    // Analyze FM video dome shape for FPV detection
                    // For cross-chunk signals, we can only analyze the current chunk portion
                    bool has_video_dome = analyze_fm_dome_shape(spectrum, signal_start_bin, signal_end_bin);
                    
                    // Track widest signal for display (any signal, not just domes)
                    if (signal_width > widest_signal_width) {
                        widest_signal_width = signal_width;
                        widest_signal_freq = signal_freq;
                        widest_signal_rssi = power_dbm;
                        
                        text_widest.set("Widest: " + to_string_dec_uint(widest_signal_width / 1000000) + 
                                       " MHz @ " + to_string_short_freq(widest_signal_freq));
                    }
                    
                    // CRITICAL: Only alert for FPV dome signals (life-saving!)
                    if (has_video_dome) {
                        dome_signals_count++;
                        threat_detected = true;  // Flag to stop at cycle end
                        text_dome_signals.set("FPV Threats: " + to_string_dec_uint(dome_signals_count));
                        text_status.set("Status: THREAT DETECTED!");
                        
                        // Immediate alert beep
                        on_signal_found(signal_freq, power_dbm, signal_width);
                    }
                }
                
                in_signal = false;
            }
        }
    }
    
    // Handle signal that extends to end of chunk
    if (in_signal) {
        // Signal continues to the end - might continue in next chunk!
        signal_at_chunk_end = true;
        chunk_end_signal_start_bin = signal_start_bin;
        chunk_end_peak_power = signal_peak_power;
        pending_signal_freq = current_chunk_center - (SPECTRUM_SLICE_WIDTH / 2) + (signal_peak_bin * bin_width);
        
        // Calculate width seen so far (may be partial)
        rf::Frequency partial_width = (signal_end_bin - signal_start_bin + 1) * bin_width;
        
        // If this is a continuation from previous chunk, add widths
        if (signal_start_bin == 0) {
            // This is a cross-chunk signal - calculate total width
            // Estimate: signal spans from start of previous chunk continuation to end of this chunk
            // We'll report it in the next chunk if it ends, or now if it's already > max_width
            rf::Frequency max_width = max_signal_width_mhz * 1000000;
            
            if (partial_width > max_width) {
                // Already too wide, stop tracking (likely WiFi, not FPV)
                signal_at_chunk_end = false;  // Don't continue - too wide already
            }
        }
    } else {
        // Signal ended in this chunk - reset cross-chunk tracking
        signal_at_chunk_end = false;
    }
    
    // Update RSSI display with max power in this chunk
    int32_t max_dbm = NOISE_FLOOR_DBM + 
        (max_power_in_chunk * (MAX_SIGNAL_DBM - NOISE_FLOOR_DBM) / RAW_MAX);
    text_rssi.set("RSSI: " + to_string_dec_int(max_dbm) + " dBm (" + 
                 to_string_dec_uint(max_power_in_chunk) + ")");
}

void ScannerAppView::on_signal_found(rf::Frequency freq, int32_t rssi, rf::Frequency width) {
    // Play alert tone
    play_alert_tone();
    
    text_status.set("Signal: " + to_string_short_freq(freq) + " " +
                   to_string_dec_uint(width / 1000000) + " MHz " +
                   to_string_dec_int(rssi) + " dBm");
}

void ScannerAppView::update_display() {
    if (current_range_index >= scan_ranges.size()) return;
    
    auto& current_range = scan_ranges[current_range_index];
    text_current_range.set("Range: " + current_range.name);
    
    // Show chunk progress (e.g., "5800 MHz [3/10]")
    rf::Frequency freq_mhz = current_chunk_center / 1000000;
    text_current_freq.set("Chunk: " + to_string_dec_uint(freq_mhz) + " MHz [" +
                         to_string_dec_uint(current_chunk_in_range + 1) + "/" +
                         to_string_dec_uint(total_chunks_in_range) + "]");
}

void ScannerAppView::play_alert_tone() {
    // Play 1000 Hz beep for 200ms to alert user of FPV signal detection
    baseband::request_audio_beep(1000, 24000, 200);
}

void ScannerAppView::stop_alert_tone() {
    baseband::request_beep_stop();
}

bool ScannerAppView::analyze_fm_dome_shape(const ChannelSpectrum& spectrum, 
                                            size_t start_bin, size_t end_bin) {
    // Analyze spectrum shape to detect FM video "dome" characteristic of FPV drones
    // FM video has a smooth, elevated dome shape spanning 6-8 MHz
    
    if (end_bin <= start_bin || (end_bin - start_bin) < 10) {
        return false;  // Too narrow to analyze
    }
    
    // Calculate center and edges
    size_t center_bin = (start_bin + end_bin) / 2;
    size_t quarter_point = start_bin + (end_bin - start_bin) / 4;
    size_t three_quarter = start_bin + 3 * (end_bin - start_bin) / 4;
    
    // Get power values
    uint8_t left_edge = spectrum.db[start_bin];
    uint8_t left_quarter = spectrum.db[quarter_point];
    uint8_t center = spectrum.db[center_bin];
    uint8_t right_quarter = spectrum.db[three_quarter];
    uint8_t right_edge = spectrum.db[end_bin];
    
    // Dome characteristics:
    // 1. Center should be elevated (peak)
    // 2. Smooth gradual slopes on both sides
    // 3. Not spiky (not digital/noise)
    
    // Check if center is elevated
    uint8_t avg_edge = (left_edge + right_edge) / 2;
    bool peak_is_elevated = (center > avg_edge + 10);  // Center at least 10 units higher
    
    // Check for smooth dome (quarter points between edges and center)
    bool left_slope_smooth = (left_quarter > left_edge) && (left_quarter < center);
    bool right_slope_smooth = (right_quarter > right_edge) && (right_quarter < center);
    
    // Check symmetry (dome should be roughly symmetric)
    int32_t left_slope = left_quarter - left_edge;
    int32_t right_slope = right_quarter - right_edge;
    bool roughly_symmetric = std::abs(left_slope - right_slope) < 30;
    
    // FM video dome detected if all criteria met
    return peak_is_elevated && left_slope_smooth && right_slope_smooth && roughly_symmetric;
}

void initialize_app(ui::NavigationView& nav) {
    nav.push<ScannerAppView>();
}

} // namespace ui::external_app::ext_scanner

// ==========================================
// External App Entry Point
// ==========================================

extern "C" {

__attribute__((section(".external_app.app_ext_scanner.application_information"), used)) 
application_information_t _application_information_ext_scanner = {
    /*.memory_location = */ (uint8_t*)0x00000000,
    /*.externalAppEntry = */ ui::external_app::ext_scanner::initialize_app,
    /*.header_version = */ CURRENT_HEADER_VERSION,
    /*.app_version = */ VERSION_MD5,
    
    /*.app_name = */ "Spectrum Scan",
    /*.bitmap_data = */ {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xF0, 0x0F, 0xF8, 0x1F, 0x7C, 0x3E, 0x3E, 0x7C,
        0x1E, 0x78, 0x1F, 0xF8, 0x0F, 0xF0, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    },
    /*.icon_color = */ ui::Color::green().v,
    /*.menu_location = */ app_location_t::UTILITIES,
    /*.desired_menu_position = */ -1,
    
    /*.m4_app_tag = portapack::spi_flash::image_tag_wideband_spectrum */ {'P', 'S', 'P', 'E'},
    /*.m4_app_offset = */ 0x00000000,
};

} // extern "C"

