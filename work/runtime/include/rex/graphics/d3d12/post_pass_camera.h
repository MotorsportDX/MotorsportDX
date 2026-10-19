/**
 * @file        graphics/d3d12/post_pass_camera.h
 * @brief       Scene camera math of the post passes: the projection of a guest
 *              vertex shader's matrices, the depth unpacking constants, and the
 *              reprojection of the static world and of objects into the
 *              previous frame's view (motion vectors)
 *
 * Header only and free of runtime dependencies, so CPU tests can include it.
 * Matrices are 4x4, m[row][column], applied to column vectors (clip = M * p).
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace rex::graphics::d3d12::post_pass {

struct Matrix {
  float m[4][4];
};

// In double, for the reprojection of the motion vectors: world positions on a
// track are kilometers, and they cancel only in double.
struct MatrixD {
  double m[4][4];
};

inline Matrix Identity() {
  Matrix r = {};
  for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
  return r;
}

inline MatrixD IdentityD() {
  MatrixD r = {};
  for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0;
  return r;
}

inline MatrixD ToDouble(const Matrix& a) {
  MatrixD r;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) r.m[i][j] = a.m[i][j];
  }
  return r;
}

inline Matrix ToFloat(const MatrixD& a) {
  Matrix r;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) r.m[i][j] = float(a.m[i][j]);
  }
  return r;
}

inline Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix r = {};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) sum += double(a.m[i][k]) * b.m[k][j];
      r.m[i][j] = float(sum);
    }
  }
  return r;
}

inline MatrixD Multiply(const MatrixD& a, const MatrixD& b) {
  MatrixD r = {};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) sum += a.m[i][k] * b.m[k][j];
      r.m[i][j] = sum;
    }
  }
  return r;
}

// General 4x4 inverse (Gauss-Jordan with partial pivoting).
inline bool Invert(const MatrixD& a, MatrixD& out) {
  double w[4][8];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      w[i][j] = a.m[i][j];
      w[i][4 + j] = i == j ? 1.0 : 0.0;
    }
  }
  for (int c = 0; c < 4; ++c) {
    int pivot = c;
    for (int r = c + 1; r < 4; ++r) {
      if (std::fabs(w[r][c]) > std::fabs(w[pivot][c])) pivot = r;
    }
    if (std::fabs(w[pivot][c]) < 1.0e-12) return false;
    if (pivot != c) {
      for (int j = 0; j < 8; ++j) {
        double t = w[c][j];
        w[c][j] = w[pivot][j];
        w[pivot][j] = t;
      }
    }
    double scale = 1.0 / w[c][c];
    for (int j = 0; j < 8; ++j) w[c][j] *= scale;
    for (int r = 0; r < 4; ++r) {
      if (r == c) continue;
      double f = w[r][c];
      if (f == 0.0) continue;
      for (int j = 0; j < 8; ++j) w[r][j] -= f * w[c][j];
    }
  }
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) out.m[i][j] = w[i][4 + j];
  }
  return true;
}

// The same in float, computed in double.
inline bool Invert(const Matrix& a, Matrix& out) {
  MatrixD inverse;
  if (!Invert(ToDouble(a), inverse)) return false;
  out = ToFloat(inverse);
  return true;
}

// How a guest vertex shader keeps its matrices in float constants c<n>, as read
// from its ucode.
enum class CameraLayout : uint32_t {
  // Row vectors: clip = x * c[wvp] + y * c[wvp + 1] + z * c[wvp + 2] + c[wvp + 3],
  // and the view position the same way with c[wv]..c[wv + 3] (xyz used).
  kWvpWvRows,
  // Column vectors: clip.i = dot(c[wvp + i], p) (4 constants), world.i =
  // dot(c[world + i], p) and world = view inverse: world.i = dot(c[view_inverse
  // + i].xyz, v) + c[view_inverse + i].w (3 constants each).
  kWvpWorldViewInverse,
  // kWvpWvRows and the object's world matrix in 4 more row-vector constants:
  // world = x * c[world] + y * c[world + 1] + z * c[world + 2] + c[world + 3].
  // With WV = V * W, the view to world matrix is W * inverse(WV).
  kWvpWvWorldRows,
};

// The layout carries the camera's view to world matrix.
inline bool CarriesView(CameraLayout layout) {
  return layout == CameraLayout::kWvpWorldViewInverse || layout == CameraLayout::kWvpWvWorldRows;
}

// constants: the guest float constants c0..c255 as 256 float4.
// Returns the matrices clip = wvp * p and view = wv * p as column-vector 4x4.
inline bool CameraMatrices(CameraLayout layout, const float (*constants)[4], uint32_t wvp,
                           uint32_t second, uint32_t third, Matrix& wvp_out, Matrix& wv_out) {
  if (wvp + 4 > 256) return false;
  switch (layout) {
    case CameraLayout::kWvpWvRows:
    case CameraLayout::kWvpWvWorldRows: {
      if (second + 4 > 256) return false;
      // Constant i is row i of the row-vector matrix, so column i here.
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          wvp_out.m[j][i] = constants[wvp + i][j];
          wv_out.m[j][i] = j < 3 ? constants[second + i][j] : (i == 3 ? 1.0f : 0.0f);
        }
      }
      return true;
    }
    case CameraLayout::kWvpWorldViewInverse: {
      if (second + 3 > 256 || third + 3 > 256) return false;
      Matrix world = Identity(), view_inverse = Identity();
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          wvp_out.m[i][j] = constants[wvp + i][j];
          if (i < 3) {
            world.m[i][j] = constants[second + i][j];
            view_inverse.m[i][j] = constants[third + i][j];
          }
        }
      }
      Matrix view;
      if (!Invert(view_inverse, view)) return false;
      wv_out = Multiply(view, world);
      return true;
    }
  }
  return false;
}

// The camera's view to world matrix (world = M * view), where the layout carries
// it (CarriesView): the view inverse of kWvpWorldViewInverse (its third
// constants), or W * inverse(WV) of kWvpWvWorldRows (its second and third), in
// double: the world translation cancels.
inline bool ViewToWorld(CameraLayout layout, const float (*constants)[4], uint32_t second,
                        uint32_t third, Matrix& out) {
  if (layout == CameraLayout::kWvpWorldViewInverse) {
    if (third + 3 > 256) return false;
    out = Identity();
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 4; ++j) out.m[i][j] = constants[third + i][j];
    }
    return true;
  }
  if (layout != CameraLayout::kWvpWvWorldRows || second + 4 > 256 || third + 4 > 256) {
    return false;
  }
  MatrixD wv = IdentityD(), world = IdentityD();
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      // Constant i is row i of the row-vector matrix, so column i here.
      if (j < 3) wv.m[j][i] = constants[second + i][j];
      world.m[j][i] = constants[third + i][j];
    }
  }
  MatrixD wv_inverse;
  if (!Invert(wv, wv_inverse)) return false;
  out = ToFloat(Multiply(world, wv_inverse));
  return true;
}

// The 3x3 of a view to world matrix is a rotation (unit, orthogonal rows), so
// the constants it came from hold a camera, not other data.
inline bool IsRigidView(const Matrix& view_to_world) {
  for (int i = 0; i < 3; ++i) {
    for (int j = i; j < 3; ++j) {
      double dot = 0.0;
      for (int k = 0; k < 3; ++k) {
        dot += double(view_to_world.m[i][k]) * double(view_to_world.m[j][k]);
      }
      if (!(std::fabs(dot - (i == j ? 1.0 : 0.0)) < 1.0e-3)) return false;
    }
  }
  return view_to_world.m[3][0] == 0.0f && view_to_world.m[3][1] == 0.0f &&
         view_to_world.m[3][2] == 0.0f && view_to_world.m[3][3] == 1.0f;
}

// The largest difference of two matrices relative to the largest element of b.
inline double MaxRelativeDifference(const Matrix& a, const Matrix& b) {
  double scale = 0.0, difference = 0.0;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      scale = std::fmax(scale, std::fabs(double(b.m[i][j])));
      difference = std::fmax(difference, std::fabs(double(a.m[i][j]) - double(b.m[i][j])));
    }
  }
  return scale > 0.0 ? difference / scale : difference;
}

// The projection, clip = P * view position, of the matrices of one draw.
inline bool ProjectionFromCamera(const Matrix& wvp, const Matrix& wv, Matrix& projection_out) {
  Matrix wv_inverse;
  if (!Invert(wv, wv_inverse)) return false;
  projection_out = Multiply(wvp, wv_inverse);
  return true;
}

// The guest viewport of the draw: screen = ndc * scale + offset (pixels for X
// and Y, the stored depth for Z), PA_CL_VPORT_*.
struct Viewport {
  float x_scale, x_offset, y_scale, y_offset, z_scale, z_offset;
};

// The projection's aspect (focal length in Y over X) is the viewport's (its
// width over its height) within 2%: a camera drawn for that viewport.
inline bool MatchesViewportAspect(const Matrix& projection, const Viewport& viewport) {
  double focal_x = std::fabs(double(projection.m[0][0]) * viewport.x_scale);
  double focal_y = std::fabs(double(projection.m[1][1]) * viewport.y_scale);
  return focal_x > 0.0 && std::fabs(focal_y / focal_x - 1.0) < 0.02;
}

// Constants of the post pass library (xe_scene_*) for turning a stored depth d at a guest
// pixel center (x, y) into a view position:
//   w = w_numerator / (d * w_depth_mul + w_depth_add),
//   view.z = w * z_from_w,
//   view.x = w * (x * x_mul + x_add), view.y = w * (y * y_mul + y_add).
// w is the clip-space W, positive in front of the camera; the view space is the
// game's own (its handedness does not matter for occlusion angles). The depth
// terms are folded on the CPU in double: with reversed Z, ndc_z - c / b would
// cancel in float far from the camera.
struct Unpack {
  float w_numerator, w_depth_mul, w_depth_add;
  float z_from_w;
  float x_mul, x_add, y_mul, y_add;
  // Clip W at the far plane (stored depth of the far plane), to skip the sky.
  float w_far;
  // Pixels per unit of view X and Y at W = 1 (focal length in pixels), to turn
  // a view-space radius into a screen-space one.
  float pixels_per_unit_x, pixels_per_unit_y;
};

// A perspective projection without shear along Z: P[3] = (0, 0, b, 0) and
// P[2] = (0, 0, c, e); P[0] and P[1] may have an off-center term in column 2.
inline bool UnpackFromProjection(const Matrix& p, const Viewport& viewport, Unpack& out) {
  const float b = p.m[3][2];
  if (std::fabs(b) < 1.0e-6f || std::fabs(p.m[3][0]) > 1.0e-4f * std::fabs(b) ||
      std::fabs(p.m[3][1]) > 1.0e-4f * std::fabs(b) || std::fabs(p.m[3][3]) > 1.0e-4f) {
    return false;  // Not a perspective projection (orthographic or skewed).
  }
  if (std::fabs(p.m[0][0]) < 1.0e-6f || std::fabs(p.m[1][1]) < 1.0e-6f ||
      std::fabs(p.m[2][3]) < 1.0e-12f || std::fabs(viewport.z_scale) < 1.0e-12f ||
      std::fabs(viewport.x_scale) < 1.0e-6f || std::fabs(viewport.y_scale) < 1.0e-6f) {
    return false;
  }
  // ndc_z = (c * vz + e) / (b * vz) = c / b + e / w, with w = b * vz, and
  // ndc_z = (d - z_offset) / z_scale.
  const double c_over_b = double(p.m[2][2]) / b;
  out.w_numerator = p.m[2][3];
  out.w_depth_mul = float(1.0 / viewport.z_scale);
  out.w_depth_add = float(-double(viewport.z_offset) / viewport.z_scale - c_over_b);
  out.z_from_w = float(1.0 / b);
  // ndc_x = (p00 * vx + p02 * vz) / w, ndc_x = (x - x_offset) / x_scale.
  //   vx = w * (ndc_x - p02 / b) / p00.
  out.x_mul = float(1.0 / (double(viewport.x_scale) * p.m[0][0]));
  out.x_add = float((-double(viewport.x_offset) / viewport.x_scale - double(p.m[0][2]) / b) /
                    p.m[0][0]);
  out.y_mul = float(1.0 / (double(viewport.y_scale) * p.m[1][1]));
  out.y_add = float((-double(viewport.y_offset) / viewport.y_scale - double(p.m[1][2]) / b) /
                    p.m[1][1]);
  // The far plane is at ndc_z = 1.
  const double far_denominator = 1.0 - c_over_b;
  out.w_far = std::fabs(far_denominator) > 1.0e-12 ? float(p.m[2][3] / far_denominator) : 0.0f;
  if (!(out.w_far > 0.0f)) out.w_far = 1.0e30f;
  out.pixels_per_unit_x = std::fabs(viewport.x_scale * p.m[0][0]);
  out.pixels_per_unit_y = std::fabs(viewport.y_scale * p.m[1][1]);
  return true;
}

// The view position the post pass library reconstructs (for tests).
inline void ViewFromDepth(const Unpack& u, float x, float y, float stored_depth, float view_out[3]) {
  const float w = u.w_numerator / (stored_depth * u.w_depth_mul + u.w_depth_add);
  view_out[0] = w * (x * u.x_mul + u.x_add);
  view_out[1] = w * (y * u.y_mul + u.y_add);
  view_out[2] = w * u.z_from_w;
}

// How a guest vertex shader keeps an object's world matrix (object to world) in
// its float constants, for the motion vectors.
enum class WorldLayout : uint32_t {
  // Row vectors, 4 constants: world = x * c[0] + y * c[1] + z * c[2] + c[3]
  // (FM4's car vertex shaders, matW).
  kRows,
  // Column vectors, 3 constants: world.i = dot(c[i], p).
  kColumns,
};

// constants: the guest float constants from the first of the matrix on (4 for
// kRows, 3 for kColumns). Returns world = M * p as a column-vector 4x4.
inline MatrixD WorldMatrix(WorldLayout layout, const float (*constants)[4]) {
  MatrixD world = IdentityD();
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (layout == WorldLayout::kRows) {
        // Constant i is row i of the row-vector matrix, so column i here.
        world.m[j][i] = constants[i][j];
      } else if (i < 3) {
        world.m[i][j] = constants[i][j];
      }
    }
  }
  return world;
}

// The previous frame's view position of a view position of this frame, as a
// column-vector 4x4: inverse(previous view to world) * previous world *
// inverse(world) * view to world for an object that moved from previous_world
// to world (object to world matrices), or inverse(previous view to world) *
// view to world for the static world (both world matrices nullptr). In double:
// the world translations cancel, the result is in view units.
inline bool ViewToPreviousView(const MatrixD& view_to_world, const MatrixD& previous_view_to_world,
                               const MatrixD* world, const MatrixD* previous_world,
                               MatrixD& out) {
  MatrixD world_to_previous_view;
  if (!Invert(previous_view_to_world, world_to_previous_view)) return false;
  if (!world || !previous_world) {
    out = Multiply(world_to_previous_view, view_to_world);
    return true;
  }
  MatrixD object_from_world;
  if (!Invert(*world, object_from_world)) return false;
  out = Multiply(Multiply(Multiply(world_to_previous_view, *previous_world), object_from_world),
                 view_to_world);
  return true;
}

// The same reprojection between clip positions, as the game's motion blur keeps
// it (FM4's invCurrentVPThenPrevVP): previous projection * view_to_previous_view
// * inverse(projection), for checking the host matrices against the game's.
inline bool ClipToPreviousClip(const MatrixD& view_to_previous_view, const MatrixD& projection,
                               const MatrixD& previous_projection, MatrixD& out) {
  MatrixD clip_to_view;
  if (!Invert(projection, clip_to_view)) return false;
  out = Multiply(Multiply(previous_projection, view_to_previous_view), clip_to_view);
  return true;
}

}  // namespace rex::graphics::d3d12::post_pass
