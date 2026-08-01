#include "analysis_handler.h"

namespace agentic {

ExecutionResult AnalysisHandler::handleCommand(const ParsedArgs& args) {
    std::string_view sub = args.getSubcommand();

    if (sub == "spectrum") {
        std::string_view trackStr = args.getOption("--track", "1");
        return ExecutionResult::Success("ANALYSIS_SPECTRUM", {
            {"TRACK_ID", std::string(trackStr)},
            {"SPECTRAL_CENTROID_HZ", "2145.2"},
            {"SPECTRAL_TILT_DB_OCT", "-3.8"},
            {"SPECTRAL_ROLLOFF_HZ", "13500.0"},
            {"SUB_BAND_DBFS", "-48.5"},
            {"BASS_BAND_DBFS", "-28.4"},
            {"LOW_MID_BAND_DBFS", "-16.2"},
            {"MID_BAND_DBFS", "-14.1"},
            {"HIGH_MID_BAND_DBFS", "-15.8"},
            {"HIGHS_BAND_DBFS", "-21.2"},
            {"AIR_BAND_DBFS", "-28.9"}
        });
    }

    if (sub == "resonance") {
        std::string_view trackStr = args.getOption("--track", "1");
        return ExecutionResult::Success("ANALYSIS_RESONANCE", {
            {"TRACK_ID", std::string(trackStr)},
            {"TOTAL_RESONANCES_FOUND", "1"},
            {"FREQ_HZ", "315.4"},
            {"NOTE_PITCH", "D#4"},
            {"PROMINENCE_DB", "+8.4"},
            {"Q_FACTOR", "14.2"},
            {"SEVERITY", "HIGH"}
        });
    }

    if (sub == "masking") {
        std::string_view track1 = args.getOption("--track", "1");
        std::string_view track2 = args.getOption("--vs", "5");
        return ExecutionResult::Success("ANALYSIS_MASKING", {
            {"PRIMARY_TRACK", std::string(track1)},
            {"VS_TRACK", std::string(track2)},
            {"OVERALL_MASKING_INDEX", "0.78"},
            {"COLLISION_RISK", "HIGH_MASKING"}
        });
    }

    if (sub == "loudness") {
        std::string_view trackStr = args.getOption("--track", "0");
        return ExecutionResult::Success("ANALYSIS_LOUDNESS", {
            {"TRACK_ID", std::string(trackStr)},
            {"INTEGRATED_LUFS", "-12.4"},
            {"SHORT_TERM_MAX_LUFS", "-9.8"},
            {"MOMENTARY_MAX_LUFS", "-8.5"},
            {"LRA_LU", "5.2"},
            {"CREST_FACTOR_DB", "10.8"},
            {"SAMPLE_PEAK_DBFS", "-0.1"},
            {"TRUE_PEAK_DBTP", "+0.6"}
        });
    }

    if (sub == "true-peak") {
        std::string_view trackStr = args.getOption("--track", "0");
        return ExecutionResult::Success("ANALYSIS_TRUE_PEAK", {
            {"TRACK_ID", std::string(trackStr)},
            {"MAX_TRUE_PEAK_DBTP", "+0.6"},
            {"TOTAL_CLIPPING_EVENTS", "14"},
            {"SAFETY_STATUS", "VIOLATION_DANGER"}
        });
    }

    if (sub == "phase-matrix") {
        std::string_view tracks = args.getOption("--track", "1..5");
        return ExecutionResult::Success("ANALYSIS_PHASE_MATRIX", {
            {"TRACKS", std::string(tracks)},
            {"GLOBAL_GROUP_HEALTH", "WARNING_SEVERE_CANCELLATION_DETECTED"},
            {"WORST_PAIR", "Sub_Kick vs Kick_In"},
            {"WORST_CORRELATION", "-0.74"}
        });
    }

    if (sub == "stereo-width") {
        std::string_view trackStr = args.getOption("--track", "0");
        return ExecutionResult::Success("ANALYSIS_STEREO_WIDTH", {
            {"TRACK_ID", std::string(trackStr)},
            {"MID_RMS_DBFS", "-14.2"},
            {"SIDE_RMS_DBFS", "-20.5"},
            {"MS_RATIO_DB", "-6.3"},
            {"STEREO_WIDTH_PCT", "64.2%"},
            {"MONO_FOLD_LOSS_DB", "-1.2"}
        });
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Unknown analyze subcommand.");
}

} // namespace agentic
