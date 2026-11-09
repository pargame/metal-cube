#pragma once

#include <cmath>

// 기준 좌표계 전이(Change of Basis)를 수행하는 4x4 동차 좌표계(Homogeneous Coordinates) 행렬
// 3차원 공간(R^3)의 회전(선형 변환)과 이동(아핀 변환)을 단일 행렬 곱셈 사슬로 처리하기 위해
// 4차원 동차 좌표([x, y, z, 1]^T)를 사용합니다. (Metal Column-Major 메모리 레이아웃 준수)
struct Matrix4x4 {
    float m[16]; // m[col * 4 + row]

    static Matrix4x4 identity() {
        Matrix4x4 r = {};
        r.m[0] = 1.0f;
        r.m[5] = 1.0f;
        r.m[10] = 1.0f;
        r.m[15] = 1.0f;
        return r;
    }

    static Matrix4x4 rotationX(float rad) {
        Matrix4x4 r = identity();
        float c = std::cos(rad);
        float s = std::sin(rad);
        r.m[5] = c;
        r.m[6] = s;
        r.m[9] = -s;
        r.m[10] = c;
        return r;
    }

    static Matrix4x4 rotationY(float rad) {
        Matrix4x4 r = identity();
        float c = std::cos(rad);
        float s = std::sin(rad);
        r.m[0] = c;
        r.m[2] = -s;
        r.m[8] = s;
        r.m[10] = c;
        return r;
    }

    static Matrix4x4 translation(float x, float y, float z) {
        Matrix4x4 r = identity();
        r.m[12] = x;
        r.m[13] = y;
        r.m[14] = z;
        return r;
    }

    // [4단계: 차원 축소] 3차원 관찰자 공간(View Space)의 시야 절두체를
    // Metal 규격의 정규화 투영 공간(NDC: X, Y in [-1, 1], Z in [0, 1])으로 압축하는 원근 투영 행렬
    static Matrix4x4 perspective(float fovyRad, float aspect, float nearZ, float farZ) {
        Matrix4x4 r = {};
        float tanHalfFov = std::tan(fovyRad * 0.5f);
        r.m[0] = 1.0f / (aspect * tanHalfFov);
        r.m[5] = 1.0f / tanHalfFov;
        r.m[10] = farZ / (nearZ - farZ);
        r.m[11] = -1.0f;
        r.m[14] = (farZ * nearZ) / (nearZ - farZ);
        return r;
    }

    static Matrix4x4 multiply(const Matrix4x4& a, const Matrix4x4& b) {
        Matrix4x4 r = {};
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += a.m[k * 4 + row] * b.m[c * 4 + k];
                }
                r.m[c * 4 + row] = sum;
            }
        }
        return r;
    }
};
