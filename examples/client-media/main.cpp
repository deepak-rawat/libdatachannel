// client-media: WebRTC client for H264 + audio + data channel
// Usage: sender/receiver mode, file-based media IO
#include <iostream>
#include <string>
#include <memory>
#include "rtc/rtc.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: client-media <sender|receiver> [options]\n";
        return 1;
    }
    std::string mode = argv[1];
    // --- Common signaling setup ---
    std::string localId = "media" + std::to_string(rand() % 10000);
    std::string wsUrl = "ws://localhost:8000/" + localId; // Change as needed
    auto ws = std::make_shared<rtc::WebSocket>();
    ws->onOpen([]() { std::cout << "WebSocket connected, signaling ready\n"; });
    ws->onError([](std::string s) { std::cout << "WebSocket error: " << s << std::endl; });
    ws->onClosed([]() { std::cout << "WebSocket closed" << std::endl; });

    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");

    std::shared_ptr<rtc::PeerConnection> pc;
    ws->onMessage([&](auto data) {
        if (!std::holds_alternative<std::string>(data)) return;
        auto msg = nlohmann::json::parse(std::get<std::string>(data));
        if (!msg.contains("id") || !msg.contains("type")) return;
        std::string id = msg["id"], type = msg["type"];
        if (!pc) pc = std::make_shared<rtc::PeerConnection>(config);
        if (type == "offer" || type == "answer") {
            pc->setRemoteDescription(rtc::Description(msg["description"], type));
        } else if (type == "candidate") {
            pc->addRemoteCandidate(rtc::Candidate(msg["candidate"], msg["mid"]));
        }
    });
    ws->open(wsUrl);

    if (mode == "sender") {
        std::cout << "Running in sender mode...\n";
        std::string h264File = "input.h264", pcmFile = "input.pcm";
        FILE* h264 = fopen(h264File.c_str(), "rb");
        if (!h264) { std::cerr << "Failed to open H264 file\n"; return 1; }
        FILE* pcm = fopen(pcmFile.c_str(), "rb");
        if (!pcm) { std::cerr << "Failed to open PCM file\n"; fclose(h264); return 1; }

        // Create H264 video track
        rtc::Description::Video videoDesc("video", rtc::Description::Direction::SendOnly);
        videoDesc.addH264Codec(96); // payload type 96
        videoDesc.setBitrate(2000); // kbps
        videoDesc.addSSRC(1234, "video-send");
        auto videoTrack = pc->addTrack(videoDesc);

    // Create PCM audio track
    rtc::Description::Audio audioDesc("audio", rtc::Description::Direction::SendOnly);
    audioDesc.addPCMUCodec(0); // payload type 0 (PCMU)
    audioDesc.setBitrate(128); // kbps
    audioDesc.addSSRC(5678, "audio-send");
    auto audioTrack = pc->addTrack(audioDesc);

        // Create data channel
        auto dc = pc->createDataChannel("chat");
        dc->onOpen([]() { std::cout << "Data channel open\n"; });
        dc->onMessage([](auto data) {
            if (std::holds_alternative<std::string>(data))
                std::cout << "Received: " << std::get<std::string>(data) << std::endl;
        });

        // Send H264 frames
        std::vector<uint8_t> h264Buf(1400);
        while (!feof(h264)) {
            size_t n = fread(h264Buf.data(), 1, h264Buf.size(), h264);
            if (n > 0) videoTrack->send(reinterpret_cast<const std::byte *>(h264Buf.data()), n);
        }

        // Send PCM frames
        std::vector<uint8_t> pcmBuf(160);
        while (!feof(pcm)) {
            size_t n = fread(pcmBuf.data(), 1, pcmBuf.size(), pcm);
            if (n > 0) audioTrack->send(reinterpret_cast<const std::byte *>(pcmBuf.data()), n);
        }

        fclose(h264);
        fclose(pcm);
        std::cout << "Media sent.\n";
    } else if (mode == "receiver") {
        std::cout << "Running in receiver mode...\n";
        std::string h264OutFile = "output.h264", pcmOutFile = "output.pcm";
        FILE* h264Out = fopen(h264OutFile.c_str(), "wb");
        if (!h264Out) { std::cerr << "Failed to open H264 output file\n"; return 1; }
        FILE* pcmOut = fopen(pcmOutFile.c_str(), "wb");
        if (!pcmOut) { std::cerr << "Failed to open PCM output file\n"; fclose(h264Out); return 1; }

        // Create H264 video track
        rtc::Description::Video videoDesc("video", rtc::Description::Direction::RecvOnly);
        videoDesc.addH264Codec(96);
        videoDesc.setBitrate(2000);
        videoDesc.addSSRC(1234, "video-send");
        auto videoTrack = pc->addTrack(videoDesc);

    // Create PCM audio track
    rtc::Description::Audio audioDesc("audio", rtc::Description::Direction::RecvOnly);
    audioDesc.addPCMUCodec(0);
    audioDesc.setBitrate(128);
    audioDesc.addSSRC(5678, "audio-send");
    auto audioTrack = pc->addTrack(audioDesc);

        // Create data channel
        auto dc = pc->createDataChannel("chat");
        dc->onOpen([]() { std::cout << "Data channel open\n"; });
        dc->onMessage([](auto data) {
            if (std::holds_alternative<std::string>(data))
                std::cout << "Received: " << std::get<std::string>(data) << std::endl;
        });

        // Receive H264 frames
        videoTrack->onFrame([h264Out](rtc::binary data, rtc::FrameInfo) {
            fwrite(data.data(), 1, data.size(), h264Out);
        });

        // Receive PCM frames
        audioTrack->onFrame([pcmOut](rtc::binary data, rtc::FrameInfo) {
            fwrite(data.data(), 1, data.size(), pcmOut);
        });

        std::cout << "Receiving media. Press Ctrl+C to exit.\n";
        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
        fclose(h264Out);
        fclose(pcmOut);
    } else {
        std::cout << "Unknown mode: " << mode << "\n";
        return 1;
    }
    // TODO: Setup signaling, data channel, media channels
    return 0;
}
