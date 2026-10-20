// CPU tests of the scene camera math of the post passes
// (rex/graphics/d3d12/post_pass_camera.h): the projection of the matrices in a
// guest vertex shader's constants, in both layouts FM4 uses, the view position
// reconstructed from a stored depth, and the camera's view to world matrix and
// the reprojection into the previous frame's view built from it (as
// post_pass.cpp builds xe_view_to_previous_view), also for a moving car (the
// motion matrices) and in the clip space form of FM4's motion blur. Also the
// scene jitter (rex/graphics/d3d12/post_pass_jitter.h): the Halton (2, 3)
// phases, the shift that its ndc_offset gives a vertex in host pixels, which
// draws it moves, the camera motion that arms it and its gate. Synthetic
// cameras only; prints the worst errors and returns 1 on failure.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include <rex/graphics/d3d12/post_pass_camera.h>
#include <rex/graphics/d3d12/post_pass_jitter.h>

namespace {

using namespace rex::graphics::d3d12::post_pass;

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

struct Vec3 {
  double x, y, z;
};
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Normalize(Vec3 a) {
  double l = std::sqrt(Dot(a, a));
  return {a.x / l, a.y / l, a.z / l};
}

// World to view, looking along +Z (left-handed) or -Z (right-handed).
Matrix LookAt(Vec3 eye, Vec3 target, Vec3 up, bool right_handed) {
  Vec3 z = Normalize(right_handed ? Sub(eye, target) : Sub(target, eye));
  Vec3 x = Normalize(Cross(up, z));
  Vec3 y = Cross(z, x);
  Matrix v = Identity();
  const Vec3 axes[3] = {x, y, z};
  for (int i = 0; i < 3; ++i) {
    v.m[i][0] = float(axes[i].x);
    v.m[i][1] = float(axes[i].y);
    v.m[i][2] = float(axes[i].z);
    v.m[i][3] = float(-Dot(axes[i], eye));
  }
  return v;
}

// D3D style perspective: ndc_z 0 at near, 1 at far; w = +vz (LH) or -vz (RH).
Matrix Perspective(double fov_y, double aspect, double near_z, double far_z, double off_center_x,
                   bool right_handed) {
  double y_scale = 1.0 / std::tan(fov_y * 0.5), x_scale = y_scale / aspect;
  double q = far_z / (far_z - near_z);
  double s = right_handed ? -1.0 : 1.0;
  Matrix p = {};
  p.m[0][0] = float(x_scale);
  p.m[0][2] = float(off_center_x * s);
  p.m[1][1] = float(y_scale);
  p.m[2][2] = float(q * s);
  p.m[2][3] = float(-q * near_z);
  p.m[3][2] = float(s);
  return p;
}

Matrix World(double yaw, Vec3 translation, double scale) {
  Matrix w = Identity();
  double c = std::cos(yaw) * scale, s = std::sin(yaw) * scale;
  w.m[0][0] = float(c);
  w.m[0][2] = float(s);
  w.m[1][1] = float(scale);
  w.m[2][0] = float(-s);
  w.m[2][2] = float(c);
  w.m[0][3] = float(translation.x);
  w.m[1][3] = float(translation.y);
  w.m[2][3] = float(translation.z);
  return w;
}

void Transform(const Matrix& m, const double p[4], double out[4]) {
  for (int i = 0; i < 4; ++i) {
    out[i] = 0.0;
    for (int j = 0; j < 4; ++j) out[i] += double(m.m[i][j]) * p[j];
  }
}

void TestCamera(const char* name, bool right_handed) {
  const double near_z = 0.25, far_z = 8000.0;
  Matrix view = LookAt({10.0, 2.0, -30.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, right_handed);
  Matrix projection =
      Perspective(50.0 * 3.14159265358979 / 180.0, 16.0 / 9.0, near_z, far_z, 0.01, right_handed);
  Matrix world = World(0.5, {5.0, 0.0, 3.0}, 1.0);
  Matrix wv = Multiply(view, world);
  Matrix wvp = Multiply(projection, wv);
  Matrix view_inverse;
  Check(Invert(view, view_inverse), "view inverse");

  // Both constant layouts, with garbage where the shaders do not read.
  static float constants[256][4];
  for (auto& c : constants) c[0] = c[1] = c[2] = c[3] = 0.5f;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      constants[0 + i][j] = wvp.m[j][i];  // Row vectors: constant i is column i.
      if (j < 3) constants[4 + i][j] = wv.m[j][i];
      constants[8 + i][j] = world.m[j][i];
      constants[128 + i][j] = wvp.m[i][j];  // Column vectors: constant i is row i.
      if (i < 3) {
        constants[140 + i][j] = world.m[i][j];
        constants[243 + i][j] = view_inverse.m[i][j];
      }
    }
  }
  const Viewport viewport = {640.0f, 640.0f, -360.0f, 360.0f, -1.0f, 1.0f};  // Reversed Z.

  struct Case {
    const char* layout;
    CameraLayout kind;
    uint32_t a, b, c;
  } cases[] = {{"wvp+wv rows", CameraLayout::kWvpWvRows, 0, 4, 0},
               {"wvp+wv+world rows", CameraLayout::kWvpWvWorldRows, 0, 4, 8},
               {"wvp+world+view inverse", CameraLayout::kWvpWorldViewInverse, 128, 140, 243}};
  for (const Case& test : cases) {
    Matrix wvp_read, wv_read, projection_read;
    bool ok = CameraMatrices(test.kind, constants, test.a, test.b, test.c, wvp_read, wv_read) &&
              ProjectionFromCamera(wvp_read, wv_read, projection_read);
    Check(ok, "projection extraction");
    double projection_error = ok ? MaxRelativeDifference(projection_read, projection) : 1.0;
    Unpack unpack;
    ok = ok && UnpackFromProjection(projection_read, viewport, unpack);
    Check(ok, "unpack constants");

    // Points of the model in front of the camera, from 0.5 m to 7 km.
    // Worst relative error near the camera (up to 500 m) and overall: the
    // projection is float, and with reversed Z its depth terms differ from 1
    // by n / (f - n), so far points lose precision (as in the game's own math).
    double worst = 0.0, worst_near = 0.0;
    const double points[][3] = {{0.0, 0.0, 0.0},       {1.0, 0.5, -1.0},   {-3.0, 2.0, 4.0},
                                {20.0, -1.0, 10.0},    {-40.0, 5.0, 80.0}, {150.0, 3.0, 400.0},
                                {-900.0, 20.0, 3000.0}, {2500.0, 50.0, 6000.0}};
    int visible = 0;
    for (const auto& point : points) {
      double model[4] = {point[0], point[1], point[2], 1.0}, clip[4], view_position[4];
      Transform(wvp, model, clip);
      Transform(wv, model, view_position);
      if (clip[3] <= near_z) continue;
      ++visible;
      double ndc_x = clip[0] / clip[3], ndc_y = clip[1] / clip[3], ndc_z = clip[2] / clip[3];
      float x = float(ndc_x * viewport.x_scale + viewport.x_offset);
      float y = float(ndc_y * viewport.y_scale + viewport.y_offset);
      float depth = float(ndc_z * viewport.z_scale + viewport.z_offset);
      float reconstructed[3];
      ViewFromDepth(unpack, x, y, depth, reconstructed);
      double distance = std::sqrt(view_position[0] * view_position[0] +
                                  view_position[1] * view_position[1] +
                                  view_position[2] * view_position[2]);
      for (int k = 0; k < 3; ++k) {
        double error = std::fabs(reconstructed[k] - view_position[k]) / distance;
        worst = std::fmax(worst, error);
        if (distance < 500.0) worst_near = std::fmax(worst_near, error);
      }
    }
    Check(visible >= 6, "visible test points");
    double far_error = std::fabs(unpack.w_far - far_z) / far_z;
    std::printf(
        "%s %s: projection %.2e, view position %.2e (%.2e within 500 m), far W %.2e, %d points\n",
        name, test.layout, projection_error, worst, worst_near, far_error, visible);
    Check(projection_error < 1.0e-4, "projection matches");
    // 5e-5: 2.5 cm at 500 m, below the 20e4 precision of the game's depth.
    Check(worst_near < 5.0e-5, "view position from depth near the camera");
    Check(worst < 1.0e-3, "view position from depth");
    Check(far_error < 1.0e-3, "far plane W");
  }

  // The view to world matrix: the layouts with the view inverse or the world
  // matrix have it.
  Matrix view_to_world, unused;
  Check(ViewToWorld(CameraLayout::kWvpWorldViewInverse, constants, 140, 243, view_to_world) &&
            MaxRelativeDifference(view_to_world, view_inverse) < 1.0e-6,
        "view to world from the view inverse");
  Check(ViewToWorld(CameraLayout::kWvpWvWorldRows, constants, 4, 8, view_to_world) &&
            MaxRelativeDifference(view_to_world, view_inverse) < 1.0e-6,
        "view to world from the world and world view matrices");
  Check(!ViewToWorld(CameraLayout::kWvpWvRows, constants, 4, 0, unused),
        "no view in the rows layout");
  Check(CarriesView(CameraLayout::kWvpWvWorldRows) &&
            CarriesView(CameraLayout::kWvpWorldViewInverse) &&
            !CarriesView(CameraLayout::kWvpWvRows),
        "layouts with a view");
  // The fallback layout takes a view only from a rotation and a translation.
  Matrix scaled = view_inverse, garbage = {};
  for (int i = 0; i < 3; ++i) scaled.m[i][i] *= 1.5f;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) garbage.m[i][j] = 0.5f;
  }
  Check(IsRigidView(view_inverse) && !IsRigidView(scaled) && !IsRigidView(garbage) &&
            !IsRigidView(wvp),
        "rigid view check");
  // The fallback layout's aspect check: the projection's against the viewport's.
  Check(MatchesViewportAspect(projection, viewport), "the scene's aspect");
  Matrix strip = projection;
  strip.m[1][1] *= 2.5f;  // A wide strip such as the rear-view mirror's.
  Check(!MatchesViewportAspect(strip, viewport), "another aspect");

  // A car and its camera far from the world origin, as on a large track: the
  // world view matrix is float, and the view to world matrix keeps the camera
  // position within a millimeter.
  Matrix far_view = LookAt({2410.0, 31.5, -1840.0}, {2400.0, 30.0, -1800.0}, {0.0, 1.0, 0.0},
                           right_handed);
  Matrix far_world = World(2.1, {2400.0, 30.0, -1800.0}, 1.0);
  Matrix far_wv = Multiply(far_view, far_world), far_view_inverse;
  Check(Invert(far_view, far_view_inverse), "far view inverse");
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (j < 3) constants[4 + i][j] = far_wv.m[j][i];
      constants[8 + i][j] = far_world.m[j][i];
    }
  }
  bool far_ok = ViewToWorld(CameraLayout::kWvpWvWorldRows, constants, 4, 8, view_to_world);
  double position_error = 0.0, axis_error = 0.0;
  for (int i = 0; far_ok && i < 3; ++i) {
    position_error = std::fmax(position_error, std::fabs(double(view_to_world.m[i][3]) -
                                                         double(far_view_inverse.m[i][3])));
    for (int j = 0; j < 3; ++j) {
      axis_error = std::fmax(axis_error, std::fabs(double(view_to_world.m[i][j]) -
                                                   double(far_view_inverse.m[i][j])));
    }
  }
  std::printf("%s far from the origin: camera position %.2e, axes %.2e\n", name, position_error,
              axis_error);
  Check(far_ok && position_error < 1.0e-3 && axis_error < 1.0e-5,
        "view to world far from the origin");
}

// xe_view_to_previous_view = inverse(previous view to world) * view to world
// takes a view position of this frame to the previous frame's view position of
// the same world point.
void TestReprojection(bool right_handed) {
  Matrix previous_view = LookAt({10.0, 2.0, -30.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, right_handed);
  Matrix view = LookAt({11.5, 2.1, -28.0}, {0.5, 1.0, 0.0}, {0.0, 1.0, 0.0}, right_handed);
  Matrix previous_view_to_world, view_to_world, world_to_previous_view;
  bool ok = Invert(previous_view, previous_view_to_world) && Invert(view, view_to_world) &&
            Invert(previous_view_to_world, world_to_previous_view);
  Check(ok, "reprojection inverses");
  Matrix view_to_previous_view = Multiply(world_to_previous_view, view_to_world);
  double worst = 0.0;
  const double points[][3] = {{0.0, 0.0, 0.0}, {3.0, 1.0, 5.0}, {-20.0, 0.0, 60.0}};
  for (const auto& point : points) {
    double world[4] = {point[0], point[1], point[2], 1.0}, now[4], before[4], reprojected[4];
    Transform(view, world, now);
    Transform(previous_view, world, before);
    Transform(view_to_previous_view, now, reprojected);
    for (int k = 0; k < 3; ++k) worst = std::fmax(worst, std::fabs(reprojected[k] - before[k]));
  }
  std::printf("%s reprojection: %.2e\n", right_handed ? "right-handed" : "left-handed", worst);
  Check(worst < 1.0e-4, "view position reprojected into the previous view");
}

// The motion of a car: ViewToPreviousView with the car's world matrices of
// both frames (read from row-vector constants, as FM4's matW c8-c11) takes a
// view position on the car to the previous frame's view position of the same
// point of the car, kilometers from the world origin; without them, a point of
// the static world. ClipToPreviousClip gives the game's clip space form,
// previous VP * previous world * inverse(world) * inverse(VP).
void TestObjectReprojection(bool right_handed) {
  Matrix previous_view =
      LookAt({3000.0, 52.0, -2010.0}, {3020.0, 50.0, -1985.0}, {0.0, 1.0, 0.0}, right_handed);
  Matrix view =
      LookAt({3001.4, 52.05, -2008.2}, {3021.0, 50.0, -1983.0}, {0.0, 1.0, 0.0}, right_handed);
  Matrix previous_world = World(0.20, {3012.0, 50.0, -1992.0}, 1.0);
  Matrix world = World(0.23, {3013.3, 50.02, -1990.4}, 1.0);
  Matrix previous_projection =
      Perspective(62.0 * 3.14159265358979 / 180.0, 16.0 / 9.0, 0.3, 7500.0, 0.0, right_handed);
  Matrix projection =
      Perspective(60.0 * 3.14159265358979 / 180.0, 16.0 / 9.0, 0.3, 7500.0, 0.0, right_handed);

  // The world matrices as a vertex shader keeps them: rows (4 constants, the
  // transpose) and columns (3 constants).
  float rows[2][4][4], columns[2][3][4];
  const Matrix* worlds[2] = {&world, &previous_world};
  for (int k = 0; k < 2; ++k) {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        rows[k][i][j] = worlds[k]->m[j][i];
        if (i < 3) columns[k][i][j] = worlds[k]->m[i][j];
      }
    }
  }
  MatrixD world_d = WorldMatrix(WorldLayout::kRows, rows[0]);
  MatrixD previous_world_d = WorldMatrix(WorldLayout::kRows, rows[1]);
  Check(MaxRelativeDifference(ToFloat(world_d), world) == 0.0 &&
            MaxRelativeDifference(ToFloat(WorldMatrix(WorldLayout::kColumns, columns[0])), world) ==
                0.0,
        "world matrix from row and column constants");

  // The camera as the runtime gets it: a float view to world matrix (the
  // road's view inverse constants), whose double inverse is the view here.
  Matrix view_to_world, previous_view_to_world;
  MatrixD view_d, previous_view_d;
  Check(Invert(view, view_to_world) && Invert(previous_view, previous_view_to_world) &&
            Invert(ToDouble(view_to_world), view_d) &&
            Invert(ToDouble(previous_view_to_world), previous_view_d),
        "object reprojection inverses");
  MatrixD car, static_world;
  Check(ViewToPreviousView(ToDouble(view_to_world), ToDouble(previous_view_to_world), &world_d,
                           &previous_world_d, car) &&
            ViewToPreviousView(ToDouble(view_to_world), ToDouble(previous_view_to_world), nullptr,
                               nullptr, static_world),
        "view to previous view");
  auto transform = [](const MatrixD& m, const double p[4], double out[4]) {
    for (int i = 0; i < 4; ++i) {
      out[i] = 0.0;
      for (int j = 0; j < 4; ++j) out[i] += m.m[i][j] * p[j];
    }
  };
  double worst_car = 0.0, worst_world = 0.0;
  const double points[][3] = {{0.0, 0.0, 0.0}, {2.1, 0.6, -0.9}, {-0.8, 1.2, 1.9}};
  for (const auto& point : points) {
    double object[4] = {point[0], point[1], point[2], 1.0}, on_car[4], before_on_car[4];
    transform(world_d, object, on_car);
    transform(previous_world_d, object, before_on_car);
    double now[4], before[4], reprojected[4];
    // The car.
    transform(view_d, on_car, now);
    transform(previous_view_d, before_on_car, before);
    transform(car, now, reprojected);
    for (int k = 0; k < 3; ++k) {
      worst_car = std::fmax(worst_car, std::fabs(reprojected[k] - before[k]));
    }
    // The same world position on the static world.
    transform(previous_view_d, on_car, before);
    transform(static_world, now, reprojected);
    for (int k = 0; k < 3; ++k) {
      worst_world = std::fmax(worst_world, std::fabs(reprojected[k] - before[k]));
    }
  }
  // The game's form, in double.
  MatrixD clip, game, vp_inverse, world_inverse;
  MatrixD vp = Multiply(ToDouble(projection), view_d);
  MatrixD previous_vp = Multiply(ToDouble(previous_projection), previous_view_d);
  bool ok = ClipToPreviousClip(car, ToDouble(projection), ToDouble(previous_projection), clip) &&
            Invert(vp, vp_inverse) && Invert(world_d, world_inverse);
  game = Multiply(Multiply(Multiply(previous_vp, previous_world_d), world_inverse), vp_inverse);
  double clip_error = 0.0;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      clip_error = std::fmax(clip_error, std::fabs(clip.m[i][j] - game.m[i][j]));
    }
  }
  std::printf("%s object reprojection: car %.2e m, static world %.2e m, clip form %.2e\n",
              right_handed ? "right-handed" : "left-handed", worst_car, worst_world, clip_error);
  // 1e-5 m: the float inputs are exact in double, the error is that of double
  // at kilometers.
  Check(worst_car < 1.0e-5, "car point reprojected into the previous view");
  Check(worst_world < 1.0e-5, "static world point reprojected into the previous view");
  Check(ok && clip_error < 1.0e-6, "clip form equals previous VP * previous W * W^-1 * VP^-1");
}

void TestRejectsOrthographic() {
  Matrix ortho = Identity();
  ortho.m[0][0] = 0.01f;
  ortho.m[1][1] = 0.01f;
  ortho.m[2][2] = 0.001f;
  Unpack unpack;
  Check(!UnpackFromProjection(ortho, {640.0f, 640.0f, -360.0f, 360.0f, 1.0f, 0.0f}, unpack),
        "orthographic rejected");
}

// The jitter: 8 phases inside the pixel, none on its edge, spread evenly; the
// ndc_offset moving a vertex by exactly the jitter in host pixels for host
// viewports of 1x, 2x and 3x (and of the huge viewport of draws without
// clipping); and the draws the rules jitter.
void TestJitter() {
  Check(Halton(1, 2) == 0.5f && Halton(2, 2) == 0.25f && Halton(3, 2) == 0.75f &&
            std::fabs(Halton(1, 3) - 1.0f / 3.0f) < 1.0e-7f &&
            std::fabs(Halton(4, 3) - 4.0f / 9.0f) < 1.0e-7f,
        "Halton sequence");
  double mean[2] = {}, closest = 1.0;
  float offsets[8][2];
  for (uint32_t phase = 0; phase < 8; ++phase) {
    JitterOffset(phase, offsets[phase]);
    for (int k = 0; k < 2; ++k) {
      Check(offsets[phase][k] > -0.5f && offsets[phase][k] < 0.5f, "jitter inside the pixel");
      mean[k] += offsets[phase][k] / 8.0;
    }
    for (uint32_t other = 0; other < phase; ++other) {
      closest = std::fmin(closest, std::hypot(offsets[phase][0] - offsets[other][0],
                                              offsets[phase][1] - offsets[other][1]));
    }
  }
  std::printf("jitter: 8 phases, mean %.4f %.4f, closest pair %.4f px\n", mean[0], mean[1],
              closest);
  Check(std::fabs(mean[0]) < 0.06 && std::fabs(mean[1]) < 0.06, "jitter phases centered");
  Check(closest > 0.15, "jitter phases spread");

  // A vertex at clip (x, y, z, w): host pixel = offset + (ndc * scale + ndc
  // offset + 1) / 2 * extent along X, and (1 - ...) / 2 * extent along Y.
  double worst = 0.0;
  const uint32_t extents[][2] = {{1280, 720}, {2560, 1440}, {3840, 2160}, {8192, 8192}};
  for (const auto& extent : extents) {
    for (uint32_t phase = 0; phase < 8; ++phase) {
      const float ndc_scale[2] = {0.75f, -1.25f}, base_offset[3] = {0.0125f, -0.03f, 0.5f};
      float ndc_offset[3] = {base_offset[0], base_offset[1], base_offset[2]};
      AddJitterToNdcOffset(offsets[phase], extent, ndc_offset);
      Check(ndc_offset[2] == base_offset[2], "jitter leaves Z alone");
      const double clip[2] = {0.3, -0.6}, w = 7.5;
      for (int k = 0; k < 2; ++k) {
        double before = (clip[k] * ndc_scale[k] + base_offset[k] * w) / w;
        double after = (clip[k] * ndc_scale[k] + ndc_offset[k] * w) / w;
        double pixels = (after - before) * 0.5 * extent[k] * (k ? -1.0 : 1.0);
        worst = std::fmax(worst, std::fabs(pixels - offsets[phase][k]));
      }
    }
  }
  std::printf("jitter: worst vertex shift error %.2e px\n", worst);
  Check(worst < 2.0e-3, "jitter moves a vertex by the jitter in host pixels");

  JitterDraw scene;
  scene.depth_base = scene.scene_depth_base = 1024;
  scene.scene_depth_known = true;
  scene.depth_or_stencil = true;
  Check(IsJitteredDraw(scene), "a scene draw is jittered");
  JitterDraw draw = scene;
  draw.scene_depth_known = false;
  Check(!IsJitteredDraw(draw), "nothing jittered before the scene depth is known");
  draw = scene;
  draw.depth_base = 80;  // The rear-view mirror's depth buffer.
  Check(!IsJitteredDraw(draw), "another depth buffer is not jittered");
  draw = scene;
  draw.depth_or_stencil = false;  // A post chain draw with the scene's depth named.
  Check(!IsJitteredDraw(draw), "a draw without depth or stencil test is not jittered");
  draw = scene;
  draw.rectangle = true;
  Check(!IsJitteredDraw(draw), "rectangles and clears are not jittered");
  draw = scene;
  draw.excluded = true;
  Check(!IsJitteredDraw(draw), "excluded vertex shaders are not jittered");
  draw = scene;
  draw.after_final = true;  // The pause menu tests the scene's depth buffer.
  Check(!IsJitteredDraw(draw), "draws after the frame's final combine are not jittered");
}

// The camera's motion in host pixels (CameraMotionPixels) against the same
// points projected through both frames' matrices: none for a still camera, a
// turn, a move forward with the focal length widening (as with speed), and the
// host pixels of a 2x scene. Then the gate: armed while the camera moves and
// for its hold after, never at 30 fps, a single long frame ignored, and with
// both thresholds at 0 armed in every frame, as before the gate.
void TestJitterGate(bool right_handed) {
  const double pi = 3.14159265358979;
  const Viewport viewport = {640.0f, 640.0f, -360.0f, 360.0f, -1.0f, 1.0f};
  Matrix projection = Perspective(50.0 * pi / 180.0, 16.0 / 9.0, 0.25, 8000.0, 0.0, right_handed);
  Matrix wider = Perspective(51.0 * pi / 180.0, 16.0 / 9.0, 0.25, 8000.0, 0.0, right_handed);
  const double distance = 10.0;
  // The largest pixel move of the points at W = distance through the 3x3 grid,
  // with the matrices: world = inverse(view) * view position, then the
  // previous projection and viewport.
  auto expected = [&](const Matrix& view, const Matrix& previous_view,
                      const Matrix& previous_projection, const Unpack& unpack, double scale) {
    Matrix view_to_world;
    Invert(view, view_to_world);
    double largest = 0.0;
    for (int iy = 0; iy < 3; ++iy) {
      for (int ix = 0; ix < 3; ++ix) {
        float x = float(640.0 * ix), y = float(360.0 * iy);
        double view_position[4] = {distance * (x * unpack.x_mul + unpack.x_add),
                                   distance * (y * unpack.y_mul + unpack.y_add),
                                   distance * unpack.z_from_w, 1.0};
        double world[4], before[4], clip[4];
        Transform(view_to_world, view_position, world);
        Transform(previous_view, world, before);
        Transform(previous_projection, before, clip);
        double px = clip[0] / clip[3] * viewport.x_scale + viewport.x_offset;
        double py = clip[1] / clip[3] * viewport.y_scale + viewport.y_offset;
        largest = std::fmax(largest, std::hypot(px - x, py - y) * scale);
      }
    }
    return largest;
  };
  Unpack unpack, wider_unpack;
  Check(UnpackFromProjection(projection, viewport, unpack) &&
            UnpackFromProjection(wider, viewport, wider_unpack),
        "gate: unpack constants");
  Matrix view = LookAt({10.0, 2.0, -30.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, right_handed);
  struct Case {
    const char* name;
    Matrix previous_view;
    bool widened;
    double scale;
    bool still;
  };
  // A turn of 0.2 degrees about the eye, and a move of 0.1 m forward (24 m/s at
  // 240 fps) while the field of view widens by a degree.
  const double turn = 0.2 * pi / 180.0;
  Vec3 forward = Normalize(Sub({0.0, 1.0, 0.0}, {10.0, 2.0, -30.0}));
  Vec3 turned_target = {10.0 - 10.0 * std::cos(turn) + 30.0 * std::sin(turn), 1.0,
                        -30.0 + 10.0 * std::sin(turn) + 30.0 * std::cos(turn)};
  Matrix turned = LookAt({10.0, 2.0, -30.0}, turned_target, {0.0, 1.0, 0.0}, right_handed);
  Matrix behind = LookAt({10.0 - 0.1 * forward.x, 2.0 - 0.1 * forward.y, -30.0 - 0.1 * forward.z},
                         {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, right_handed);
  const Case cases[] = {
      {"still", view, false, 1.0, true},
      {"turn", turned, false, 1.0, false},
      {"forward", behind, true, 1.0, false},
      {"turn at 2x", turned, false, 2.0, false},
  };
  for (const Case& test : cases) {
    Matrix view_to_world, previous_view_to_world;
    MatrixD reprojection;
    bool ok = Invert(view, view_to_world) && Invert(test.previous_view, previous_view_to_world) &&
              ViewToPreviousView(ToDouble(view_to_world), ToDouble(previous_view_to_world),
                                 nullptr, nullptr, reprojection);
    const Unpack& previous = test.widened ? wider_unpack : unpack;
    double motion = ok ? CameraMotionPixels(reprojection, unpack, previous, 1280.0, 720.0,
                                            test.scale, test.scale, distance)
                       : -1.0;
    double reference =
        expected(view, test.previous_view, test.widened ? wider : projection, unpack, test.scale);
    std::printf("%s gate, %s: camera motion %.4f px (matrices %.4f px)\n",
                right_handed ? "right-handed" : "left-handed", test.name, motion, reference);
    // The matrices are float: 0.01 px and 0.1% of the motion.
    Check(ok && std::fabs(motion - reference) < 1.0e-2 + 1.0e-3 * reference,
          "gate: camera motion in host pixels");
    Check(test.still ? motion < 1.0e-6 : motion > 1.0, "gate: no motion of a still camera only");
  }

  JitterGateSettings settings;
  settings.min_motion = 0.5;
  settings.hold = 8;
  settings.min_frame_rate = 45.0;
  JitterGate gate;
  Check(!gate.Armed(settings), "gate: not armed before a frame");
  for (int i = 0; i < 10; ++i) gate.EndFrame(2.0, 1.0 / 240.0, settings);
  Check(gate.Armed(settings) && std::fabs(gate.FrameRate() - 240.0) < 1.0e-6,
        "gate: armed on a moving camera at 240 fps");
  int still_armed = 0;
  for (int i = 0; i < 12; ++i) {
    gate.EndFrame(i % 2 ? 0.1 : -1.0, 1.0 / 240.0, settings);
    still_armed += gate.Armed(settings);
  }
  Check(still_armed == 7 && !gate.Armed(settings),
        "gate: held for 8 frames after the camera stops, unknown motion as still");
  gate.EndFrame(0.6, 1.0 / 240.0, settings);
  Check(gate.Armed(settings), "gate: armed again when the camera moves");
  for (int i = 0; i < 8; ++i) gate.EndFrame(3.0, 1.0 / 30.0, settings);
  Check(!gate.Armed(settings) && std::fabs(gate.FrameRate() - 30.0) < 1.0e-6,
        "gate: never at 30 fps");
  for (int i = 0; i < 8; ++i) gate.EndFrame(3.0, i == 3 ? 0.2 : 1.0 / 60.0, settings);
  Check(gate.Armed(settings) && std::fabs(gate.FrameRate() - 60.0) < 1.0e-6,
        "gate: a single long frame does not change the frame rate");
  JitterGateSettings always;
  JitterGate fresh;
  fresh.EndFrame(-1.0, 0.0, always);
  Check(fresh.Armed(always), "gate: thresholds at 0 arm every frame");

  // With still: also on a camera still for hold frames, at any frame rate, and
  // marked still (the consumer accumulates); not right after a cut.
  JitterGateSettings with_still = settings;
  with_still.still = true;
  JitterGate still_gate;
  for (int i = 0; i < 10; ++i) still_gate.EndFrame(2.0, 1.0 / 240.0, with_still);
  Check(still_gate.Armed(with_still) && !still_gate.Still(with_still),
        "gate: still: a moving camera is armed, not still");
  int armed = 0, still = 0;
  for (int i = 0; i < 12; ++i) {
    still_gate.EndFrame(0.1, 1.0 / 240.0, with_still);
    armed += still_gate.Armed(with_still);
    still += still_gate.Still(with_still);
  }
  Check(armed == 12 && still == 5,
        "gate: still: armed throughout, still from the camera's 8th still frame");
  for (int i = 0; i < 8; ++i) still_gate.EndFrame(0.0, 1.0 / 30.0, with_still);
  Check(still_gate.Armed(with_still) && still_gate.Still(with_still),
        "gate: still: armed on a still camera at 30 fps");
  still_gate.EndFrame(-1.0, 1.0 / 30.0, with_still);
  Check(!still_gate.Armed(with_still) && !still_gate.Still(with_still),
        "gate: still: not right after a cut");
  still_gate.EndFrame(0.0, 1.0 / 30.0, with_still);
  Check(still_gate.Still(with_still), "gate: still: again in the frame after the cut");
  for (int i = 0; i < 8; ++i) still_gate.EndFrame(3.0, 1.0 / 30.0, with_still);
  Check(!still_gate.Armed(with_still), "gate: still: a moving camera at 30 fps stays off");
  JitterGateSettings still_always = always;
  still_always.still = true;
  Check(!fresh.Still(still_always), "gate: still: never with min_motion 0 (every frame jittered)");
}

}  // namespace

int main() {
  TestJitter();
  TestCamera("left-handed", false);
  TestCamera("right-handed", true);
  TestReprojection(false);
  TestReprojection(true);
  TestObjectReprojection(false);
  TestObjectReprojection(true);
  TestRejectsOrthographic();
  TestJitterGate(false);
  TestJitterGate(true);
  std::printf(failures ? "post pass camera tests: %d failed\n" : "post pass camera tests: passed\n",
              failures);
  return failures ? 1 : 0;
}
