#include "colour_mapper_neon.h"
#include "colour_mapper.h"

#ifdef __ARM_NEON

#include <algorithm>
#include <cmath>
#include <vector>

namespace ColourMapperNEON {

using ColourSpace = ::ColourSpace;

namespace {

size_t boundedTripleSize(std::span<const float> first,
						 std::span<const float> second,
						 std::span<const float> third,
						 std::span<float> outFirst,
						 std::span<float> outSecond,
						 std::span<float> outThird,
						 const size_t count) {
	return std::min({first.size(), second.size(), third.size(),
					outFirst.size(), outSecond.size(), outThird.size(), count});
}

void ensureBufferSize(std::vector<float>& buffer, const size_t size) {
	if (buffer.size() < size) {
		buffer.resize(size);
	}
}

}

bool isNEONAvailable() {
	return true;
}

void rgbToXyz(std::span<const float> r, std::span<const float> g, std::span<const float> b,
			  std::span<float> X, std::span<float> Y, std::span<float> Z, const size_t count,
			  const ColourSpace colourSpace) {
	const size_t size = boundedTripleSize(r, g, b, X, Y, Z, count);
	if (size == 0) {
		return;
	}

	thread_local std::vector<float> linearR;
	thread_local std::vector<float> linearG;
	thread_local std::vector<float> linearB;
	ensureBufferSize(linearR, size);
	ensureBufferSize(linearG, size);
	ensureBufferSize(linearB, size);

	for (size_t i = 0; i < size; ++i) {
		ColourMapper::decodeRGB(std::clamp(r[i], 0.0f, 1.0f),
								std::clamp(g[i], 0.0f, 1.0f),
								std::clamp(b[i], 0.0f, 1.0f),
								linearR[i], linearG[i], linearB[i], colourSpace);
	}

	const auto& matrix = ColourMapper::getRGBtoXYZMatrix(colourSpace);
	const float32x4_t m00 = vdupq_n_f32(matrix[0]);
	const float32x4_t m01 = vdupq_n_f32(matrix[1]);
	const float32x4_t m02 = vdupq_n_f32(matrix[2]);
	const float32x4_t m10 = vdupq_n_f32(matrix[3]);
	const float32x4_t m11 = vdupq_n_f32(matrix[4]);
	const float32x4_t m12 = vdupq_n_f32(matrix[5]);
	const float32x4_t m20 = vdupq_n_f32(matrix[6]);
	const float32x4_t m21 = vdupq_n_f32(matrix[7]);
	const float32x4_t m22 = vdupq_n_f32(matrix[8]);

	const size_t vectorSize = size & ~size_t{3};
	size_t i = 0;
	for (; i < vectorSize; i += 4) {
		const float32x4_t rVec = vld1q_f32(linearR.data() + i);
		const float32x4_t gVec = vld1q_f32(linearG.data() + i);
		const float32x4_t bVec = vld1q_f32(linearB.data() + i);

		float32x4_t XVec = vaddq_f32(vmulq_f32(rVec, m00), vmulq_f32(gVec, m01));
		XVec = vaddq_f32(XVec, vmulq_f32(bVec, m02));
		float32x4_t YVec = vaddq_f32(vmulq_f32(rVec, m10), vmulq_f32(gVec, m11));
		YVec = vaddq_f32(YVec, vmulq_f32(bVec, m12));
		float32x4_t ZVec = vaddq_f32(vmulq_f32(rVec, m20), vmulq_f32(gVec, m21));
		ZVec = vaddq_f32(ZVec, vmulq_f32(bVec, m22));

		vst1q_f32(&X[i], XVec);
		vst1q_f32(&Y[i], YVec);
		vst1q_f32(&Z[i], ZVec);
	}

	for (; i < size; ++i) {
		ColourMapper::linearRGBToXYZ(linearR[i], linearG[i], linearB[i], X[i], Y[i], Z[i], colourSpace);
	}
}

void xyzToRgb(std::span<const float> X, std::span<const float> Y, std::span<const float> Z,
			  std::span<float> r, std::span<float> g, std::span<float> b, const size_t count,
			  const ColourSpace colourSpace, const bool applyGamutMapping) {
	const size_t size = boundedTripleSize(X, Y, Z, r, g, b, count);
	if (size == 0) {
		return;
	}

	thread_local std::vector<float> linearR;
	thread_local std::vector<float> linearG;
	thread_local std::vector<float> linearB;
	ensureBufferSize(linearR, size);
	ensureBufferSize(linearG, size);
	ensureBufferSize(linearB, size);

	const auto& matrix = ColourMapper::getXYZtoRGBMatrix(colourSpace);
	const float32x4_t m00 = vdupq_n_f32(matrix[0]);
	const float32x4_t m01 = vdupq_n_f32(matrix[1]);
	const float32x4_t m02 = vdupq_n_f32(matrix[2]);
	const float32x4_t m10 = vdupq_n_f32(matrix[3]);
	const float32x4_t m11 = vdupq_n_f32(matrix[4]);
	const float32x4_t m12 = vdupq_n_f32(matrix[5]);
	const float32x4_t m20 = vdupq_n_f32(matrix[6]);
	const float32x4_t m21 = vdupq_n_f32(matrix[7]);
	const float32x4_t m22 = vdupq_n_f32(matrix[8]);

	const size_t vectorSize = size & ~size_t{3};
	size_t i = 0;
	for (; i < vectorSize; i += 4) {
		const float32x4_t xVec = vld1q_f32(&X[i]);
		const float32x4_t yVec = vld1q_f32(&Y[i]);
		const float32x4_t zVec = vld1q_f32(&Z[i]);

		float32x4_t rVec = vaddq_f32(vmulq_f32(xVec, m00), vmulq_f32(yVec, m01));
		rVec = vaddq_f32(rVec, vmulq_f32(zVec, m02));
		float32x4_t gVec = vaddq_f32(vmulq_f32(xVec, m10), vmulq_f32(yVec, m11));
		gVec = vaddq_f32(gVec, vmulq_f32(zVec, m12));
		float32x4_t bVec = vaddq_f32(vmulq_f32(xVec, m20), vmulq_f32(yVec, m21));
		bVec = vaddq_f32(bVec, vmulq_f32(zVec, m22));

		vst1q_f32(linearR.data() + i, rVec);
		vst1q_f32(linearG.data() + i, gVec);
		vst1q_f32(linearB.data() + i, bVec);
	}

	for (; i < size; ++i) {
		ColourMapper::XYZtoLinearRGB(X[i], Y[i], Z[i], linearR[i], linearG[i], linearB[i], colourSpace);
	}

	for (size_t index = 0; index < size; ++index) {
		ColourMapper::encodeRGB(linearR[index], linearG[index], linearB[index],
								r[index], g[index], b[index], colourSpace, applyGamutMapping);
	}
}

void rgbToLab(std::span<const float> r, std::span<const float> g, std::span<const float> b,
			  std::span<float> L, std::span<float> a, std::span<float> b_comp, const size_t count,
			  const ColourSpace colourSpace) {
	const size_t size = boundedTripleSize(r, g, b, L, a, b_comp, count);
	if (size == 0) {
		return;
	}

	thread_local std::vector<float> X;
	thread_local std::vector<float> Y;
	thread_local std::vector<float> Z;
	ensureBufferSize(X, size);
	ensureBufferSize(Y, size);
	ensureBufferSize(Z, size);

	rgbToXyz(r, g, b, X, Y, Z, size, colourSpace);

	for (size_t i = 0; i < size; ++i) {
		ColourMapper::XYZtoLab(X[i], Y[i], Z[i], L[i], a[i], b_comp[i]);
	}
}

void labToRgb(std::span<const float> L, std::span<const float> a, std::span<const float> b_comp,
			  std::span<float> r, std::span<float> g, std::span<float> b, const size_t count,
			  const ColourSpace colourSpace, const bool applyGamutMapping) {
	const size_t size = boundedTripleSize(L, a, b_comp, r, g, b, count);
	if (size == 0) {
		return;
	}

	thread_local std::vector<float> X;
	thread_local std::vector<float> Y;
	thread_local std::vector<float> Z;
	ensureBufferSize(X, size);
	ensureBufferSize(Y, size);
	ensureBufferSize(Z, size);

	for (size_t i = 0; i < size; ++i) {
		ColourMapper::LabtoXYZ(L[i], a[i], b_comp[i], X[i], Y[i], Z[i]);
	}

	xyzToRgb(X, Y, Z, r, g, b, size, colourSpace, applyGamutMapping);
}

void rgbToOklab(std::span<const float> r, std::span<const float> g, std::span<const float> b,
				std::span<float> L, std::span<float> a, std::span<float> b_comp, const size_t count,
				const ColourSpace colourSpace) {
	const size_t size = boundedTripleSize(r, g, b, L, a, b_comp, count);
	if (size == 0) {
		return;
	}

	thread_local std::vector<float> X;
	thread_local std::vector<float> Y;
	thread_local std::vector<float> Z;
	ensureBufferSize(X, size);
	ensureBufferSize(Y, size);
	ensureBufferSize(Z, size);

	rgbToXyz(r, g, b, X, Y, Z, size, colourSpace);

	for (size_t i = 0; i < size; ++i) {
		ColourMapper::XYZtoOklab(X[i], Y[i], Z[i], L[i], a[i], b_comp[i]);
	}
}

void oklabToRgb(std::span<const float> L, std::span<const float> a, std::span<const float> b_comp,
				std::span<float> r, std::span<float> g, std::span<float> b, const size_t count,
				const ColourSpace colourSpace) {
	const size_t size = boundedTripleSize(L, a, b_comp, r, g, b, count);
	if (size == 0) {
		return;
	}

	thread_local std::vector<float> X;
	thread_local std::vector<float> Y;
	thread_local std::vector<float> Z;
	ensureBufferSize(X, size);
	ensureBufferSize(Y, size);
	ensureBufferSize(Z, size);

	for (size_t i = 0; i < size; ++i) {
		ColourMapper::OklabtoXYZ(L[i], a[i], b_comp[i], X[i], Y[i], Z[i]);
	}

	xyzToRgb(X, Y, Z, r, g, b, size, colourSpace, true);
}

void vectorLerp(std::span<float> result, std::span<const float> a, std::span<const float> b,
				std::span<const float> t, const size_t count) {
	const size_t size = std::min({result.size(), a.size(), b.size(), t.size(), count});
	const size_t vectorSize = size & ~size_t{3};
	const float32x4_t one = vdupq_n_f32(1.0f);

	size_t i = 0;
	for (; i < vectorSize; i += 4) {
		const float32x4_t aVec = vld1q_f32(&a[i]);
		const float32x4_t bVec = vld1q_f32(&b[i]);
		const float32x4_t tVec = vld1q_f32(&t[i]);
		const float32x4_t outVec = vaddq_f32(vmulq_f32(aVec, vsubq_f32(one, tVec)),
											 vmulq_f32(bVec, tVec));
		vst1q_f32(&result[i], outVec);
	}

	for (; i < size; ++i) {
		result[i] = a[i] * (1.0f - t[i]) + b[i] * t[i];
	}
}

void vectorClamp(std::span<float> data, const float min_val, const float max_val, const size_t count) {
	const size_t size = std::min(data.size(), count);
	const size_t vectorSize = size & ~size_t{3};
	const float32x4_t minVec = vdupq_n_f32(min_val);
	const float32x4_t maxVec = vdupq_n_f32(max_val);

	size_t i = 0;
	for (; i < vectorSize; i += 4) {
		const float32x4_t value = vld1q_f32(&data[i]);
		const float32x4_t out = vmaxq_f32(vminq_f32(value, maxVec), minVec);
		vst1q_f32(&data[i], out);
	}

	for (; i < size; ++i) {
		data[i] = std::clamp(data[i], min_val, max_val);
	}
}

void vectorPow(std::span<float> data, const float exponent, const size_t count) {
	const size_t size = std::min(data.size(), count);
	for (size_t i = 0; i < size; ++i) {
		data[i] = std::pow(data[i], exponent);
	}
}

void vectorLog(std::span<float> result, std::span<const float> input, const size_t count) {
	const size_t size = std::min({result.size(), input.size(), count});
	for (size_t i = 0; i < size; ++i) {
		result[i] = std::log(input[i]);
	}
}

void vectorExp(std::span<float> result, std::span<const float> input, const size_t count) {
	const size_t size = std::min({result.size(), input.size(), count});
	for (size_t i = 0; i < size; ++i) {
		result[i] = std::exp(input[i]);
	}
}

void vectorSqrt(std::span<float> result, std::span<const float> input, const size_t count) {
	const size_t size = std::min({result.size(), input.size(), count});
	const size_t vectorSize = size & ~size_t{3};

	size_t i = 0;
	for (; i < vectorSize; i += 4) {
		const float32x4_t value = vld1q_f32(&input[i]);
		const float32x4_t out = vsqrtq_f32(value);
		vst1q_f32(&result[i], out);
	}

	for (; i < size; ++i) {
		result[i] = std::sqrt(input[i]);
	}
}

}

#endif
