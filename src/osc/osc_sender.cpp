#include "osc_sender.h"

#include "osc_addresses.h"
#include "osc_analysis_fields.h"
#include "ui/smoothing/smoothing.h"

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

template <typename Data, std::size_t Size>
void appendFields(osc::OutboundPacketStream& packet, const Data& data,
                  const std::array<OSCFloatField<Data>, Size>& fields, bool source = false) {
    for (const auto& field : fields) appendFloatValue(packet, source ? field.sourceAddress : field.legacyAddress, data.*field.value);
}

void appendStringValue(osc::OutboundPacketStream& packet, const char* address, const char* value) {
    packet << osc::BeginMessage(address) << value << osc::EndMessage;
}

void appendNamedValueMessages(osc::OutboundPacketStream& packet, const OSCFrameData& frame) {
    appendStringValue(packet, kFrameStatusAddress, statusName(frame.status));
    appendInt32Value(packet, kFrameMetaSampleRateAddress, frame.meta.sampleRate);
    appendInt32Value(packet, kFrameMetaFftSizeAddress, frame.meta.fftSize);
    appendInt64Value(packet, kFrameMetaTimestampAddress, frame.meta.frameTimestamp);

    appendBoolValue(packet, kFrameSmoothingEnabledAddress, frame.smoothing.enabled);
    appendStringValue(packet, kFrameSmoothingModeAddress, smoothingMode(frame.smoothing));
    appendFloatValue(packet, kFrameSmoothingAmountAddress, frame.smoothing.amount);
    appendFloatValue(packet, kFrameSmoothingManualResponseAddress, manualResponseTime(frame.smoothing.amount));
    appendFloatValue(packet, kFrameSmoothingResponseAddress, frame.smoothing.responseSeconds);
    appendFloatValue(packet, kFrameSmoothingMinimumResponseAddress, frame.smoothing.minimumResponseSeconds);
    appendFloatValue(packet, kFrameSmoothingMaximumResponseAddress, frame.smoothing.maximumResponseSeconds);

    appendBoolValue(packet, kFrameSourceAnalysisAvailableAddress, frame.sourceAnalysis.available);
    appendFields(packet, frame.signal, signalFields, true);
    appendFields(packet, frame.spectral, spectralFields, true);
    appendFields(packet, frame.loudness, loudnessFields, true);
    appendFields(packet, frame.phase, phaseFields, true);
    for (const auto& field : colourAnalysisFields) appendFloatValue(packet, field.sourceAddress, frame.sourceAnalysis.*field.value);

    appendBoolValue(packet, kFramePresentedAnalysisAvailableAddress, frame.presentedAnalysis.available);
    for (const auto& field : colourAnalysisFields) appendFloatValue(packet, field.presentedAddress, frame.presentedAnalysis.*field.value);

    appendFloatValue(packet, kFrameColourDisplayRAddress, frame.colour.displayR);
    appendFloatValue(packet, kFrameColourDisplayGAddress, frame.colour.displayG);
    appendFloatValue(packet, kFrameColourDisplayBAddress, frame.colour.displayB);
    appendFloatValue(packet, kFrameColourCIEXAddress, frame.colour.cieX);
    appendFloatValue(packet, kFrameColourCIEYAddress, frame.colour.cieY);
    appendFloatValue(packet, kFrameColourCIEZAddress, frame.colour.cieZ);
    appendFloatValue(packet, kFrameMeasurementCIE2015XAddress, frame.colour.measurementXYZ.X);
    appendFloatValue(packet, kFrameMeasurementCIE2015YAddress, frame.colour.measurementXYZ.Y);
    appendFloatValue(packet, kFrameMeasurementCIE2015ZAddress, frame.colour.measurementXYZ.Z);
    appendBoolValue(packet, kFrameMeasurementAvailableAddress, frame.colour.measurementAvailable);
    appendFloatValue(packet, kFrameColourOklabLAddress, frame.colour.oklabL);
    appendFloatValue(packet, kFrameColourOklabAAddress, frame.colour.oklabA);
    appendFloatValue(packet, kFrameColourOklabBAddress, frame.colour.oklabB);

    appendFields(packet, frame.signal, signalFields);
    appendFields(packet, frame.spectral, spectralFields);
    appendFields(packet, frame.loudness, loudnessFields);
    appendFields(packet, frame.phase, phaseFields);
    appendFloatValue(packet, kFrameAnalysisSpectralFluxAddress, frame.spectral.spectralFlux);
    appendBoolValue(packet, kFrameAnalysisOnsetDetectedAddress, frame.transient.onsetDetected);
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
