// FiguraBridge math: vectors and matrices with Figura 0.1.6's exact semantics (org.figuramc.figura.math.*).
// FiguraMat4 stores v<row><col>; translate/scale/rotate*() LEFT-multiply (new op applied after the existing ones),
// exactly like the Java code, so the PartCustomization maths can be ported line by line.
// Original port for Bedrock Forge (Figura: PolyForm Noncommercial; ported for Fernando's private use with permission).
#pragma once
#include <cmath>

namespace bf::figura {

inline constexpr double kPi = 3.14159265358979323846;
inline double toRad(double d) { return d * kPi / 180.0; }
inline double toDeg(double r) { return r * 180.0 / kPi; }

struct Vec2 {
    double x = 0, y = 0;
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    constexpr Vec3(double a, double b, double c) : x(a), y(b), z(c) {}
    Vec3  operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3  operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3  operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3  mul(Vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    bool  operator==(Vec3 const& o) const { return x == o.x && y == o.y && z == o.z; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
};

struct Vec4 {
    double x = 0, y = 0, z = 0, w = 0;
};

/// Figura FiguraMat4 (row-major fields v11..v44, column vectors: p' = M p).
struct Mat4 {
    double v11 = 1, v12 = 0, v13 = 0, v14 = 0;
    double v21 = 0, v22 = 1, v23 = 0, v24 = 0;
    double v31 = 0, v32 = 0, v33 = 1, v34 = 0;
    double v41 = 0, v42 = 0, v43 = 0, v44 = 1;

    void reset() { *this = Mat4{}; }

    void translate(double x, double y, double z) {
        v11 += x * v41; v12 += x * v42; v13 += x * v43; v14 += x * v44;
        v21 += y * v41; v22 += y * v42; v23 += y * v43; v24 += y * v44;
        v31 += z * v41; v32 += z * v42; v33 += z * v43; v34 += z * v44;
    }
    /// translateFirst: this = this * T (applied before the existing ops)
    void translateFirst(double x, double y, double z) {
        v14 += v11 * x + v12 * y + v13 * z;
        v24 += v21 * x + v22 * y + v23 * z;
        v34 += v31 * x + v32 * y + v33 * z;
        v44 += v41 * x + v42 * y + v43 * z;
    }
    void scale(double x, double y, double z) {
        v11 *= x; v12 *= x; v13 *= x; v14 *= x;
        v21 *= y; v22 *= y; v23 *= y; v24 *= y;
        v31 *= z; v32 *= z; v33 *= z; v34 *= z;
    }
    void scale(Vec3 s) { scale(s.x, s.y, s.z); }
    void rotateX(double deg) {
        double r = toRad(deg), c = std::cos(r), s = std::sin(r);
        double n21 = c * v21 - s * v31, n22 = c * v22 - s * v32, n23 = c * v23 - s * v33, n24 = c * v24 - s * v34;
        v31 = s * v21 + c * v31; v32 = s * v22 + c * v32; v33 = s * v23 + c * v33; v34 = s * v24 + c * v34;
        v21 = n21; v22 = n22; v23 = n23; v24 = n24;
    }
    void rotateY(double deg) {
        double r = toRad(deg), c = std::cos(r), s = std::sin(r);
        double n11 = c * v11 + s * v31, n12 = c * v12 + s * v32, n13 = c * v13 + s * v33, n14 = c * v14 + s * v34;
        v31 = c * v31 - s * v11; v32 = c * v32 - s * v12; v33 = c * v33 - s * v13; v34 = c * v34 - s * v14;
        v11 = n11; v12 = n12; v13 = n13; v14 = n14;
    }
    void rotateZ(double deg) {
        double r = toRad(deg), c = std::cos(r), s = std::sin(r);
        double n11 = c * v11 - s * v21, n12 = c * v12 - s * v22, n13 = c * v13 - s * v23, n14 = c * v14 - s * v24;
        v21 = s * v11 + c * v21; v22 = s * v12 + c * v22; v23 = s * v13 + c * v23; v24 = s * v14 + c * v24;
        v11 = n11; v12 = n12; v13 = n13; v14 = n14;
    }
    /// Figura rotateZYX(x, y, z): left-multiplies Rz(z)*Ry(y)*Rx(x).
    void rotateZYX(double x, double y, double z) {
        x = toRad(x); y = toRad(y); z = toRad(z);
        double a = std::cos(x), b = std::sin(x), c = std::cos(y), d = std::sin(y), e = std::cos(z), f = std::sin(z);
        double bc = b * c, ac = a * c, ce = c * e, cf = c * f;
        double p1 = (b * d * e - a * f), p2 = (a * d * e + b * f), p3 = (a * e + b * d * f), p4 = (a * d * f - b * e);
        double n11 = ce * v11 + p1 * v21 + p2 * v31, n21 = cf * v11 + p3 * v21 + p4 * v31, n31 = -d * v11 + bc * v21 + ac * v31;
        double n12 = ce * v12 + p1 * v22 + p2 * v32, n22 = cf * v12 + p3 * v22 + p4 * v32, n32 = -d * v12 + bc * v22 + ac * v32;
        double n13 = ce * v13 + p1 * v23 + p2 * v33, n23 = cf * v13 + p3 * v23 + p4 * v33, n33 = -d * v13 + bc * v23 + ac * v33;
        double n14 = ce * v14 + p1 * v24 + p2 * v34, n24 = cf * v14 + p3 * v24 + p4 * v34, n34 = -d * v14 + bc * v24 + ac * v34;
        v11 = n11; v21 = n21; v31 = n31; v12 = n12; v22 = n22; v32 = n32;
        v13 = n13; v23 = n23; v33 = n33; v14 = n14; v24 = n24; v34 = n34;
    }
    /// this = this * o
    Mat4 operator*(Mat4 const& o) const {
        Mat4 r;
        double const* a = &v11;
        double const* b = &o.v11;
        double*       c = &r.v11;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                double s = 0;
                for (int k = 0; k < 4; k++) s += a[i * 4 + k] * b[k * 4 + j];
                c[i * 4 + j] = s;
            }
        return r;
    }
    Vec3 apply(Vec3 p) const {
        return {v11 * p.x + v12 * p.y + v13 * p.z + v14, v21 * p.x + v22 * p.y + v23 * p.z + v24,
                v31 * p.x + v32 * p.y + v33 * p.z + v34};
    }
    double  at(int r, int c) const { return (&v11)[r * 4 + c]; }
    double& at(int r, int c) { return (&v11)[r * 4 + c]; }
};

/// Bedrock bone values for one part (what the resource pack's drive animation reads every frame).
struct BoneOut {
    Vec3 rot;            // Bedrock bone rotation, degrees ([-x, -y, z] of the Blockbench euler)
    Vec3 pos;            // Bedrock animation position offset ([-x, y, z] of the Blockbench-space offset)
    Vec3 scale{1, 1, 1};
};

/// Decompose a part's LOCAL matrix (Blockbench/Figura part space, parent-relative) into Bedrock bone values for a bone
/// whose fixed pivot is `pivot` (Blockbench space) and whose bind rotation is 0:
///   M = T(t) * T(p) * Rz*Ry*Rx * S * T(-p)   ->   t = M*p - p, R*S = linear part.
/// Handles negative scale (mirroring) by flipping X; assumes no shear (Figura parts only rotate/scale/translate).
inline BoneOut decomposeForBedrock(Mat4 const& m, Vec3 pivot) {
    BoneOut o;
    double a[3][3] = {{m.v11, m.v12, m.v13}, {m.v21, m.v22, m.v23}, {m.v31, m.v32, m.v33}};
    double s[3];
    for (int c = 0; c < 3; c++) s[c] = std::sqrt(a[0][c] * a[0][c] + a[1][c] * a[1][c] + a[2][c] * a[2][c]);
    double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
               + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    if (det < 0) s[0] = -s[0];
    double r[3][3];
    for (int c = 0; c < 3; c++)
        for (int rr = 0; rr < 3; rr++) r[rr][c] = s[c] != 0 ? a[rr][c] / s[c] : (rr == c ? 1.0 : 0.0);
    // R = Rz(z) Ry(y) Rx(x): r20 = -sin y, r21 = cos y sin x, r22 = cos y cos x, r10 = cos y sin z, r00 = cos y cos z
    double sy = -r[2][0];
    if (sy > 1) sy = 1;
    if (sy < -1) sy = -1;
    double ry = std::asin(sy), rx, rz;
    if (std::fabs(sy) < 0.9999999) {
        rx = std::atan2(r[2][1], r[2][2]);
        rz = std::atan2(r[1][0], r[0][0]);
    } else { // gimbal lock: put everything in x
        rz = 0;
        rx = std::atan2(-r[1][2], r[1][1]);
    }
    Vec3 mp = m.apply(pivot);
    Vec3 t  = mp - pivot;
    o.rot   = {-toDeg(rx), -toDeg(ry), toDeg(rz)};
    o.pos   = {-t.x, t.y, t.z};
    o.scale = {s[0], s[1], s[2]};
    return o;
}

} // namespace bf::figura
