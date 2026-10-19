#include <cassert>
#include <cstdio>
#include <string>
#include "../src/ghost_core.h"

int main() {
    GhostState st;
    assert(st.phase == GhostPhase::Starting);

    // 0. LAN IPv4 extracted
    parse_uxplay_line(st, "mDNS advertising on LAN IPv4 192.168.1.163");
    assert(st.lan_ip == "192.168.1.163");
    assert(st.phase == GhostPhase::Waiting);

    // 1. Server initialized -> Waiting
    parse_uxplay_line(st, "using network ports UDP 7011 6001 6000 TCP 7100 7000 7001");
    assert(st.phase == GhostPhase::Waiting);

    // 2. PIN requested -> PairingPin with 4 digits extracted
    parse_uxplay_line(st, "*** CLIENT MUST NOW ENTER PIN = \"4829\" AS AIRPLAY PASSWORD");
    assert(st.phase == GhostPhase::PairingPin);
    assert(st.pin == "4829");

    // 3. Client connection request -> Connected with client name & model
    parse_uxplay_line(st, "connection request from Nat's iPhone (iPhone16,2) with deviceID = AA:BB:CC:DD:EE:FF");
    assert(st.phase == GhostPhase::Connected);
    assert(st.client_name == "Nat's iPhone");
    assert(st.client_model == "iPhone16,2");
    assert(st.pin.empty());

    // 4. Client disconnect -> returns to Waiting and clears client info
    parse_uxplay_line(st, "raop_rtp_mirror tcp socket was closed by client (recv returned 0)");
    assert(st.phase == GhostPhase::Waiting);
    assert(st.client_name.empty());

    // 5. Reconnect then network lost -> returns to Waiting
    parse_uxplay_line(st, "connection request from iPad Pro (iPad14,5) with deviceID = 11:22:33:44:55:66");
    assert(st.phase == GhostPhase::Connected);
    parse_uxplay_line(st, "*** ERROR lost connection with client (network problem?)");
    assert(st.phase == GhostPhase::Waiting);

    // 6. Fatal error line -> Error state
    parse_uxplay_line(st, "*** ERROR: dnssd_register_raop failed");
    assert(st.phase == GhostPhase::Error);

    // 7. Preset + Advance custom configuration (RES, FPS, CODEC, VSYNC)
    GhostConfig cfg;
    apply_preset(cfg, GhostPreset::LowLatency1080p60);
    std::string cmd0 = build_uxplay_args(cfg);
    assert(cmd0.find("-n \"Ghost\"") != std::string::npos);
    assert(cmd0.find("-nh") != std::string::npos);
    assert(cmd0.find("-vsync no") != std::string::npos);
    assert(cmd0.find("-s 1920x1080@60") != std::string::npos);
    assert(cmd0.find("-fps 60") != std::string::npos);
    assert(cmd0.find("-vs d3d11videosink") != std::string::npos);
    assert(cmd0.find("-as wasapisink") != std::string::npos);
    assert(cmd0.find("-h265") == std::string::npos);
    assert(cmd0.find("-fs") == std::string::npos);

    // Advance custom tweak: 2560x1440, 120 FPS, H.265, VSync ON, Audio OFF, PIN ON, Fullscreen ON
    cfg.preset = GhostPreset::Custom;
    cfg.res = GhostRes::Res1440p;
    cfg.fps = 120;
    cfg.codec = GhostCodec::H265;
    cfg.vsync = true;
    cfg.audio_enabled = false;
    cfg.pin_enabled = true;
    cfg.fullscreen = true;

    std::string cmdCustom = build_uxplay_args(cfg);
    assert(cmdCustom.find("-s 2560x1440@120") != std::string::npos);
    assert(cmdCustom.find("-fps 120") != std::string::npos);
    assert(cmdCustom.find("-h265") != std::string::npos);
    assert(cmdCustom.find("-vsync no") == std::string::npos);
    assert(cmdCustom.find("-a") != std::string::npos);
    assert(cmdCustom.find("-pin") != std::string::npos);
    assert(cmdCustom.find("-fs") != std::string::npos);

    printf("ALL PARSER & ADVANCE COMMAND BUILDER TESTS PASSED!\n");
    return 0;
}
