#include "resyne/encoding/formats/exporter.h"
#include "resyne/encoding/formats/format_tiff.h"
#include "resyne/encoding/formats/format_resyne.h"
#include "resyne/encoding/formats/format_wav.h"

bool SequenceExporter::exportToResyne(const std::string& filepath,
                                       const std::vector<AudioColourSample>& samples,
                                       const AudioMetadata& metadata,
                                       const std::function<void(float)>& progress) {
	return SequenceExporterInternal::exportToResyne(filepath, samples, metadata, progress);
}

bool SequenceExporter::loadFromResyne(const std::string& filepath,
                                       std::vector<AudioColourSample>& samples,
                                       AudioMetadata& metadata,
                                       const std::function<void(float)>& progress,
                                       const SequenceFrameCallback& onFrameDecoded) {
    return SequenceExporterInternal::loadFromResyne(filepath, samples, metadata, progress, onFrameDecoded);
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
