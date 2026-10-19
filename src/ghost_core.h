#pragma once
#include <string>

enum class GhostPhase {
    Starting,
    Waiting,
    PairingPin,
    Connected,
    Error
};

enum class GhostPreset {
    LowLatency1080p60 = 0,
    Sharp1440p60 = 1,
    Ultra4K60H265 = 2,
    Custom = 3
};

enum class GhostRes {
    Res720p  = 0, // 1280x720
    Res1080p = 1, // 1920x1080
    Res1440p = 2, // 2560x1440
    Res4K    = 3  // 3840x2160
};

enum class GhostCodec {
    H264 = 0,
    H265 = 1
};

struct GhostConfig {
    std::string server_name = "Ghost";
    GhostPreset preset = GhostPreset::LowLatency1080p60;
    GhostRes res = GhostRes::Res1080p;
    int fps = 60; // 30, 60, 120
    GhostCodec codec = GhostCodec::H264;
    bool vsync = false; // false = -vsync no (lowest latency)
    bool audio_enabled = true;
    int volume_percent = 85; // 0..100 real-time WASAPI volume slider
    bool pin_enabled = false;
    bool fullscreen = false;
};

struct GhostState {
    GhostPhase phase = GhostPhase::Starting;
    std::string pin;
    std::string client_name;
    std::string client_model;
    std::string lan_ip;
    std::string error_msg;
    std::string last_log;
};

inline void apply_preset(GhostConfig& cfg, GhostPreset p) {
    cfg.preset = p;
    switch (p) {
        case GhostPreset::LowLatency1080p60:
            cfg.res = GhostRes::Res1080p;
            cfg.fps = 60;
            cfg.codec = GhostCodec::H264;
            cfg.vsync = false;
            break;
        case GhostPreset::Sharp1440p60:
            cfg.res = GhostRes::Res1440p;
            cfg.fps = 60;
            cfg.codec = GhostCodec::H264;
            cfg.vsync = false;
            break;
        case GhostPreset::Ultra4K60H265:
            cfg.res = GhostRes::Res4K;
            cfg.fps = 60;
            cfg.codec = GhostCodec::H265;
            cfg.vsync = false;
            break;
        case GhostPreset::Custom:
            break;
    }
}

inline GhostPreset next_preset(GhostPreset p) {
    if (p == GhostPreset::LowLatency1080p60) return GhostPreset::Sharp1440p60;
    if (p == GhostPreset::Sharp1440p60)      return GhostPreset::Ultra4K60H265;
    return GhostPreset::LowLatency1080p60;
}

inline GhostRes next_res(GhostRes r) {
    return static_cast<GhostRes>((static_cast<int>(r) + 1) % 4);
}

inline int next_fps(int fps) {
    if (fps == 30) return 60;
    if (fps == 60) return 120;
    return 30;
}

inline GhostCodec next_codec(GhostCodec c) {
    return (c == GhostCodec::H264) ? GhostCodec::H265 : GhostCodec::H264;
}

inline const wchar_t* preset_label(GhostPreset p) {
    switch (p) {
        case GhostPreset::LowLatency1080p60: return L"Low Latency (1080p60 H.264)";
        case GhostPreset::Sharp1440p60:      return L"Sharp QHD (1440p60 H.264)";
        case GhostPreset::Ultra4K60H265:     return L"Ultra 4K (2160p60 H.265)";
        case GhostPreset::Custom:            return L"Custom Advance Tuning";
    }
    return L"Custom";
}

inline const wchar_t* res_label(GhostRes r) {
    switch (r) {
        case GhostRes::Res720p:  return L"RES: 720p";
        case GhostRes::Res1080p: return L"RES: 1080p";
        case GhostRes::Res1440p: return L"RES: 1440p";
        case GhostRes::Res4K:    return L"RES: 4K UHD";
    }
    return L"RES: 1080p";
}

inline std::string res_dimensions(GhostRes r) {
    switch (r) {
        case GhostRes::Res720p:  return "1280x720";
        case GhostRes::Res1080p: return "1920x1080";
        case GhostRes::Res1440p: return "2560x1440";
        case GhostRes::Res4K:    return "3840x2160";
    }
    return "1920x1080";
}

inline const wchar_t* codec_label(GhostCodec c) {
    return (c == GhostCodec::H265) ? L"CODEC: H.265" : L"CODEC: H.264";
}

// ponytail: fixed substring parser over UxPlay 1.74 log lines; upgrade to structured IPC only if UxPlay changes log format
inline void parse_uxplay_line(GhostState& st, const std::string& raw_line) {
    std::string line = raw_line;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
        line.pop_back();
    }
    if (line.empty()) return;
    st.last_log = line;

    // 0. LAN IPv4 address advertisement
    const std::string ip_prefix = "mDNS advertising on LAN IPv4 ";
    auto ip_pos = line.find(ip_prefix);
    if (ip_pos != std::string::npos) {
        st.lan_ip = line.substr(ip_pos + ip_prefix.size());
        if (st.phase == GhostPhase::Starting) {
            st.phase = GhostPhase::Waiting;
        }
        return;
    }

    // 1. PIN prompt: *** CLIENT MUST NOW ENTER PIN = "1234" AS AIRPLAY PASSWORD
    const std::string pin_marker = "ENTER PIN = \"";
    auto pin_pos = line.find(pin_marker);
    if (pin_pos != std::string::npos) {
        size_t start = pin_pos + pin_marker.size();
        size_t end = line.find('"', start);
        if (end != std::string::npos && end > start) {
            st.pin = line.substr(start, end - start);
            st.phase = GhostPhase::PairingPin;
            return;
        }
    }

    // 2. Client connected: connection request from <Name> (<Model>) with deviceID = <MAC>
    const std::string conn_prefix = "connection request from ";
    const std::string conn_mid = ") with deviceID = ";
    auto conn_pos = line.find(conn_prefix);
    auto mid_pos = line.find(conn_mid);
    if (conn_pos != std::string::npos && mid_pos != std::string::npos && mid_pos > conn_pos) {
        size_t name_start = conn_pos + conn_prefix.size();
        std::string name_and_model = line.substr(name_start, mid_pos - name_start);
        auto paren_pos = name_and_model.rfind(" (");
        if (paren_pos != std::string::npos) {
            st.client_name = name_and_model.substr(0, paren_pos);
            st.client_model = name_and_model.substr(paren_pos + 2);
        } else {
            st.client_name = name_and_model;
            st.client_model.clear();
        }
        st.pin.clear();
        st.phase = GhostPhase::Connected;
        return;
    }

    // 3. Client disconnected / reset back to waiting
    if (line.find("tcp socket was closed by client") != std::string::npos ||
        line.find("lost connection with client") != std::string::npos ||
        line.find("video_reset") != std::string::npos) {
        st.phase = GhostPhase::Waiting;
        st.client_name.clear();
        st.client_model.clear();
        st.pin.clear();
        return;
    }

    // 4. Server ready
    if (line.find("using network ports UDP") != std::string::npos ||
        line.find("using system MAC address") != std::string::npos ||
        line.find("using randomly-generated MAC address") != std::string::npos ||
        line.find("Initialized server socket(s)") != std::string::npos) {
        if (st.phase == GhostPhase::Starting || st.phase == GhostPhase::Error) {
            st.phase = GhostPhase::Waiting;
            st.error_msg.clear();
        }
        return;
    }

    // 5. Fatal startup errors
    if (line.find("*** ERROR:") != std::string::npos ||
        line.find("dnssd_register_raop failed") != std::string::npos ||
        line.find("Could not initialize") != std::string::npos) {
        st.phase = GhostPhase::Error;
        st.error_msg = line;
        return;
    }
}

inline std::string build_uxplay_args(const GhostConfig& cfg) {
    std::string fps_str = std::to_string(cfg.fps);
    std::string args = "-n \"" + cfg.server_name + "\" -nh -p -reset 5";

    if (!cfg.vsync) {
        args += " -vsync no";
    }

    args += " -s " + res_dimensions(cfg.res) + "@" + fps_str + " -fps " + fps_str;

    if (cfg.codec == GhostCodec::H265) {
        args += " -h265";
    }

    args += " -vs d3d11videosink";

    if (cfg.audio_enabled) {
        args += " -as wasapisink";
    } else {
        args += " -a";
    }

    if (cfg.pin_enabled) {
        args += " -pin";
    }

    if (cfg.fullscreen) {
        args += " -fs";
    }

    return args;
}
