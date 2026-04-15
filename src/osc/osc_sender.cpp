#include "osc_sender.h"

#include "osc_addresses.h"

#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"
#include "osc/OscTypes.h"

namespace Synesthesia::OSC {

namespace {

void appendFloatValue(osc::OutboundPacketStream& packet, const char* address, const float value) {
    packet << osc::BeginMessage(address)
           << value
           << osc::EndMessage;
}

void appendInt32Value(osc::OutboundPacketStream& packet, const char* address, const int32_t value) {
    packet << osc::BeginMessage(address)
           << static_cast<osc::int32>(value)
           << osc::EndMessage;
}

void appendInt64Value(osc::OutboundPacketStream& packet, const char* address, const int64_t value) {
    packet << osc::BeginMessage(address)
           << static_cast<osc::int64>(value)
           << osc::EndMessage;
}

void appendBoolValue(osc::OutboundPacketStream& packet, const char* address, const bool value) {
    packet << osc::BeginMessage(address)
           << value
           << osc::EndMessage;
}

void appendNamedValueMessages(osc::OutboundPacketStream& packet, const OSCFrameData& frame) {
    appendInt32Value(packet, kFrameMetaSampleRateAddress, frame.meta.sampleRate);
    appendInt32Value(packet, kFrameMetaFftSizeAddress, frame.meta.fftSize);
    appendInt64Value(packet, kFrameMetaTimestampAddress, frame.meta.frameTimestamp);

    appendFloatValue(packet, kFrameSignalDominantFrequencyAddress, frame.signal.dominantFrequencyHz);
    appendFloatValue(packet, kFrameSignalDominantWavelengthAddress, frame.signal.dominantWavelengthNm);
    appendFloatValue(packet, kFrameSignalVisualiserMagnitudeAddress, frame.signal.visualiserMagnitude);
    appendFloatValue(packet, kFrameSignalPhaseRadiansAddress, frame.signal.phaseRadians);

    appendFloatValue(packet, kFrameColourDisplayRAddress, frame.colour.displayR);
    appendFloatValue(packet, kFrameColourDisplayGAddress, frame.colour.displayG);
    appendFloatValue(packet, kFrameColourDisplayBAddress, frame.colour.displayB);
    appendFloatValue(packet, kFrameColourCIEXAddress, frame.colour.cieX);
    appendFloatValue(packet, kFrameColourCIEYAddress, frame.colour.cieY);
    appendFloatValue(packet, kFrameColourCIEZAddress, frame.colour.cieZ);
    appendFloatValue(packet, kFrameColourOklabLAddress, frame.colour.oklabL);
    appendFloatValue(packet, kFrameColourOklabAAddress, frame.colour.oklabA);
    appendFloatValue(packet, kFrameColourOklabBAddress, frame.colour.oklabB);

    appendFloatValue(packet, kFrameAnalysisSpectralFlatnessAddress, frame.spectral.flatness);
    appendFloatValue(packet, kFrameAnalysisSpectralCentroidAddress, frame.spectral.centroidHz);
    appendFloatValue(packet, kFrameAnalysisSpectralSpreadAddress, frame.spectral.spreadHz);
    appendFloatValue(packet, kFrameAnalysisSpectralSpreadNormalisedAddress, frame.spectral.normalisedSpread);
    appendFloatValue(packet, kFrameAnalysisSpectralRolloffAddress, frame.spectral.rolloffHz);
    appendFloatValue(packet, kFrameAnalysisSpectralCrestAddress, frame.spectral.crestFactor);
    appendFloatValue(packet, kFrameAnalysisSpectralFluxAddress, frame.spectral.spectralFlux);

    appendFloatValue(packet, kFrameAnalysisLoudnessDbAddress, frame.loudness.loudnessDb);
    appendFloatValue(packet, kFrameAnalysisLoudnessNormalisedAddress, frame.loudness.loudnessNormalised);
    appendFloatValue(packet, kFrameAnalysisFrameLoudnessDbAddress, frame.loudness.frameLoudnessDb);
    appendFloatValue(packet, kFrameAnalysisMomentaryLoudnessAddress, frame.loudness.momentaryLoudnessLUFS);
    appendFloatValue(packet, kFrameAnalysisEstimatedSPLAddress, frame.loudness.estimatedSPL);
    appendFloatValue(packet, kFrameAnalysisLuminanceAddress, frame.loudness.luminanceCdM2);
    appendFloatValue(packet, kFrameAnalysisBrightnessNormalisedAddress, frame.loudness.brightnessNormalised);

    appendFloatValue(packet, kFrameAnalysisTransientMixAddress, frame.transient.transientMix);
    appendBoolValue(packet, kFrameAnalysisOnsetDetectedAddress, frame.transient.onsetDetected);

    appendFloatValue(packet, kFrameAnalysisPhaseInstabilityAddress, frame.phase.instabilityNorm);
    appendFloatValue(packet, kFrameAnalysisPhaseCoherenceAddress, frame.phase.coherenceNorm);
    appendFloatValue(packet, kFrameAnalysisPhaseTransientAddress, frame.phase.transientNorm);

    appendBoolValue(packet, kFrameSmoothingOnsetDetectedAddress, frame.smoothing.onsetDetected);
    appendFloatValue(packet, kFrameSmoothingSpectralFluxAddress, frame.smoothing.spectralFlux);
    appendFloatValue(packet, kFrameSmoothingSpectralFlatnessAddress, frame.smoothing.spectralFlatness);
    appendFloatValue(packet, kFrameSmoothingLoudnessNormalisedAddress, frame.smoothing.loudnessNormalised);
    appendFloatValue(packet, kFrameSmoothingBrightnessNormalisedAddress, frame.smoothing.brightnessNormalised);
    appendFloatValue(packet, kFrameSmoothingSpectralSpreadAddress, frame.smoothing.spectralSpreadNorm);
    appendFloatValue(packet, kFrameSmoothingSpectralRolloffAddress, frame.smoothing.spectralRolloffNorm);
    appendFloatValue(packet, kFrameSmoothingSpectralCrestAddress, frame.smoothing.spectralCrestNorm);
    appendFloatValue(packet, kFrameSmoothingPhaseInstabilityAddress, frame.smoothing.phaseInstabilityNorm);
    appendFloatValue(packet, kFrameSmoothingPhaseCoherenceAddress, frame.smoothing.phaseCoherenceNorm);
    appendFloatValue(packet, kFrameSmoothingPhaseTransientAddress, frame.smoothing.phaseTransientNorm);
}

}

OSCSender::~OSCSender() = default;

bool OSCSender::configure(const OSCConfig& config, std::string& errorMessage) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto destination = validateOSCDestination(config.destinationHost);
    if (!destination.valid) {
        socket_.reset();
        buffer_.clear();
        errorMessage = destination.errorMessage;
        return false;
    }

    try {
        config_ = config;
        config_.destinationHost = destination.canonicalHost;
        buffer_.assign(config.outputBufferSize, '\0');
        socket_ = std::make_unique<UdpTransmitSocket>(
            IpEndpointName(static_cast<unsigned long>(destination.address), static_cast<int>(config_.transmitPort))
        );
    } catch (const std::exception& exception) {
        socket_.reset();
        buffer_.clear();
        errorMessage = exception.what();
        return false;
    } catch (...) {
        socket_.reset();
        buffer_.clear();
        errorMessage = "Failed to create OSC transmit socket";
        return false;
    }

    errorMessage.clear();
    return true;
}

void OSCSender::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_.reset();
    buffer_.clear();
}

bool OSCSender::sendFrame(const OSCFrameData& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!socket_ || buffer_.empty()) {
        return false;
    }

    try {
        osc::OutboundPacketStream packet(buffer_.data(), buffer_.size());
        packet << osc::BeginBundleImmediate;
        appendNamedValueMessages(packet, frame);
        packet << osc::EndBundle;

        socket_->Send(packet.Data(), packet.Size());
    } catch (...) {
        return false;
    }

    return true;
}

}
