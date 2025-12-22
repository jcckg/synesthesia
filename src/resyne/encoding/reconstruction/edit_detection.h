#pragma once

#include "resyne/encoding/spectral/colour_native_codec.h"

#include <cstddef>
#include <vector>

namespace PhaseReconstruction {

struct EditBoundaryInfo {
	std::vector<float> boundaryWeights;
	std::vector<bool> isEditedRegion;
	size_t width;
	size_t height;

	EditBoundaryInfo() : width(0), height(0) {}
	EditBoundaryInfo(size_t w, size_t h)
		: boundaryWeights(w * h, 0.0f),
		  isEditedRegion(w * h, false),
		  width(w),
		  height(h) {}

	float weightAt(size_t x, size_t y) const {
		return boundaryWeights[y * width + x];
	}

	bool isEdited(size_t x, size_t y) const {
		return isEditedRegion[y * width + x];
	}
};

EditBoundaryInfo detectEditBoundaries(const ColourNativeImage& original,
									  const ColourNativeImage& edited);

EditBoundaryInfo detectEditBoundariesSingleImage(const ColourNativeImage& image);

std::vector<float> computeTransitionWeights(const EditBoundaryInfo& boundaries,
											size_t transitionRadius);

}
