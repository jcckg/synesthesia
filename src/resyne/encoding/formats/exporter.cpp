#include "resyne/encoding/formats/exporter.h"
#include "resyne/encoding/formats/format_tiff.h"
#include "resyne/encoding/formats/format_rsyn.h"
#include "resyne/encoding/formats/format_wav.h"

bool SequenceExporter::exportToRsyn(const std::string& filepath,
                                    const std::vector<AudioColourSample>& samples,
                                    const AudioMetadata& metadata,
                                    const RSYNExportOptions& options,
                                    const std::function<void(float)>& progress) {
    return SequenceExporterInternal::exportToRsyn(filepath, samples, metadata, options, progress);
}

bool SequenceExporter::loadFromRsyn(const std::string& filepath,
                                    std::vector<AudioColourSample>& samples,
                                    AudioMetadata& metadata,
                                    const std::function<void(float)>& progress,
                                    const SequenceFrameCallback& onFrameDecoded) {
    return SequenceExporterInternal::loadFromRsyn(filepath, samples, metadata, progress, onFrameDecoded);
}

bool SequenceExporter::loadFromRsynShell(const std::string& filepath,
                                         AudioMetadata& metadata,
                                         const std::function<void(float)>& progress) {
    return SequenceExporterInternal::loadFromRsynShell(filepath, metadata, progress);
}

bool SequenceExporter::hydrateRsynSamples(AudioMetadata& metadata,
                                          std::vector<AudioColourSample>& samples,
                                          const std::function<void(float)>& progress,
                                          const SequenceFrameCallback& onFrameDecoded) {
    return SequenceExporterInternal::hydrateRsynSamples(metadata, samples, progress, onFrameDecoded);
}

bool SequenceExporter::hydrateRsynSource(AudioMetadata& metadata,
                                         const std::function<void(float)>& progress) {
    return SequenceExporterInternal::hydrateRsynSource(metadata, progress);
}

bool SequenceExporter::exportToWAV(const std::string& filepath,
                                   const std::vector<AudioColourSample>& samples,
                                   const AudioMetadata& metadata,
                                   const std::function<void(float)>& progress) {
	return SequenceExporterInternal::exportToWAV(filepath, samples, metadata, progress);
}

bool SequenceExporter::exportToTIFF(const std::string& filepath,
                                    const std::vector<AudioColourSample>& samples,
                                    const AudioMetadata& metadata,
                                    const std::function<void(float)>& progress) {
	return SequenceExporterInternal::exportToTIFF(filepath, samples, metadata, progress);
}

bool SequenceExporter::loadFromTIFF(const std::string& filepath,
                                    std::vector<AudioColourSample>& samples,
                                    AudioMetadata& metadata,
                                    const std::function<void(float)>& progress,
                                    const SequenceFrameCallback& onFrameDecoded) {
    return SequenceExporterInternal::loadFromTIFF(filepath, samples, metadata, progress, onFrameDecoded);
}
