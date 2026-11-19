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

    // 5. Reconnect then network lost / clean mirror stop / recv error -> returns to Waiting (NOT fatal Error!)
    parse_uxplay_line(st, "connection request from iPad Pro (iPad14,5) with deviceID = 11:22:33:44:55:66");
    assert(st.phase == GhostPhase::Connected);
    parse_uxplay_line(st, "*** ERROR:   3 seconds since last client feedback request (expected every two seconds); client may be offline");
    assert(st.phase == GhostPhase::Connected); // transient warning must not trigger Error
    parse_uxplay_line(st, "raop_rtp_mirror->running is no longer true");
    assert(st.phase == GhostPhase::Waiting);
    assert(st.client_name.empty());

    parse_uxplay_line(st, "connection request from iPhone (iPhone16,2) with deviceID = AA:BB:CC:DD:EE:FF");
    assert(st.phase == GhostPhase::Connected);
    parse_uxplay_line(st, "*** ERROR: raop_rtp_mirror error in recv: 10054 Connection reset by peer");
    assert(st.phase == GhostPhase::Waiting); // disconnect recv error must return to Waiting, not Error!

    parse_uxplay_line(st, "connection request from MacBook (Mac15,6) with deviceID = 22:33:44:55:66:77");
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

    // 8. UI helpers, preset sync & exponential easing sanity checks
    GhostConfig syncCfg;
    assert(syncCfg.dark_mode == false);
    assert(syncCfg.motion_enabled == true);
    syncCfg.res = GhostRes::Res4K;
    syncCfg.fps = 60;
    syncCfg.codec = GhostCodec::H265;
    syncCfg.vsync = false;
    sync_preset_from_config(syncCfg);
    assert(syncCfg.preset == GhostPreset::Ultra4K60H265);
    syncCfg.fps = 120;
    sync_preset_from_config(syncCfg);
    assert(syncCfg.preset == GhostPreset::Custom);

    assert(std::wstring(res_short_label(GhostRes::Res4K)) == L"4K UHD");
    assert(std::wstring(codec_short_label(GhostCodec::H265)) == L"H.265");
    assert(ease_out_expo(-0.5f) == 0.0f);
    assert(ease_out_expo(0.0f) == 0.0f);
    assert(ease_out_expo(1.0f) == 1.0f);
    assert(ease_out_expo(1.5f) == 1.0f);
    assert(ease_out_expo(0.5f) > 0.95f && ease_out_expo(0.5f) < 1.0f);

    printf("ALL PARSER & ADVANCE COMMAND BUILDER TESTS PASSED!\n");
    return 0;
}
