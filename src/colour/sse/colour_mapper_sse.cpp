#include "colour_mapper_sse.h"
#include "colour_mapper.h"

#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)

#include <algorithm>
#include <cmath>
#include <vector>

namespace ColourMapperSSE {

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

bool isSSEAvailable() {
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
	const __m128 m00 = _mm_set1_ps(matrix[0]);
	const __m128 m01 = _mm_set1_ps(matrix[1]);
	const __m128 m02 = _mm_set1_ps(matrix[2]);
	const __m128 m10 = _mm_set1_ps(matrix[3]);
	const __m128 m11 = _mm_set1_ps(matrix[4]);
	const __m128 m12 = _mm_set1_ps(matrix[5]);
	const __m128 m20 = _mm_set1_ps(matrix[6]);
	const __m128 m21 = _mm_set1_ps(matrix[7]);
	const __m128 m22 = _mm_set1_ps(matrix[8]);

	const size_t vectorSize = size & ~size_t{3};
	size_t i = 0;
	for (; i < vectorSize; i += 4) {
		const __m128 rVec = _mm_loadu_ps(linearR.data() + i);
		const __m128 gVec = _mm_loadu_ps(linearG.data() + i);
		const __m128 bVec = _mm_loadu_ps(linearB.data() + i);

		__m128 XVec = _mm_add_ps(_mm_mul_ps(rVec, m00), _mm_mul_ps(gVec, m01));
		XVec = _mm_add_ps(XVec, _mm_mul_ps(bVec, m02));
		__m128 YVec = _mm_add_ps(_mm_mul_ps(rVec, m10), _mm_mul_ps(gVec, m11));
		YVec = _mm_add_ps(YVec, _mm_mul_ps(bVec, m12));
		__m128 ZVec = _mm_add_ps(_mm_mul_ps(rVec, m20), _mm_mul_ps(gVec, m21));
		ZVec = _mm_add_ps(ZVec, _mm_mul_ps(bVec, m22));

		_mm_storeu_ps(&X[i], XVec);
		_mm_storeu_ps(&Y[i], YVec);
		_mm_storeu_ps(&Z[i], ZVec);
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
	const __m128 m00 = _mm_set1_ps(matrix[0]);
	const __m128 m01 = _mm_set1_ps(matrix[1]);
	const __m128 m02 = _mm_set1_ps(matrix[2]);
	const __m128 m10 = _mm_set1_ps(matrix[3]);
	const __m128 m11 = _mm_set1_ps(matrix[4]);
	const __m128 m12 = _mm_set1_ps(matrix[5]);
	const __m128 m20 = _mm_set1_ps(matrix[6]);
	const __m128 m21 = _mm_set1_ps(matrix[7]);
	const __m128 m22 = _mm_set1_ps(matrix[8]);

	const size_t vectorSize = size & ~size_t{3};
	size_t i = 0;
	for (; i < vectorSize; i += 4) {
		const __m128 xVec = _mm_loadu_ps(&X[i]);
		const __m128 yVec = _mm_loadu_ps(&Y[i]);
		const __m128 zVec = _mm_loadu_ps(&Z[i]);

		__m128 rVec = _mm_add_ps(_mm_mul_ps(xVec, m00), _mm_mul_ps(yVec, m01));
		rVec = _mm_add_ps(rVec, _mm_mul_ps(zVec, m02));
		__m128 gVec = _mm_add_ps(_mm_mul_ps(xVec, m10), _mm_mul_ps(yVec, m11));
		gVec = _mm_add_ps(gVec, _mm_mul_ps(zVec, m12));
		__m128 bVec = _mm_add_ps(_mm_mul_ps(xVec, m20), _mm_mul_ps(yVec, m21));
		bVec = _mm_add_ps(bVec, _mm_mul_ps(zVec, m22));

		_mm_storeu_ps(linearR.data() + i, rVec);
		_mm_storeu_ps(linearG.data() + i, gVec);
		_mm_storeu_ps(linearB.data() + i, bVec);
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
	const __m128 one = _mm_set1_ps(1.0f);

	size_t i = 0;
	for (; i < vectorSize; i += 4) {
		const __m128 aVec = _mm_loadu_ps(&a[i]);
		const __m128 bVec = _mm_loadu_ps(&b[i]);
		const __m128 tVec = _mm_loadu_ps(&t[i]);
		const __m128 outVec = _mm_add_ps(_mm_mul_ps(aVec, _mm_sub_ps(one, tVec)),
										 _mm_mul_ps(bVec, tVec));
		_mm_storeu_ps(&result[i], outVec);
	}

	for (; i < size; ++i) {
		result[i] = a[i] * (1.0f - t[i]) + b[i] * t[i];
	}
}

void vectorClamp(std::span<float> data, const float min_val, const float max_val, const size_t count) {
	const size_t size = std::min(data.size(), count);
	const size_t vectorSize = size & ~size_t{3};
	const __m128 minVec = _mm_set1_ps(min_val);
	const __m128 maxVec = _mm_set1_ps(max_val);

	size_t i = 0;
	for (; i < vectorSize; i += 4) {
		const __m128 value = _mm_loadu_ps(&data[i]);
		const __m128 out = _mm_min_ps(_mm_max_ps(value, minVec), maxVec);
		_mm_storeu_ps(&data[i], out);
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
		const __m128 value = _mm_loadu_ps(&input[i]);
		const __m128 out = _mm_sqrt_ps(value);
		_mm_storeu_ps(&result[i], out);
	}

	for (; i < size; ++i) {
		result[i] = std::sqrt(input[i]);
	}
}

}

#endif
