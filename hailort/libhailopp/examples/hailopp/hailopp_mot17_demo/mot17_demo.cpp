/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mot17_demo.cpp
 * @brief MOT17 Dataset Object Tracking Demo (No Visualization)
 * 
 * This program processes MOT17 format detections and outputs tracking results.
 * Compatible with MOT Challenge evaluation tools.
 * 
 * Usage:
 *   ./mot17_demo <sequence_path> [options]
 * 
 * Example:
 *   ./mot17_demo MOT17/train/MOT17-02-FRCNN --output results.txt
 **/

#include "hailotracker.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct SequenceInfo {
    std::string name;
    int frame_rate = 30;
    int frame_count = 0;
    int image_width = 1920;
    int image_height = 1080;
};

struct MotDetection {
    int frame = 0;
    float32_t bb_left = 0.0f;
    float32_t bb_top = 0.0f;
    float32_t bb_width = 0.0f;
    float32_t bb_height = 0.0f;
    float32_t confidence = 0.0f;
};

struct TrackOutput {
    uint32_t id = 0;
    float32_t position_x = 0.0f;
    float32_t position_y = 0.0f;
    float32_t width = 0.0f;
    float32_t height = 0.0f;
    float32_t score = 0.0f;
};

struct DetectionDataset {
    std::map<int, std::vector<MotDetection>> frames;
    size_t max_per_frame = 0;
    int max_frame_index = 0;
};

struct DemoOptions {
    std::string sequence_path;
    std::string detections_file;
    std::string output_file = "output.txt";
    int detection_period = 1;
    float32_t confidence_threshold = 0.5f;
    float32_t iou_threshold = 0.3f;
    int max_age = 30;
    int min_hits = 3;
    bool verbose = false;
};

namespace {

void print_usage(const char *program) {
    std::cout << "MOT17 Hailo Tracker Demo (No Visualization)\n\n"
              << "Usage: " << program << " <sequence_path> [options]\n\n"
              << "Options:\n"
              << "  --det-file <file>       Override detections file (default: <sequence>/det/det.txt)\n"
              << "  --output <file>         Save MOT17 formatted results (default: output.txt)\n"
              << "  --detection-period <N>  Process detections every N frames (default: 1)\n"
              << "  --conf <value>          Confidence threshold (default: 0.5)\n"
              << "  --iou <value>           IoU threshold (default: 0.3)\n"
              << "  --max-age <N>           Max frames to keep lost tracks (default: 30)\n"
              << "  --min-hits <N>          Min matches before confirming (default: 3)\n"
              << "  --verbose               Print detailed progress\n\n"
              << "Examples:\n"
              << "  " << program << " MOT17/train/MOT17-02-FRCNN --output results.txt\n"
              << "  " << program << " MOT17/train/MOT17-04-SDP --conf 0.6 --iou 0.4\n"
              << std::endl;
}

DemoOptions parse_cli(int argc, char **argv) {
    DemoOptions opts;
    if (argc < 2) {
        return opts;
    }
 
    opts.sequence_path = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--det-file" && (i + 1) < argc) {
            opts.detections_file = argv[++i];
        } else if (arg == "--output" && (i + 1) < argc) {
            opts.output_file = argv[++i];
        } else if (arg == "--detection-period" && (i + 1) < argc) {
            opts.detection_period = std::stoi(argv[++i]);
        } else if (arg == "--conf" && (i + 1) < argc) {
            opts.confidence_threshold = std::stof(argv[++i]);
        } else if (arg == "--iou" && (i + 1) < argc) {
            opts.iou_threshold = std::stof(argv[++i]);
        } else if (arg == "--max-age" && (i + 1) < argc) {
            opts.max_age = std::stoi(argv[++i]);
        } else if (arg == "--min-hits" && (i + 1) < argc) {
            opts.min_hits = std::stoi(argv[++i]);
        } else if (arg == "--verbose") {
            opts.verbose = true;
        }
    }

    if (opts.detections_file.empty()) {
        opts.detections_file = opts.sequence_path + "/det/det.txt";
    }

    return opts;
}

SequenceInfo parse_seqinfo(const std::string &seq_path) {
    SequenceInfo info;
    std::ifstream file(seq_path + "/seqinfo.ini");
    if (!file.is_open()) {
        std::cerr << "Warning: seqinfo.ini not found, using defaults\n";
        return info;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("name=", 0) == 0) {
            info.name = line.substr(5);
        } else if (line.rfind("frameRate=", 0) == 0) {
            info.frame_rate = std::stoi(line.substr(10));
        } else if (line.rfind("seqLength=", 0) == 0) {
            info.frame_count = std::stoi(line.substr(10));
        } else if (line.rfind("imWidth=", 0) == 0) {
            info.image_width = std::stoi(line.substr(8));
        } else if (line.rfind("imHeight=", 0) == 0) {
            info.image_height = std::stoi(line.substr(9));
        }
    }
    return info;
}

DetectionDataset load_detections(const DemoOptions &opts) {
    DetectionDataset dataset;
    std::ifstream file(opts.detections_file);
    if (!file.is_open()) {
        std::cerr << "Error: cannot open detections file " << opts.detections_file << '\n';
        return dataset;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        MotDetection det;
        int raw_id = -1;
        char comma = 0;
        std::istringstream iss(line);

        if (!(iss >> det.frame >> comma >> raw_id >> comma >> det.bb_left >> comma >> det.bb_top >> comma >>
              det.bb_width >> comma >> det.bb_height >> comma >> det.confidence)) {
            continue;
        }

        if (det.confidence < opts.confidence_threshold) {
            continue;
        }

        dataset.frames[det.frame].push_back(det);
        dataset.max_per_frame = std::max(dataset.max_per_frame, dataset.frames[det.frame].size());
        dataset.max_frame_index = std::max(dataset.max_frame_index, det.frame);
    }

    return dataset;
}

hailo_tracker_config_t create_config(const DemoOptions &opts) {
    hailo_tracker_config_t cfg = HAILO_TRACKER_CONFIG_DEFAULT;
    cfg.min_confirmed_frames = static_cast<uint8_t>(std::min(opts.min_hits, 255));
    cfg.aging_threshold = static_cast<uint8_t>(std::min(opts.max_age, 255));
    cfg.add_threshold = opts.confidence_threshold;
    cfg.association_threshold = opts.iou_threshold;
    return cfg;
}

std::vector<uint8_t> create_detection_storage(size_t capacity) {
    capacity = std::max<size_t>(capacity, 1);
    size_t buffer_size = sizeof(hailo_detections_t) + capacity * sizeof(hailo_detection_t);
    return std::vector<uint8_t>(buffer_size, 0);
}

void fill_detection_buffer(const std::vector<MotDetection> &frame_dets, hailo_detections_t *detections) {
    detections->count = static_cast<uint16_t>(frame_dets.size());
    for (size_t i = 0; i < frame_dets.size(); ++i) {
        const auto &src = frame_dets[i];
        hailo_detection_t &dst = detections->detections[i];
        
        // hailo_detection_t order: y_min, x_min, y_max, x_max
        dst.y_min = src.bb_top;
        dst.x_min = src.bb_left;
        dst.y_max = src.bb_top + src.bb_height;
        dst.x_max = src.bb_left + src.bb_width;
        dst.score = src.confidence;
        dst.class_id = 0;
    }
}

bool write_results(const std::string &path, const std::map<int, std::vector<TrackOutput>> &results) {
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: cannot create output file " << path << '\n';
        return false;
    }

    file << std::fixed << std::setprecision(2);
    for (const auto &frame_pair : results) {
        int frame = frame_pair.first;
        for (const auto &entry : frame_pair.second) {
            // MOT format: frame,id,bb_left,bb_top,bb_width,bb_height,conf,-1,-1,-1
            float32_t bb_left = entry.position_x - entry.width * 0.5f;
            float32_t bb_top = entry.position_y - entry.height * 0.5f;
            
            file << frame << ',' << entry.id << ',' << bb_left << ',' << bb_top << ',' 
                 << entry.width << ',' << entry.height << ',' << entry.score << ",-1,-1,-1\n";
        }
    }

    return true;
}

} // namespace

int main(int argc, char **argv) {
    DemoOptions opts = parse_cli(argc, argv);
    if (opts.sequence_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "=== MOT17 Hailo Tracker Demo ===\n";
    std::cout << "Sequence: " << opts.sequence_path << "\n";

    // Parse sequence info
    SequenceInfo seq_info = parse_seqinfo(opts.sequence_path);
    std::cout << "Name: " << seq_info.name << ", Frames: " << seq_info.frame_count 
              << ", FPS: " << seq_info.frame_rate << "\n";

    // Load detections
    DetectionDataset dataset = load_detections(opts);
    if (dataset.frames.empty()) {
        std::cerr << "No detections loaded. Exiting.\n";
        return 1;
    }
    std::cout << "Loaded " << dataset.frames.size() << " frames, max " 
              << dataset.max_per_frame << " detections/frame\n";

    // Create tracker
    hailo_tracker_config_t config = create_config(opts);
    std::cout << "Tracker: max_age=" << static_cast<int>(config.aging_threshold)
              << ", min_hits=" << static_cast<int>(config.min_confirmed_frames)
              << ", iou=" << config.association_threshold << "\n";

    hailo_tracker tracker = nullptr;
    hailopp_status status = hailo_tracker_create(&config, &tracker);
    if (status != HAILOPP_SUCCESS) {
        std::cerr << "Failed to create tracker, status=" << status << '\n';
        return 1;
    }

    // Prepare storage
    auto storage = create_detection_storage(dataset.max_per_frame);
    auto *detections = reinterpret_cast<hailo_detections_t *>(storage.data());

    int last_frame = seq_info.frame_count > 0 ? seq_info.frame_count : dataset.max_frame_index;
    std::map<int, std::vector<TrackOutput>> results;

    // Timing
    auto start_time = std::chrono::high_resolution_clock::now();
    size_t total_detections = 0;
    size_t total_tracks = 0;

    std::cout << "\nProcessing " << last_frame << " frames...\n";

    for (int frame = 1; frame <= last_frame; ++frame) {
        // Get detections for current frame
        auto it = dataset.frames.find(frame);
        if (it != dataset.frames.end() && (frame % opts.detection_period == 0)) {
            fill_detection_buffer(it->second, detections);
            total_detections += detections->count;
        } else {
            detections->count = 0;
        }

        // Predict
        hailo_tracklets_t tracklets{nullptr, 0};
        status = hailo_tracker_predict(tracker, &tracklets);
        if (status != HAILOPP_SUCCESS) {
            std::cerr << "Predict failed on frame " << frame << '\n';
            break;
        }

        // Update
        status = hailo_tracker_update(tracker, detections);
        if (status != HAILOPP_SUCCESS) {
            std::cerr << "Update failed on frame " << frame << '\n';
            break;
        }

        // Collect results (only TRACKED state)
        std::vector<TrackOutput> frame_results;
        for (size_t i = 0; i < tracklets.count; ++i) {
            const auto &tracklet = tracklets.tracklets[i];
            if (tracklet.state == HAILO_TRACKLET_STATE_TRACKED) {
                TrackOutput entry;
                entry.id = tracklet.id;
                // Get bounding box from detection
                const auto& det = tracklet.detection;
                entry.width = det.x_max - det.x_min;
                entry.height = det.y_max - det.y_min;
                entry.position_x = det.x_min + entry.width * 0.5f;
                entry.position_y = det.y_min + entry.height * 0.5f;
                entry.score = det.score;
                frame_results.push_back(entry);
                total_tracks++;
            }
        }

        if (!frame_results.empty()) {
            results[frame] = std::move(frame_results);
        }

        // Progress
        if (opts.verbose && (frame % 100 == 0 || frame == last_frame)) {
            std::cout << "  Frame " << frame << "/" << last_frame << "\n";
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    hailo_tracker_release(tracker);

    // Write results
    if (!write_results(opts.output_file, results)) {
        return 1;
    }

    // Summary
    std::cout << "\n=== Summary ===\n";
    std::cout << "Frames processed: " << last_frame << "\n";
    std::cout << "Total detections: " << total_detections << "\n";
    std::cout << "Total track outputs: " << total_tracks << "\n";
    std::cout << "Processing time: " << duration_ms << " ms\n";
    std::cout << "Average: " << (duration_ms > 0 ? static_cast<double>(last_frame) * 1000.0 / duration_ms : 0) << " FPS\n";
    std::cout << "Output: " << opts.output_file << "\n";

    return 0;
}

