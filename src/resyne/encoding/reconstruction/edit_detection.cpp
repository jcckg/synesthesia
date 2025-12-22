#include "edit_detection.h"

#include <algorithm>
#include <cmath>

namespace PhaseReconstruction {

namespace {
constexpr float GRADIENT_THRESHOLD = 0.015f;
constexpr float SECOND_DERIVATIVE_THRESHOLD = 0.008f;
constexpr size_t MIN_REGION_SIZE = 4;

float computeGradientMagnitude(const ColourNativeImage& image,
							   size_t x, size_t y,
							   size_t channel) {
	if (x == 0 || x >= image.width - 1 || y == 0 || y >= image.height - 1) {
		return 0.0f;
	}

	auto getValue = [&](size_t px, size_t py) -> float {
		const RGBAColour& pixel = image.at(px, py);
		switch (channel) {
			case 0: return pixel.r;
			case 1: return pixel.g;
			case 2: return pixel.b;
			case 3: return pixel.a;
			default: return 0.0f;
		}
	};

	const float dx = (getValue(x + 1, y) - getValue(x - 1, y)) * 0.5f;
	const float dy = (getValue(x, y + 1) - getValue(x, y - 1)) * 0.5f;

	return std::sqrt(dx * dx + dy * dy);
}

float computeSecondDerivative(const ColourNativeImage& image,
							  size_t x, size_t y,
							  size_t channel) {
	if (x == 0 || x >= image.width - 1 || y == 0 || y >= image.height - 1) {
		return 0.0f;
	}

	auto getValue = [&](size_t px, size_t py) -> float {
		const RGBAColour& pixel = image.at(px, py);
		switch (channel) {
			case 0: return pixel.r;
			case 1: return pixel.g;
			case 2: return pixel.b;
			case 3: return pixel.a;
			default: return 0.0f;
		}
	};

	const float centre = getValue(x, y);
	const float laplacian = getValue(x + 1, y) + getValue(x - 1, y) +
							getValue(x, y + 1) + getValue(x, y - 1) -
							4.0f * centre;

	return std::abs(laplacian);
}

}

EditBoundaryInfo detectEditBoundaries(const ColourNativeImage& original,
									  const ColourNativeImage& edited) {
	if (original.width != edited.width || original.height != edited.height) {
		return EditBoundaryInfo();
	}

	EditBoundaryInfo result(edited.width, edited.height);

	for (size_t y = 0; y < edited.height; ++y) {
		for (size_t x = 0; x < edited.width; ++x) {
			const RGBAColour& origPixel = original.at(x, y);
			const RGBAColour& editPixel = edited.at(x, y);

			const float rDiff = std::abs(editPixel.r - origPixel.r);
			const float gDiff = std::abs(editPixel.g - origPixel.g);
			const float bDiff = std::abs(editPixel.b - origPixel.b);
			const float aDiff = std::abs(editPixel.a - origPixel.a);

			const float maxDiff = std::max({rDiff, gDiff, bDiff, aDiff});
			const size_t idx = y * edited.width + x;

			if (maxDiff > 0.001f) {
				result.isEditedRegion[idx] = true;
			}
		}
	}

	for (size_t y = 1; y < edited.height - 1; ++y) {
		for (size_t x = 1; x < edited.width - 1; ++x) {
			const size_t idx = y * edited.width + x;
			const bool currentEdited = result.isEditedRegion[idx];

			bool isBoundary = false;
			for (int dy = -1; dy <= 1 && !isBoundary; ++dy) {
				for (int dx = -1; dx <= 1 && !isBoundary; ++dx) {
					if (dx == 0 && dy == 0) continue;
					const size_t nx = x + dx;
					const size_t ny = y + dy;
					const size_t nidx = ny * edited.width + nx;
					if (result.isEditedRegion[nidx] != currentEdited) {
						isBoundary = true;
					}
				}
			}

			if (isBoundary) {
				result.boundaryWeights[idx] = 1.0f;
			}
		}
	}

	return result;
}

EditBoundaryInfo detectEditBoundariesSingleImage(const ColourNativeImage& image) {
	EditBoundaryInfo result(image.width, image.height);

	std::vector<float> gradientMag(image.width * image.height, 0.0f);

	for (size_t y = 1; y < image.height - 1; ++y) {
		for (size_t x = 1; x < image.width - 1; ++x) {
			const size_t idx = y * image.width + x;

			float maxGradient = 0.0f;
			float maxSecondDeriv = 0.0f;

			for (size_t ch = 0; ch < 4; ++ch) {
				const float grad = computeGradientMagnitude(image, x, y, ch);
				const float secondDeriv = computeSecondDerivative(image, x, y, ch);
				maxGradient = std::max(maxGradient, grad);
				maxSecondDeriv = std::max(maxSecondDeriv, secondDeriv);
			}

			gradientMag[idx] = maxGradient;

			const bool isEdge = maxGradient > GRADIENT_THRESHOLD &&
								maxSecondDeriv > SECOND_DERIVATIVE_THRESHOLD;

			if (isEdge) {
				result.boundaryWeights[idx] = std::min(1.0f, maxGradient / (GRADIENT_THRESHOLD * 2.0f));
			}
		}
	}

	std::vector<bool> visited(image.width * image.height, false);
	std::vector<size_t> stack;

	for (size_t y = 1; y < image.height - 1; ++y) {
		for (size_t x = 1; x < image.width - 1; ++x) {
			const size_t idx = y * image.width + x;
			if (result.boundaryWeights[idx] > 0.0f || visited[idx]) {
				continue;
			}

			stack.clear();
			stack.push_back(idx);
			std::vector<size_t> region;

			while (!stack.empty()) {
				const size_t current = stack.back();
				stack.pop_back();

				if (visited[current]) continue;
				visited[current] = true;

				const size_t cx = current % image.width;
				const size_t cy = current / image.width;

				if (result.boundaryWeights[current] > 0.0f) {
					continue;
				}

				region.push_back(current);

				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (dx == 0 && dy == 0) continue;
						const size_t nx = cx + dx;
						const size_t ny = cy + dy;
						if (nx >= 1 && nx < image.width - 1 &&
							ny >= 1 && ny < image.height - 1) {
							const size_t nidx = ny * image.width + nx;
							if (!visited[nidx]) {
								stack.push_back(nidx);
							}
						}
					}
				}
			}

			if (region.size() >= MIN_REGION_SIZE && region.size() < image.width * image.height / 2) {
				for (size_t ridx : region) {
					result.isEditedRegion[ridx] = true;
				}
			}
		}
	}

	return result;
}

std::vector<float> computeTransitionWeights(const EditBoundaryInfo& boundaries,
											size_t transitionRadius) {
	std::vector<float> weights(boundaries.width * boundaries.height, 0.0f);

	if (transitionRadius == 0) {
		for (size_t i = 0; i < weights.size(); ++i) {
			weights[i] = boundaries.isEditedRegion[i] ? 1.0f : 0.0f;
		}
		return weights;
	}

	const float radiusF = static_cast<float>(transitionRadius);

	for (size_t y = 0; y < boundaries.height; ++y) {
		for (size_t x = 0; x < boundaries.width; ++x) {
			const size_t idx = y * boundaries.width + x;

			if (boundaries.boundaryWeights[idx] <= 0.0f) {
				weights[idx] = boundaries.isEditedRegion[idx] ? 1.0f : 0.0f;
				continue;
			}

			float minDistToUnedited = static_cast<float>(transitionRadius + 1);
			float minDistToEdited = static_cast<float>(transitionRadius + 1);

			for (int dy = -static_cast<int>(transitionRadius);
				 dy <= static_cast<int>(transitionRadius); ++dy) {
				for (int dx = -static_cast<int>(transitionRadius);
					 dx <= static_cast<int>(transitionRadius); ++dx) {
					const int nx = static_cast<int>(x) + dx;
					const int ny = static_cast<int>(y) + dy;

					if (nx < 0 || nx >= static_cast<int>(boundaries.width) ||
						ny < 0 || ny >= static_cast<int>(boundaries.height)) {
						continue;
					}

					const size_t nidx = static_cast<size_t>(ny) * boundaries.width +
										static_cast<size_t>(nx);
					const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));

					if (boundaries.isEditedRegion[nidx]) {
						minDistToEdited = std::min(minDistToEdited, dist);
					} else if (boundaries.boundaryWeights[nidx] <= 0.0f) {
						minDistToUnedited = std::min(minDistToUnedited, dist);
					}
				}
			}

			const float totalDist = minDistToEdited + minDistToUnedited;
			if (totalDist > 0.0f) {
				const float t = minDistToUnedited / totalDist;
				weights[idx] = 0.5f * (1.0f - std::cos(t * 3.14159265f));
			} else {
				weights[idx] = boundaries.isEditedRegion[idx] ? 1.0f : 0.0f;
			}
		}
	}

	return weights;
}

}
