#include "analysis_handler.h"
#include "../../../Middle Bridge/analysis/ianalysis_controller.h"
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>

namespace agentic {

namespace {

std::vector<uint32_t> parseTrackRange(std::string_view trackStr) {
    std::vector<uint32_t> tracks;
    std::string str(trackStr);

    auto rangePos = str.find("..");
    if (rangePos != std::string::npos) {
        uint32_t start = static_cast<uint32_t>(std::atoi(str.substr(0, rangePos).c_str()));
        uint32_t end = static_cast<uint32_t>(std::atoi(str.substr(rangePos + 2).c_str()));
        if (start > 0 && end >= start) {
            for (uint32_t i = start; i <= end; ++i) {
                tracks.push_back(i);
            }
        }
        return tracks;
    }

    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (!token.empty()) {
            uint32_t val = static_cast<uint32_t>(std::atoi(token.c_str()));
            if (val > 0) tracks.push_back(val);
        }
    }

    if (tracks.empty()) {
        tracks.push_back(1);
    }
    return tracks;
}

} // namespace

ExecutionResult AnalysisHandler::handleCommand(const ParsedArgs& args, bridge::IAnalysisController* controller) {
    if (!controller) {
        return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "CONTROLLER_UNAVAILABLE", "Bridge IAnalysisController instance is not injected.");
    }

    std::string_view sub = args.getSubcommand();

    if (sub == "spectrum") {
        std::string_view trackStr = args.getOption("--track", "1");
        uint32_t trackId = static_cast<uint32_t>(std::atoi(std::string(trackStr).c_str()));

        auto res = controller->computeSpectrum(trackId);
        if (!res.success) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "TRACK_NOT_FOUND", res.errorMessage);
        }

        return ExecutionResult::Success("ANALYSIS_SPECTRUM", {
            {"TRACK_ID", std::to_string(trackId)},
            {"SPECTRAL_CENTROID_HZ", std::to_string(res.spectralCentroidHz)},
            {"SPECTRAL_TILT_DB_OCT", std::to_string(res.spectralTiltDbOct)},
            {"SPECTRAL_ROLLOFF_HZ", std::to_string(res.spectralRolloffHz)},
            {"SUB_BAND_DBFS", std::to_string(res.subBandDbfs)},
            {"BASS_BAND_DBFS", std::to_string(res.bassBandDbfs)},
            {"LOW_MID_BAND_DBFS", std::to_string(res.lowMidBandDbfs)},
            {"MID_BAND_DBFS", std::to_string(res.midBandDbfs)},
            {"HIGH_MID_BAND_DBFS", std::to_string(res.highMidBandDbfs)},
            {"HIGHS_BAND_DBFS", std::to_string(res.highsBandDbfs)},
            {"AIR_BAND_DBFS", std::to_string(res.airBandDbfs)}
        });
    }

    if (sub == "resonance") {
        std::string_view trackStr = args.getOption("--track", "1");
        uint32_t trackId = static_cast<uint32_t>(std::atoi(std::string(trackStr).c_str()));

        auto res = controller->computeResonances(trackId);
        if (!res.success) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "TRACK_NOT_FOUND", res.errorMessage);
        }

        std::map<std::string, std::string> fields;
        fields["TRACK_ID"] = std::to_string(trackId);
        fields["TOTAL_RESONANCES_FOUND"] = std::to_string(res.resonances.size());

        for (size_t i = 0; i < res.resonances.size(); ++i) {
            std::string prefix = "RESONANCE_" + std::to_string(i + 1) + "_";
            const auto& r = res.resonances[i];
            fields[prefix + "FREQ_HZ"] = std::to_string(r.freqHz);
            fields[prefix + "NOTE"] = r.notePitch;
            fields[prefix + "PROMINENCE_DB"] = "+" + std::to_string(r.prominenceDb);
            fields[prefix + "Q_FACTOR"] = std::to_string(r.qFactor);
            fields[prefix + "SEVERITY"] = r.severity;
            fields[prefix + "REC_NOTCH_DB"] = std::to_string(r.recNotchDb);
        }

        return ExecutionResult::Success("ANALYSIS_RESONANCE", std::move(fields));
    }

    if (sub == "masking") {
        if (args.getOption("--vs").empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Missing required --vs target track argument.");
        }
        std::string_view primaryStr = args.getOption("--track", "1");
        std::string_view vsStr = args.getOption("--vs", "5");
        uint32_t primaryTrack = static_cast<uint32_t>(std::atoi(std::string(primaryStr).c_str()));
        uint32_t vsTrack = static_cast<uint32_t>(std::atoi(std::string(vsStr).c_str()));

        auto res = controller->computeMasking(primaryTrack, vsTrack);
        if (!res.success) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "TRACK_NOT_FOUND", res.errorMessage);
        }

        std::map<std::string, std::string> fields;
        fields["PRIMARY_TRACK"] = std::to_string(primaryTrack);
        fields["VS_TRACK"] = std::to_string(vsTrack);
        fields["OVERALL_MASKING_INDEX"] = std::to_string(res.overallMaskingIndex);
        fields["COLLISION_RISK"] = res.collisionRisk;
        fields["MASKED_BAND_COUNT"] = std::to_string(res.maskedBands.size());

        for (size_t i = 0; i < res.maskedBands.size(); ++i) {
            std::string prefix = "BAND_" + std::to_string(i + 1) + "_";
            const auto& b = res.maskedBands[i];
            fields[prefix + "RANGE_HZ"] = b.rangeHz;
            fields[prefix + "MASK_AMOUNT_DB"] = std::to_string(b.maskAmountDb);
            fields[prefix + "RECOMMENDED_ACTION"] = b.recommendedAction;
        }

        return ExecutionResult::Success("ANALYSIS_MASKING", std::move(fields));
    }

    if (sub == "loudness") {
        std::string_view trackStr = args.getOption("--track", "0");
        uint32_t trackId = static_cast<uint32_t>(std::atoi(std::string(trackStr).c_str()));

        auto res = controller->computeLoudness(trackId);
        if (!res.success) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "TRACK_NOT_FOUND", res.errorMessage);
        }

        return ExecutionResult::Success("ANALYSIS_LOUDNESS", {
            {"TRACK_ID", std::to_string(trackId)},
            {"INTEGRATED_LUFS", std::to_string(res.integratedLufs)},
            {"SHORT_TERM_MAX_LUFS", std::to_string(res.shortTermMaxLufs)},
            {"MOMENTARY_MAX_LUFS", std::to_string(res.momentaryMaxLufs)},
            {"LRA_LU", std::to_string(res.lraLu)},
            {"CREST_FACTOR_DB", std::to_string(res.crestFactorDb)},
            {"SAMPLE_PEAK_DBFS", std::to_string(res.samplePeakDbfs)},
            {"TRUE_PEAK_DBTP", std::to_string(res.truePeakDbtp)}
        });
    }

    if (sub == "true-peak") {
        std::string_view trackStr = args.getOption("--track", "0");
        uint32_t trackId = static_cast<uint32_t>(std::atoi(std::string(trackStr).c_str()));

        auto res = controller->computeTruePeak(trackId);
        if (!res.success) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "TRACK_NOT_FOUND", res.errorMessage);
        }

        return ExecutionResult::Success("ANALYSIS_TRUE_PEAK", {
            {"TRACK_ID", std::to_string(trackId)},
            {"MAX_TRUE_PEAK_DBTP", std::to_string(res.maxTruePeakDbtp)},
            {"TOTAL_CLIPPING_EVENTS", std::to_string(res.totalClippingEvents)},
            {"SAFETY_STATUS", res.safetyStatus}
        });
    }

    if (sub == "phase-matrix") {
        std::string_view tracksStr = args.getOption("--track", "1..4");
        auto trackIds = parseTrackRange(tracksStr);

        auto res = controller->computePhaseMatrix(trackIds);
        if (!res.success) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "TRACK_NOT_FOUND", res.errorMessage);
        }

        std::map<std::string, std::string> fields;
        fields["TRACKS_ANALYZED"] = std::string(tracksStr);
        fields["GLOBAL_GROUP_HEALTH"] = res.globalHealth;

        fields["WORST_PAIR_1"] = "Track_" + std::to_string(res.worstPairTrackA) + " vs Track_" + std::to_string(res.worstPairTrackB);
        fields["WORST_CORRELATION_1"] = std::to_string(res.worstCorrelation);
        fields["REC_ACTION_1"] = res.recommendedAction;

        std::string matrixStr;
        for (size_t i = 0; i < res.flatMatrix.size(); ++i) {
            if (i > 0) matrixStr += (i % trackIds.size() == 0) ? "|" : ",";
            matrixStr += std::to_string(res.flatMatrix[i]);
        }
        fields["CORRELATION_MATRIX_FLAT"] = matrixStr;

        return ExecutionResult::Success("ANALYSIS_PHASE_MATRIX", std::move(fields));
    }

    if (sub == "phase-align") {
        std::string_view trackAStr = args.getOption("--track", "1");
        std::string_view trackBStr = args.getOption("--vs", "2");
        uint32_t trackA = static_cast<uint32_t>(std::atoi(std::string(trackAStr).c_str()));
        uint32_t trackB = static_cast<uint32_t>(std::atoi(std::string(trackBStr).c_str()));

        auto res = controller->computePhaseAlign(trackA, trackB);
        if (!res.success) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "TRACK_NOT_FOUND", res.errorMessage);
        }

        return ExecutionResult::Success("ANALYSIS_PHASE_ALIGN", {
            {"PRIMARY_TRACK", std::to_string(trackA)},
            {"VS_TRACK", std::to_string(trackB)},
            {"RECOMMENDED_SAMPLE_OFFSET", std::to_string(res.recommendedSampleOffset)},
            {"RECOMMENDED_TIME_OFFSET_MS", std::to_string(res.recommendedTimeOffsetMs)},
            {"CURRENT_CORRELATION", std::to_string(res.currentCorrelation)},
            {"IMPROVED_CORRELATION", std::to_string(res.improvedCorrelation)},
            {"RECOMMENDED_ACTION", res.recommendedAction}
        });
    }

    if (sub == "live") {
        std::string_view trackStr = args.getOption("--track", "0");
        std::string_view windowStr = args.getOption("--window-ms", "400");
        uint32_t trackId = static_cast<uint32_t>(std::atoi(std::string(trackStr).c_str()));
        uint32_t windowMs = static_cast<uint32_t>(std::atoi(std::string(windowStr).c_str()));

        auto res = controller->getLiveTelemetry(trackId, windowMs);
        if (!res.success) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "TRACK_NOT_FOUND", res.errorMessage);
        }

        return ExecutionResult::Success("ANALYSIS_LIVE_STREAM", {
            {"TRACK_ID", std::to_string(trackId)},
            {"WINDOW_DURATION_MS", std::to_string(windowMs)},
            {"MOMENTARY_LUFS", std::to_string(res.telemetry.momentaryLufs)},
            {"SHORT_TERM_LUFS", std::to_string(res.telemetry.shortTermLufs)},
            {"SAMPLE_PEAK_DBFS", std::to_string(res.telemetry.peakDbfs)},
            {"TRUE_PEAK_DBTP", std::to_string(res.telemetry.truePeakDbtp)},
            {"IS_CLIPPING", res.telemetry.isClipping ? "TRUE" : "FALSE"},
            {"CLIPPING_EVENTS_COUNT", std::to_string(res.telemetry.clipEventsCount)},
            {"CREST_FACTOR_DB", std::to_string(res.telemetry.crestFactorDb)},
            {"SPECTRAL_CENTROID_HZ", std::to_string(res.telemetry.spectralCentroidHz)},
            {"STEREO_CORRELATION", std::to_string(res.telemetry.stereoCorrelation)},
            {"SAFETY_STATUS", res.safetyStatus},
            {"REC_GAIN_TRIM_DB", std::to_string(res.recGainTrimDb)}
        });
    }

    if (sub == "stereo-width") {
        std::string_view trackStr = args.getOption("--track", "0");
        uint32_t trackId = static_cast<uint32_t>(std::atoi(std::string(trackStr).c_str()));

        auto res = controller->computeStereoWidth(trackId);
        if (!res.success) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "TRACK_NOT_FOUND", res.errorMessage);
        }

        return ExecutionResult::Success("ANALYSIS_STEREO_WIDTH", {
            {"TRACK_ID", std::to_string(trackId)},
            {"MID_RMS_DBFS", std::to_string(res.midRmsDbfs)},
            {"SIDE_RMS_DBFS", std::to_string(res.sideRmsDbfs)},
            {"MS_RATIO_DB", std::to_string(res.msRatioDb)},
            {"STEREO_WIDTH_PCT", std::to_string(res.stereoWidthPct) + "%"},
            {"MONO_FOLD_LOSS_DB", std::to_string(res.monoFoldLossDb)}
        });
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Unknown analyze subcommand.");
}

} // namespace agentic
