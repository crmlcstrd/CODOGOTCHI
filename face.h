#pragma once
#include <stdint.h>
#include <math.h>

// ---- drawing surface --------------------------------------------------------
struct Canvas {
  virtual ~Canvas() {}
  virtual void clear() = 0;
  virtual void fillRoundRect(int x, int y, int w, int h, int r, bool on) = 0;
  virtual void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on) = 0;
  virtual void drawLine(int x0, int y0, int x1, int y1, bool on) = 0;
};

// ---- moods and reactions ----------------------------------------------------
enum Mood { MOOD_SAD = 0, MOOD_NEUTRAL = 1, MOOD_SLIGHTLY_HAPPY = 2, MOOD_HAPPY = 3 };
enum Reaction { REACT_NONE = 0, REACT_PET, REACT_CELEBRATE };
enum Expr { EX_NEUTRAL, EX_SLIGHTLY_HAPPY, EX_HAPPY, EX_SAD, EX_SLEEPY, EX_PETTED, EX_CELEBRATE };

// ---- tunables ---------------------------------------------------------------
namespace facecfg {
const int kCenterX = 64;
const int kCenterY = 30;
const float kMorphSpeed = 8.0f;
const float kBlinkSeconds = 0.18f;
const int kSleepHealth = 15;
const int kDroopBelow = 60;
const uint32_t kPetMs = 2200;
const uint32_t kCelebrateMs = 2800;
const float kLookPxX = 12.0f;
const float kLookPxY = 6.0f;
}

// ---- pose -------------------------------------------------------------------
struct Pose {
  float w, h, r, gap;
  float tired;
  float happy;
  float bob;
  float bobHz;
  float gaze;
  float gazeBias;
  float blinkEvery;
};

inline Pose presetFor(Expr e) {
  Pose p = {32, 34, 12, 16, 0.00f, 0.00f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f};

  switch (e) {
    case EX_SLIGHTLY_HAPPY: {
      Pose q = {32, 29, 13, 16, 0.00f, 0.25f, 0.4f, 0.4f, 1.0f, 0.0f, 4.0f};
      return q;
    }
    case EX_HAPPY: {
      Pose q = {38, 38, 17, 12, 0.00f, 1.00f, 2.0f, 1.2f, 1.0f, -0.1f, 3.0f};
      return q;
    }
    case EX_SAD: {
      Pose q = {30, 30, 11, 18, 0.65f, 0.00f, 0.0f, 0.0f, 0.35f, 0.7f, 6.0f};
      return q;
    }
    case EX_SLEEPY: {
      Pose q = {32, 15, 8, 16, 0.90f, 0.00f, 0.8f, 0.25f, 0.0f, 0.3f, 7.0f};
      return q;
    }
    case EX_PETTED: {
      Pose q = {35, 28, 13, 14, 0.00f, 1.00f, 1.0f, 2.0f, 0.0f, 0.0f, 99.0f};
      return q;
    }
    case EX_CELEBRATE: {
      Pose q = {35, 36, 14, 12, 0.00f, 0.55f, 4.0f, 2.5f, 0.0f, -0.3f, 99.0f};
      return q;
    }
    default:
      return p;
  }
}

inline void approachPose(Pose& c, const Pose& t, float k) {
  c.w += (t.w - c.w) * k;
  c.h += (t.h - c.h) * k;
  c.r += (t.r - c.r) * k;
  c.gap += (t.gap - c.gap) * k;
  c.tired += (t.tired - c.tired) * k;
  c.happy += (t.happy - c.happy) * k;
  c.bob += (t.bob - c.bob) * k;
  c.bobHz += (t.bobHz - c.bobHz) * k;
  c.gaze += (t.gaze - c.gaze) * k;
  c.gazeBias += (t.gazeBias - c.gazeBias) * k;
  c.blinkEvery += (t.blinkEvery - c.blinkEvery) * k;
}

// ---- face -------------------------------------------------------------------
class Face {
 public:
  Face() { seed(1); }

  void seed(uint32_t s) {
    rng_ = s ? s : 1;
  }

  void setMood(int m) {
    mood_ = m < 0 ? 0 : (m > 3 ? 3 : m);
  }

  void setHealth(int h) {
    health_ = h < 0 ? 0 : (h > 100 ? 100 : h);
  }

  void react(Reaction r, uint32_t nowMs) {
    if (r == REACT_NONE) return;
    if (reaction_ == REACT_CELEBRATE && r == REACT_PET) return;

    reaction_ = r;
    reactEnd_ = nowMs + (r == REACT_PET ? facecfg::kPetMs : facecfg::kCelebrateMs);
  }

  Expr expression() const {
    if (reaction_ == REACT_PET) return EX_PETTED;
    if (reaction_ == REACT_CELEBRATE) return EX_CELEBRATE;
    if (health_ <= facecfg::kSleepHealth) return EX_SLEEPY;

    switch (mood_) {
      case MOOD_HAPPY: return EX_HAPPY;
      case MOOD_SLIGHTLY_HAPPY: return EX_SLIGHTLY_HAPPY;
      case MOOD_SAD: return EX_SAD;
      default: return EX_NEUTRAL;
    }
  }

  bool blinking() const {
    return blinkPhase_ >= 0.0f;
  }

  void update(uint32_t nowMs) {
    float dt = 0.033f;

    if (started_) {
      dt = (float)(nowMs - lastMs_) * 0.001f;
      if (dt > 0.1f) dt = 0.1f;
    }

    lastMs_ = nowMs;

    if (reaction_ != REACT_NONE && (int32_t)(nowMs - reactEnd_) >= 0)
      reaction_ = REACT_NONE;

    Pose target = targetPose();

    if (!started_) {
      cur_ = target;
      started_ = true;
    } else {
      approachPose(cur_, target, 1.0f - expf(-facecfg::kMorphSpeed * dt));
    }

    stepBlink(dt);
    stepGaze(dt);
    stepBob(dt);

    clock_ += dt;
  }

  void render(Canvas& c) const {
    c.clear();

    const float open = openness();

    int w = (int)floorf(cur_.w + 0.5f);
    int h = (int)floorf(cur_.h * open + 0.5f);
    if (h < 2) h = 2;

    int gap = (int)floorf(cur_.gap + 0.5f);
    int r = (int)floorf(cur_.r + 0.5f);

    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;

    const int cx = facecfg::kCenterX +
      (int)floorf(gcx_ * facecfg::kLookPxX * 0.7f + 0.5f);

    const int cy = facecfg::kCenterY +
      (int)floorf(gcy_ * facecfg::kLookPxY + 0.5f) +
      (int)floorf(bobPx_ + 0.5f);

    for (int i = 0; i < 2; i++) {
      const int x = (i == 0) ? cx - gap / 2 - w : cx + gap / 2;
      const int y = cy - h / 2;

      c.fillRoundRect(x, y, w, h, r, true);

      // Thinner upper eyelids
      if (cur_.tired > 0.02f) {
        const int cut = y + (int)(h * 0.35f * cur_.tired);

        if (i == 0)
          c.fillTriangle(x - 1, y - 1, x + w + 1, y - 1, x - 1, cut, false);
        else
          c.fillTriangle(x - 1, y - 1, x + w + 1, y - 1, x + w + 1, cut, false);
      }

      if (cur_.happy > 0.02f) {
        const int off = (int)(h * 0.25f * cur_.happy);
        c.fillRoundRect(x - 1, y + h - off, w + 2, h + 4, r, false);
      }
    }

    drawMouth(c);

    if (reaction_ == REACT_NONE && health_ <= facecfg::kSleepHealth)
      drawZ(c);
  }

 private:

  // ---- animated mouth -------------------------------------------------------
  void drawMouth(Canvas& c) const {
    const int cx = facecfg::kCenterX;

    const int cy = facecfg::kCenterY +
      (int)floorf(gcy_ * facecfg::kLookPxY + 0.5f) +
      (int)floorf(bobPx_ + 0.5f) + 21;

    switch (expression()) {

      case EX_NEUTRAL:
        c.drawLine(cx - 5, cy, cx + 5, cy, true);
        break;

      case EX_SLIGHTLY_HAPPY:
        c.drawLine(cx - 5, cy, cx - 2, cy + 2, true);
        c.drawLine(cx - 2, cy + 2, cx + 2, cy + 2, true);
        c.drawLine(cx + 2, cy + 2, cx + 5, cy, true);
        break;

      case EX_HAPPY:
      case EX_PETTED;
        c.drawLine(cx - 10, cy - 2, cx - 6, cy + 3, true);
        c.drawLine(cx - 6, cy + 3, cx, cy + 6, true);
        c.drawLine(cx, cy + 6, cx + 6, cy + 3, true);
        c.drawLine(cx + 6, cy + 3, cx + 10, cy - 2, true);
        break;

      case EX_SAD:
        c.drawLine(cx - 7, cy + 2, cx - 3, cy - 1, true);
        c.drawLine(cx - 3, cy - 1, cx + 3, cy - 1, true);
        c.drawLine(cx + 3, cy - 1, cx + 7, cy + 2, true);
        break;

      case EX_SLEEPY:
        c.drawLine(cx - 4, cy, cx + 4, cy, true);
        break;

      case EX_CELEBRATE: {
        const float pulse = (sinf(clock_ * 10.0f) + 1.0f) * 0.5f;

        const int mouthH = 3 + (int)(pulse * 7.0f);
        const int mouthW = 8 + (int)(pulse * 3.0f);

        c.fillRoundRect(
          cx - mouthW / 2,
          cy - mouthH / 2,
          mouthW,
          mouthH,
          3,
          true
        );
        break;
      }
    }
  }

  Pose targetPose() const {
    const Expr e = expression();
    Pose t = presetFor(e);

    if (reaction_ == REACT_NONE && e != EX_SLEEPY && health_ < facecfg::kDroopBelow) {
      const float d = (float)(facecfg::kDroopBelow - health_) /
                      (float)facecfg::kDroopBelow;

      t.tired += 0.5f * d;
      if (t.tired > 1.0f) t.tired = 1.0f;

      t.h *= 1.0f - 0.2f * d;
      t.gaze *= 1.0f - 0.5f * d;
    }

    return t;
  }

  float rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(rng_ & 0xFFFFFF) / 16777216.0f;
  }

  float openness() const {
    if (blinkPhase_ < 0.0f) return 1.0f;

    const float p = blinkPhase_;
    const float tri = p < 0.5f ? 1.0f - 2.0f * p : 2.0f * p - 1.0f;

    return 0.04f + 0.96f * tri;
  }

  void stepBlink(float dt) {
    if (blinkPhase_ >= 0.0f) {
      blinkPhase_ += dt / facecfg::kBlinkSeconds;

      if (blinkPhase_ >= 1.0f) {
        blinkPhase_ = -1.0f;
        blinkTimer_ = cur_.blinkEvery * (0.5f + rnd());

        if (rnd() < 0.15f)
          blinkTimer_ = 0.15f;
      }
    } else if (cur_.blinkEvery < 50.0f) {
      blinkTimer_ -= dt;

      if (blinkTimer_ <= 0.0f)
        blinkPhase_ = 0.0f;
    }
  }

  void stepGaze(float dt) {
    gazeTimer_ -= dt;

    if (gazeTimer_ <= 0.0f) {
      gazeTimer_ = 1.2f + 3.0f * rnd();

      if (rnd() < 0.3f) {
        gx_ = 0.0f;
        gy_ = 0.0f;
      } else {
        gx_ = rnd() * 2.0f - 1.0f;
        gy_ = (rnd() * 2.0f - 1.0f) * 0.6f;
      }
    }

    const float k = 1.0f - expf(-10.0f * dt);
    const float tx = gx_ * cur_.gaze;

    float ty = gy_ * cur_.gaze + cur_.gazeBias;

    if (ty > 1.0f) ty = 1.0f;
    if (ty < -1.0f) ty = -1.0f;

    gcx_ += (tx - gcx_) * k;
    gcy_ += (ty - gcy_) * k;
  }

  void stepBob(float dt) {
    bobPhase_ += 6.2831853f * cur_.bobHz * dt;

    if (bobPhase_ > 6.2831853f)
      bobPhase_ -= 6.2831853f;

    bobPx_ = cur_.bob * sinf(bobPhase_);
  }

  void drawZ(Canvas& c) const {
    const float t = fmodf(clock_ * 0.5f, 1.0f);

    const int s = 5 + (int)(t * 3.0f);
    const int zx = 98 + (int)(t * 10.0f);
    const int zy = 14 - (int)(t * 10.0f);

    c.drawLine(zx, zy, zx + s, zy, true);
    c.drawLine(zx + s, zy, zx, zy + s, true);
    c.drawLine(zx, zy + s, zx + s, zy + s, true);
  }

  // ---- state ----------------------------------------------------------------
  Pose cur_ = {32, 34, 9, 16, 0, 0, 0, 0, 1, 0, 4};

  int mood_ = MOOD_NEUTRAL;
  int health_ = 60;

  Reaction reaction_ = REACT_NONE;

  uint32_t reactEnd_ = 0;
  uint32_t lastMs_ = 0;
  uint32_t rng_ = 1;

  bool started_ = false;

  float clock_ = 0.0f;
  float blinkPhase_ = -1.0f;
  float blinkTimer_ = 2.0f;

  float gazeTimer_ = 1.0f;
  float gx_ = 0.0f, gy_ = 0.0f;
  float gcx_ = 0.0f, gcy_ = 0.0f;

  float bobPhase_ = 0.0f;
  float bobPx_ = 0.0f;
};
