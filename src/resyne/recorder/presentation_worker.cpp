#include "resyne/recorder/presentation_worker.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "resyne/recorder/recorder.h"
#include "resyne/encoding/formats/rsyn_presentation.h"
#include "resyne/encoding/formats/format_rsyn.h"

namespace ReSyne {
namespace {

std::atomic<std::uint64_t> snapshotRevision{0};

bool sameAnalysis(const RSYNPresentationSettings& a, const RSYNPresentationSettings& b) {
    return a.lowGain == b.lowGain && a.midGain == b.midGain && a.highGain == b.highGain;
}

bool sameSmoothing(const RSYNPresentationSettings& a, const RSYNPresentationSettings& b) {
    return a.smoothingEnabled == b.smoothingEnabled && a.manualSmoothing == b.manualSmoothing &&
        (!a.manualSmoothing || a.smoothingAmount == b.smoothingAmount);
}

RSYNPresentationSettings settingsFor(const RecorderState& state) {
    RSYNPresentationSettings settings;
    settings.colourSpace = state.importColourSpace;
    settings.applyGamutMapping = state.importGamutMapping;
    settings.lowGain = state.importLowGain;
    settings.midGain = state.importMidGain;
    settings.highGain = state.importHighGain;
    settings.smoothingEnabled = state.presentationSmoothingEnabled;
    settings.manualSmoothing = state.presentationManualSmoothing;
    settings.smoothingAmount = state.presentationSmoothingAmount;
    return settings;
}

ColourPresentation::TrackPoint trackPoint(const RSYNPresentationFrame& frame, bool smoothed) {
    ColourPresentation::TrackPoint point{frame.timestamp};
    const auto xyz = smoothed ? frame.presentedXYZ : std::array<float, 3>{frame.analysis.X, frame.analysis.Y, frame.analysis.Z};
    point.colour = ColourPresentation::toColourTriple(xyz);
    point.responseSeconds = smoothed ? frame.responseSeconds : 0.0f;
    return point;
}

}

void PresentationAnalysis::append(const RSYNPresentationFrame& frame) {
    if (blocks_.empty() || blocks_.back()->size() == blockSize)
        blocks_.push_back(std::make_shared<std::vector<RSYNPresentationFrame>>());
    if (blocks_.back().use_count() > 1)
        blocks_.back() = std::make_shared<std::vector<RSYNPresentationFrame>>(*blocks_.back());
    std::const_pointer_cast<std::vector<RSYNPresentationFrame>>(blocks_.back())->push_back(frame);
    ++size_;
}

const RSYNPresentationFrame& PresentationAnalysis::operator[](std::size_t index) const {
    return (*blocks_[index / blockSize])[index % blockSize];
}

std::size_t PresentationAnalysis::indexAt(double time) const {
    std::size_t first = 0, last = size_;
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if ((*this)[middle].timestamp <= time) first = middle + 1;
        else last = middle;
    }
    return first == 0 ? 0 : first - 1;
}

ColourPresentation::SpectralContributions PresentationAnalysis::contributionsAt(double time) const {
    if (empty()) return {};
    if (!std::isfinite(time)) time = 0.0;
    const auto index = indexAt(time);
    const auto& left = (*this)[index];
    const auto& right = (*this)[std::min(index + 1, size() - 1)];
    if (!left.presentedContributions) return {};
    if (!right.presentedContributions || right.timestamp <= left.timestamp) return *left.presentedContributions;
    const float fraction = static_cast<float>(std::clamp((time - left.timestamp) / (right.timestamp - left.timestamp), 0.0, 1.0));
    std::array<ColourPresentation::SpectralContribution, ColourPresentation::contributionBandCount> values{};
    for (std::size_t i = 0; i < values.size(); ++i) values[i].band = static_cast<std::uint16_t>(i);
    for (const auto& band : *left.presentedContributions) {
        for (std::size_t c = 0; c < 3; ++c) values[band.band].xyz[c] += (1.0f - fraction) * band.xyz[c];
        values[band.band].magnitude += (1.0f - fraction) * band.magnitude;
        values[band.band].responseSeconds += (1.0f - fraction) * band.responseSeconds;
    }
    for (const auto& band : *right.presentedContributions) {
        for (std::size_t c = 0; c < 3; ++c) values[band.band].xyz[c] += fraction * band.xyz[c];
        values[band.band].magnitude += fraction * band.magnitude;
        values[band.band].responseSeconds += fraction * band.responseSeconds;
    }
    return {values.begin(), values.end()};
}

struct PresentationWorker::Impl {
    struct Request {
        std::uint64_t serial = 0;
        std::uint64_t generation = 0;
        std::uint64_t sourceRevision = 0;
        std::uint64_t retainedPreviewGeneration = 0;
        bool preview = false;
        std::size_t count = 0;
        std::size_t availableFrames = 0;
        AudioMetadata metadata;
        RSYNPresentationSettings settings;
    };
    RecorderState& owner;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    Request latest;
    std::shared_ptr<const PresentationSnapshot> published;
    std::uint64_t publishedSerial = 0;
    std::shared_ptr<PresentationSnapshot> importHandover;
    std::uint64_t retainedPreviewGeneration = 0;
    std::thread thread;
    std::uint64_t cachedGeneration = 0;
    std::uint64_t cachedSourceRevision = 0;
    bool cachedPreview = false;
    RSYNPresentationSettings analysisSettings;
    RSYNPresentationSettings smoothingSettings;
    RSYNPresentationSettings projectionSettings;
    PresentationAnalysis analysed;
    PresentationAnalysis presented;
    std::unique_ptr<RSYNPresentation::ReplayState> analysisReplay;
    std::unique_ptr<RSYNPresentation::ReplayState> colourReplay;
    ColourPresentation::ColourTrack colours;
    ColourPresentation::ColourTrack targets;
    std::size_t rendered = 0;

    explicit Impl(RecorderState& state) : owner(state), thread([this] { run(); }) {}
    ~Impl() {
        { std::lock_guard lock(mutex); stopping = true; }
        wake.notify_one();
        if (thread.joinable()) thread.join();
    }

    bool canContinue(const Request& request) const {
        return latest.generation == request.generation && latest.sourceRevision == request.sourceRevision &&
            latest.preview == request.preview &&
            latest.count >= request.count && latest.availableFrames >= request.availableFrames &&
            sameAnalysis(latest.settings, request.settings) &&
            sameSmoothing(latest.settings, request.settings) &&
            latest.settings.colourSpace == request.settings.colourSpace &&
            latest.settings.applyGamutMapping == request.settings.applyGamutMapping;
    }

    bool cancelled(const Request& request) {
        std::lock_guard lock(mutex);
        return stopping || !canContinue(request);
    }

    void publish(const Request& request, const std::string& message = {}) {
        {
            std::lock_guard lock(mutex);
            if (stopping || !canContinue(request)) return;
            if (published && published->sourceGeneration == request.generation &&
                published->preview == request.preview &&
                rendered < published->processedFrames) return;
            if (importHandover && !request.preview && importHandover->sourceGeneration == request.generation &&
                rendered < importHandover->processedFrames) return;
        }
        auto snapshot = std::make_shared<PresentationSnapshot>();
        colours.prepareProjection(ColourCore::ColourSpace::SRGB, request.settings.applyGamutMapping);
        snapshot->colours = colours.snapshot();
        snapshot->targets = targets.snapshot();
        snapshot->analysis = std::make_shared<PresentationAnalysis>(presented);
        snapshot->settings = request.settings;
        snapshot->sourceGeneration = request.generation;
        snapshot->sourceRevision = request.sourceRevision;
        snapshot->preview = request.preview;
        snapshot->revision = ++snapshotRevision;
        snapshot->processedFrames = rendered;
        snapshot->sourceFrames = request.count;
        snapshot->duration = RSYNPresentation::durationSeconds(request.metadata, colours.endTime());
        snapshot->message = message;
        std::lock_guard lock(mutex);
        if (!stopping && canContinue(request)) {
            snapshot->sourceFrames = latest.count;
            snapshot->duration = RSYNPresentation::durationSeconds(latest.metadata, colours.endTime());
            published = std::move(snapshot);
            publishedSerial = request.serial;
        }
    }

    void fallback(const Request& request, const std::string& message) {
        std::shared_ptr<PresentationSnapshot> snapshot;
        {
            std::lock_guard lock(mutex);
            if (published && published->sourceGeneration == request.generation && published->preview == request.preview)
                snapshot = std::make_shared<PresentationSnapshot>(*published);
        }
        if (request.metadata.presentationData &&
            (!snapshot || snapshot->processedFrames < request.metadata.presentationData->frames.size() ||
                (snapshot->settings.smoothingEnabled != request.settings.smoothingEnabled &&
                    request.metadata.presentationData->settings.smoothingEnabled == request.settings.smoothingEnabled))) {
            snapshot = std::make_shared<PresentationSnapshot>();
            const auto& stored = *request.metadata.presentationData;
            snapshot->colours = RSYNPresentation::colourTrack(stored, stored.settings.smoothingEnabled);
            snapshot->targets = RSYNPresentation::colourTrack(stored, false);
            auto analysis = std::make_shared<PresentationAnalysis>();
            for (const auto& frame : stored.frames) analysis->append(frame);
            snapshot->analysis = std::move(analysis);
            snapshot->settings = stored.settings;
            snapshot->sourceGeneration = request.generation;
            snapshot->preview = request.preview;
            snapshot->processedFrames = stored.frames.size();
            snapshot->sourceFrames = request.count;
            snapshot->duration = RSYNPresentation::durationSeconds(request.metadata, snapshot->colours->endTime());
        }
        if (!snapshot) {
            snapshot = std::make_shared<PresentationSnapshot>();
            snapshot->colours = std::make_shared<ColourPresentation::ColourTrack>();
            snapshot->targets = std::make_shared<ColourPresentation::ColourTrack>();
            snapshot->settings = request.settings;
            snapshot->sourceGeneration = request.generation;
            snapshot->preview = request.preview;
            snapshot->sourceFrames = request.count;
            snapshot->duration = RSYNPresentation::durationSeconds(request.metadata);
        }
        snapshot->message = message;
        if (!request.settings.smoothingEnabled && snapshot->targets && snapshot->targets->size() > 0 &&
            sameAnalysis(snapshot->settings, request.settings)) {
            snapshot->colours = snapshot->targets;
            if (snapshot->analysis && snapshot->settings.smoothingEnabled) {
                auto analysis = std::make_shared<PresentationAnalysis>();
                RSYNPresentation::ReplayState replay(request.settings);
                for (std::size_t i = 0; i < snapshot->analysis->size(); ++i)
                    analysis->append(replay.processAnalysed((*snapshot->analysis)[i]));
                snapshot->analysis = std::move(analysis);
            }
            snapshot->settings = request.settings;
            snapshot->message.clear();
        }
        snapshot->sourceRevision = request.sourceRevision;
        snapshot->revision = ++snapshotRevision;
        std::lock_guard lock(mutex);
        if (!stopping && latest.serial == request.serial) {
            published = std::move(snapshot);
            publishedSerial = request.serial;
        }
    }

    void build(const Request& request) {
        if (cachedPreview && !request.preview && request.retainedPreviewGeneration == cachedGeneration) {
            cachedGeneration = request.generation;
            cachedPreview = false;
        }
        const bool newSource = cachedGeneration != request.generation || cachedPreview != request.preview ||
            cachedSourceRevision != request.sourceRevision;
        if (newSource || !sameAnalysis(analysisSettings, request.settings)) {
            analysed = {};
            analysisSettings = request.settings;
            auto rawSettings = request.settings;
            rawSettings.smoothingEnabled = false;
            rawSettings.colourSpace = ColourCore::ColourSpace::Rec2020;
            analysisReplay = std::make_unique<RSYNPresentation::ReplayState>(rawSettings);
            cachedGeneration = request.generation;
            cachedSourceRevision = request.sourceRevision;
            cachedPreview = request.preview;
            colourReplay.reset();
        }
        if (!colourReplay || !sameSmoothing(smoothingSettings, request.settings)) {
            smoothingSettings = request.settings;
            colourReplay = std::make_unique<RSYNPresentation::ReplayState>(request.settings);
            colours = ColourPresentation::ColourTrack();
            targets = ColourPresentation::ColourTrack();
            rendered = 0;
            presented = {};
        }

        colourReplay->setOutputSettings(request.settings.colourSpace, request.settings.applyGamutMapping);
        if (projectionSettings.colourSpace != request.settings.colourSpace ||
            projectionSettings.applyGamutMapping != request.settings.applyGamutMapping) {
            PresentationAnalysis reprojected;
            for (std::size_t i = 0; i < presented.size(); ++i) {
                if ((i % 64) == 0 && cancelled(request)) return;
                auto frame = presented[i];
                const auto raw = ColourPresentation::projectXYZ({static_cast<double>(frame.analysis.X),
                    static_cast<double>(frame.analysis.Y), static_cast<double>(frame.analysis.Z)},
                    request.settings.colourSpace, request.settings.applyGamutMapping);
                frame.analysis.r = raw.r; frame.analysis.g = raw.g; frame.analysis.b = raw.b;
                const auto output = ColourPresentation::projectXYZ(ColourPresentation::toColourTriple(frame.presentedXYZ),
                    request.settings.colourSpace, request.settings.applyGamutMapping);
                frame.smoothedDisplayRgb = {output.r, output.g, output.b};
                reprojected.append(frame);
            }
            presented = std::move(reprojected);
            projectionSettings = request.settings;
        }

        auto lastPublish = std::chrono::steady_clock::now();
        bool publishedChunk = false;
        std::vector<AudioColourSample> hydrated;
        while (rendered < request.count) {
            if (cancelled(request)) return;
            if (rendered >= analysed.size()) {
                std::vector<AudioColourSample> chunk;
                {
                    std::lock_guard lock(owner.samplesMutex);
                    const auto generation = request.preview ? owner.presentationPreviewGeneration : owner.presentationSourceGeneration;
                    const auto sourceRevision = request.preview ? owner.presentationPreviewRevision : owner.presentationSourceRevision;
                    if (generation != request.generation || sourceRevision != request.sourceRevision) return;
                    const auto& source = request.preview ? owner.previewSamples : owner.samples;
                    if (analysed.size() < source.size()) {
                        const auto end = std::min({source.size(), request.count, analysed.size() + 16});
                        chunk.assign(source.begin() + analysed.size(), source.begin() + end);
                    }
                }
                if (chunk.empty() && !request.preview && request.metadata.lazyAsset) {
                    auto metadata = request.metadata;
                    if (hydrated.empty() && !SequenceExporterInternal::hydrateRsynSamples(metadata, hydrated,
                        [this, &request](float) { if (cancelled(request)) throw std::runtime_error("Presentation superseded"); })) {
                        fallback(request, "Archived spectra could not be loaded; showing the available presentation.");
                        return;
                    }
                    if (cancelled(request)) return;
                    if (analysed.size() < hydrated.size()) {
                        const auto end = std::min({hydrated.size(), request.count, analysed.size() + 16});
                        chunk.assign(hydrated.begin() + analysed.size(), hydrated.begin() + end);
                    }
                }
                if (chunk.empty()) {
                    const bool awaitingImport = request.preview && request.metadata.presentationData &&
                        request.availableFrames < request.count;
                    fallback(request, awaitingImport ? "" :
                        "This presentation has no spectra for the requested adjustment.");
                    return;
                }
                for (const auto& sample : chunk) analysed.append(analysisReplay->analyse(sample));
            }
            const auto end = std::min({analysed.size(), request.count, rendered + 16});
            for (; rendered < end; ++rendered) {
                auto frame = colourReplay->processAnalysed(analysed[rendered]);
                targets.append(trackPoint(frame, false));
                colours.append(trackPoint(frame, request.settings.smoothingEnabled));
                presented.append(frame);
            }
            const auto now = std::chrono::steady_clock::now();
            if (!publishedChunk || now - lastPublish >= std::chrono::milliseconds(8)) {
                publish(request);
                lastPublish = std::chrono::steady_clock::now();
                publishedChunk = true;
            }
        }
        publish(request);
    }

    void run() {
        std::uint64_t handled = 0;
        for (;;) {
            Request request;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || latest.serial != handled; });
                if (stopping) return;
                request = latest;
                handled = request.serial;
            }
            try { build(request); }
            catch (const std::exception& error) {
                if (!cancelled(request)) fallback(request, error.what());
            }
        }
    }
};

PresentationWorker::PresentationWorker(RecorderState& state) : impl_(std::make_unique<Impl>(state)) {}
PresentationWorker::~PresentationWorker() = default;

void PresentationWorker::retainImportPreview(std::uint64_t previewGeneration, std::uint64_t sourceGeneration) {
    std::lock_guard lock(impl_->mutex);
    impl_->importHandover.reset();
    impl_->retainedPreviewGeneration = previewGeneration;
    const auto& preview = impl_->published;
    if (preview && preview->preview && preview->sourceGeneration == previewGeneration) {
        impl_->importHandover = std::make_shared<PresentationSnapshot>(*preview);
        impl_->importHandover->sourceGeneration = sourceGeneration;
        impl_->importHandover->preview = false;
        impl_->importHandover->revision = ++snapshotRevision;
    }
}

std::shared_ptr<const PresentationSnapshot> PresentationWorker::request() {
    Impl::Request request;
    {
        std::lock_guard lock(impl_->owner.samplesMutex);
        const auto& state = impl_->owner;
        request.preview = state.importPhase == 3;
        request.generation = request.preview ? state.presentationPreviewGeneration : state.presentationSourceGeneration;
        request.sourceRevision = request.preview ? state.presentationPreviewRevision : state.presentationSourceRevision;
        request.metadata = state.metadata;
        if (request.preview) request.metadata = state.previewMetadata;
        request.settings = settingsFor(state);
        if (request.preview && request.metadata.presentationData) {
            const auto& stored = request.metadata.presentationData->settings;
            request.settings.colourSpace = stored.colourSpace;
            request.settings.applyGamutMapping = stored.applyGamutMapping;
            request.settings.smoothingEnabled = stored.smoothingEnabled;
            request.settings.manualSmoothing = stored.manualSmoothing;
            request.settings.smoothingAmount = stored.smoothingAmount;
        }
        request.count = request.preview ? state.previewSamples.size() : state.samples.size();
        request.availableFrames = request.count;
        if (request.metadata.presentationData &&
            sameAnalysis(request.metadata.presentationData->settings, request.settings))
            request.count = std::max(request.count, request.metadata.presentationData->frames.size());
    }
    std::lock_guard lock(impl_->mutex);
    if (impl_->importHandover && !request.preview &&
        impl_->importHandover->sourceGeneration == request.generation)
        request.retainedPreviewGeneration = impl_->retainedPreviewGeneration;
    const auto& previous = impl_->latest;
    if (previous.serial == 0 || request.generation != previous.generation || request.preview != previous.preview ||
        request.sourceRevision != previous.sourceRevision || request.count != previous.count ||
        request.availableFrames != previous.availableFrames || !sameAnalysis(request.settings, previous.settings) ||
        !sameSmoothing(request.settings, previous.settings) ||
        request.settings.colourSpace != previous.settings.colourSpace ||
        request.settings.applyGamutMapping != previous.settings.applyGamutMapping ||
        request.metadata.originalSampleCount != previous.metadata.originalSampleCount ||
        request.metadata.durationSeconds != previous.metadata.durationSeconds) {
        request.serial = previous.serial + 1;
        impl_->latest = request;
        impl_->wake.notify_one();
    }
    const auto snapshot = impl_->published;
    if (snapshot && snapshot->sourceGeneration == request.generation && snapshot->preview == request.preview &&
        (snapshot->message.empty() || impl_->publishedSerial == impl_->latest.serial)) {
        if (!request.preview) impl_->importHandover.reset();
        return snapshot;
    }
    if (const auto& handover = impl_->importHandover; handover && !request.preview &&
        handover->sourceGeneration == request.generation && request.count > 0) {
        auto current = std::make_shared<PresentationSnapshot>(*handover);
        current->sourceFrames = request.count;
        current->duration = RSYNPresentation::durationSeconds(request.metadata, current->colours->endTime());
        return current;
    }
    impl_->importHandover.reset();
    return {};
}

std::shared_ptr<const PresentationSnapshot> recorderPresentation(RecorderState& state) {
    if (!state.presentationWorker) state.presentationWorker = std::make_shared<PresentationWorker>(state);
    return state.presentationWorker->request();
}

}
