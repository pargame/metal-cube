#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

// 기준 좌표계 전이(Change of Basis)를 수행하는 4x4 동차 좌표계(Homogeneous Coordinates) 행렬
// 3차원 공간(R^3)의 회전(선형 변환)과 이동(아핀 변환)을 단일 행렬 곱셈 사슬로 처리하기 위해
// 4차원 동차 좌표([x, y, z, 1]^T)를 사용합니다. (Metal Column-Major 메모리 레이아웃 준수)
struct Matrix4x4 {
    std::array<float, 16> m{}; // Column-Major: m[col * 4 + row]

    // [C++23/26 다중 첨자 연산자 (Multidimensional Subscript Operator)]
    // 1차원 오프셋(col * 4 + row) 수동 계산 없이 mat[col, row] 형태로 직관적 인덱싱을 제공합니다.
    constexpr float& operator[](std::size_t col, std::size_t row) noexcept {
        return m[col * 4 + row];
    }

    constexpr float operator[](std::size_t col, std::size_t row) const noexcept {
        return m[col * 4 + row];
    }

    static constexpr Matrix4x4 identity() noexcept {
        Matrix4x4 r{};
        r[0, 0] = 1.0f;
        r[1, 1] = 1.0f;
        r[2, 2] = 1.0f;
        r[3, 3] = 1.0f;
        return r;
    }

    static Matrix4x4 rotationX(float rad) noexcept {
        Matrix4x4 r = identity();
        float c = std::cos(rad);
        float s = std::sin(rad);
        r[1, 1] = c;
        r[1, 2] = s;
        r[2, 1] = -s;
        r[2, 2] = c;
        return r;
    }

    static Matrix4x4 rotationY(float rad) noexcept {
        Matrix4x4 r = identity();
        float c = std::cos(rad);
        float s = std::sin(rad);
        r[0, 0] = c;
        r[0, 2] = -s;
        r[2, 0] = s;
        r[2, 2] = c;
        return r;
    }

    static constexpr Matrix4x4 translation(float x, float y, float z) noexcept {
        Matrix4x4 r = identity();
        r[3, 0] = x;
        r[3, 1] = y;
        r[3, 2] = z;
        return r;
    }

    // [4단계: 차원 축소] 3차원 관찰자 공간(View Space)의 시야 절두체를
    // Metal 규격의 정규화 투영 공간(NDC: X, Y in [-1, 1], Z in [0, 1])으로 압축하는 원근 투영 행렬
    static Matrix4x4 perspective(float fovyRad, float aspect, float nearZ, float farZ) noexcept {
        Matrix4x4 r{};
        float tanHalfFov = std::tan(fovyRad * 0.5f);
        r[0, 0] = 1.0f / (aspect * tanHalfFov);
        r[1, 1] = 1.0f / tanHalfFov;
        r[2, 2] = farZ / (nearZ - farZ);
        r[2, 3] = -1.0f;
        r[3, 2] = (farZ * nearZ) / (nearZ - farZ);
        return r;
    }

    static constexpr Matrix4x4 multiply(const Matrix4x4& a, const Matrix4x4& b) noexcept {
        Matrix4x4 r{};
        for (std::size_t col = 0; col < 4; ++col) {
            for (std::size_t row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (std::size_t k = 0; k < 4; ++k) {
                    sum += a[k, row] * b[col, k];
                }
                r[col, row] = sum;
            }
        }
        return r;
    }
};

// GPU(MSL float4x4)와 직접 메모리를 공유하므로 표준 메모리 레이아웃(Standard Layout) 및 64바이트 크기를 컴파일 타임 검증
static_assert(std::is_standard_layout_v<Matrix4x4>, "Matrix4x4 must have a standard layout for GPU ABI compatibility.");
static_assert(sizeof(Matrix4x4) == 64, "Matrix4x4 must be exactly 64 bytes (16 floats).");
