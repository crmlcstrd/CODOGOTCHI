#ifndef ACCESSORIES_H
#define ACCESSORIES_H
/*
  CODOGOTCHI - 36 Accessories (ported from TS -> Adafruit GFX)
  -----------------------------------------------------------
  Mirrors the TypeScript definitions in accessories.ts / accessories-eyes.ts /
  accessories-mouth.ts 1:1. Every draw call uses only Adafruit_GFX primitives
  so the rendering is identical on the 1.3" 128x64 SH1106.

  Slot layout on the face:
    Eyes  : centered at y = 26, left cx = 38, right cx = 90, width 26, height 28
    Mouth : centered at (64, 50)
    Cheeks: around y = 42..55, x = 10..118

  Color convention (from TS):  true  -> SH110X_WHITE (draw)
                                false -> SH110X_BLACK (erase / cutout)
*/

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

extern Adafruit_SH1106G display;   // defined in sketch.ino.cpp

// ---------- Face frame (matches TS FaceFrame) ----------
struct FaceFrame {
  int cx, cy;              // face center
  int w, h;                // eye width / height
  int leftEyeX, rightEyeX; // top-left X of each eye bounding box
  int eyeY;                // top Y of eyes
  int mouthX, mouthY;      // mouth center
  float openness;          // 0..1 (eye open amount)
};

static inline void buildFaceFrame(FaceFrame& f, float openness) {
  f.cx = 64; f.cy = 26;
  f.w = 26; f.h = 28;
  f.leftEyeX  = 38 - f.w / 2;   // 25
  f.rightEyeX = 90 - f.w / 2;   // 77
  f.eyeY   = f.cy - f.h / 2;    // 12
  f.mouthX = 64;
  f.mouthY = 50;
  f.openness = openness;
}

// ---------- shared helpers (ported from TS) ----------
static void drawHollowRoundRect(int x, int y, int w, int h, int r, int t = 2) {
  display.fillRoundRect(x, y, w, h, r, SH110X_WHITE);
  int innerR = r - t; if (innerR < 0) innerR = 0;
  display.fillRoundRect(x + t, y + t, w - t * 2, h - t * 2, innerR, SH110X_BLACK);
}

static void drawInnerPupils(const FaceFrame& f, int inset = 10) {
  int ew = f.w - inset; if (ew < 10) ew = 10;
  int eh = (int)((f.h - 8) * f.openness); if (eh < 3) eh = 3;
  int rad = eh / 2; if (rad > 5) rad = 5;
  int lx = f.leftEyeX  + (f.w - ew) / 2;
  int rx = f.rightEyeX + (f.w - ew) / 2;
  int yy = f.cy - eh / 2;
  display.fillRoundRect(lx, yy, ew, eh, rad, SH110X_WHITE);
  display.fillRoundRect(rx, yy, ew, eh, rad, SH110X_WHITE);
}

// ============================================================
//  EYE ACCESSORIES  (cosmeticId 1,4,7,8,11,12,15,17,19,20,22,24,25,27,29,30,32,35)
// ============================================================

static void acc_pixel_shades(const FaceFrame& f) {
  int top = f.cy - 9;
  int lx = f.leftEyeX - 3, rx = f.rightEyeX - 2;
  int lw = f.w + 5;
  display.fillRoundRect(lx - 1, top - 1, lw + 2, 17, 1, SH110X_BLACK);
  display.fillRoundRect(rx - 1, top - 1, lw + 2, 17, 1, SH110X_BLACK);
  display.fillRoundRect(lx, top, lw, 14, 1, SH110X_WHITE);
  display.fillRoundRect(rx, top, lw, 14, 1, SH110X_WHITE);
  display.fillRect(lx + 4, top + 14, lw - 8, 2, SH110X_WHITE);
  display.fillRect(rx + 4, top + 14, lw - 8, 2, SH110X_WHITE);
  display.fillRect(lx + lw - 1, top + 2, rx - (lx + lw) + 2, 3, SH110X_WHITE);
  display.drawFastHLine(lx - 6, top + 2, 6, SH110X_WHITE);
  display.drawFastHLine(lx - 6, top + 3, 6, SH110X_WHITE);
  display.drawFastHLine(rx + lw, top + 2, 6, SH110X_WHITE);
  display.drawFastHLine(rx + lw, top + 3, 6, SH110X_WHITE);
  for (int ox : {lx, rx}) {
    display.fillRect(ox + 5,  top + 3, 3, 3, SH110X_BLACK);
    display.fillRect(ox + 8,  top + 6, 3, 3, SH110X_BLACK);
    display.fillRect(ox + 11, top + 9, 3, 3, SH110X_BLACK);
    display.fillRect(ox + 12, top + 3, 3, 3, SH110X_BLACK);
    display.fillRect(ox + 15, top + 6, 3, 3, SH110X_BLACK);
  }
}

static void acc_flutter_lashes(const FaceFrame& f) {
  int ly = f.eyeY + 3;
  display.drawLine(f.leftEyeX + 1, ly,     f.leftEyeX - 6, ly - 4, SH110X_WHITE);
  display.drawLine(f.leftEyeX + 1, ly + 1, f.leftEyeX - 6, ly - 3, SH110X_WHITE);
  display.drawLine(f.leftEyeX + 3, ly - 1, f.leftEyeX - 3, ly - 7, SH110X_WHITE);
  display.drawLine(f.leftEyeX,     ly + 4, f.leftEyeX - 6, ly + 2, SH110X_WHITE);
  int rx = f.rightEyeX + f.w - 1;
  display.drawLine(rx - 1, ly,     rx + 6, ly - 4, SH110X_WHITE);
  display.drawLine(rx - 1, ly + 1, rx + 6, ly - 3, SH110X_WHITE);
  display.drawLine(rx - 3, ly - 1, rx + 3, ly - 7, SH110X_WHITE);
  display.drawLine(rx,     ly + 4, rx + 6, ly + 2, SH110X_WHITE);
}

static void acc_wire_specs(const FaceFrame& f) {
  int fw = f.w + 8, fh = 24;
  int top = f.cy - 12;
  int lx = f.leftEyeX - 4, rx = f.rightEyeX - 4;
  drawHollowRoundRect(lx, top, fw, fh, 3, 1);
  drawHollowRoundRect(rx, top, fw, fh, 3, 1);
  drawInnerPupils(f, 12);
  display.drawFastHLine(lx + fw, f.cy - 2, rx - (lx + fw), SH110X_WHITE);
  display.drawFastHLine(lx - 5,  f.cy - 2, 5, SH110X_WHITE);
  display.drawFastHLine(rx + fw, f.cy - 2, 5, SH110X_WHITE);
}

static void acc_retro_round(const FaceFrame& f) {
  int size = f.w + 6, rad = size / 2;
  int top = f.cy - rad;
  int lx = f.leftEyeX - 3, rx = f.rightEyeX - 3;
  drawHollowRoundRect(lx - 1, top - 1, size + 2, size + 2, rad + 1, 2);
  drawHollowRoundRect(rx - 1, top - 1, size + 2, size + 2, rad + 1, 2);
  drawInnerPupils(f, 10);
  display.drawFastHLine(lx + size,     f.cy - 2, rx - (lx + size), SH110X_WHITE);
  display.drawFastHLine(lx + size,     f.cy - 1, rx - (lx + size), SH110X_WHITE);
  display.drawFastHLine(lx - 6,        f.cy - 2, 6, SH110X_WHITE);
  display.drawFastHLine(rx + size + 1, f.cy - 2, 6, SH110X_WHITE);
}

static void acc_studio_frames(const FaceFrame& f) {
  int fw = f.w + 8, fh = 28;
  int top = f.cy - 14;
  int lx = f.leftEyeX - 4, rx = f.rightEyeX - 4;
  drawHollowRoundRect(lx, top, fw, fh, 5, 3);
  drawHollowRoundRect(rx, top, fw, fh, 5, 3);
  drawInnerPupils(f, 10);
  display.fillRect(lx + fw - 1, top + 5, rx - (lx + fw) + 2, 4, SH110X_WHITE);
}

static void acc_browline_half_rims(const FaceFrame& f) {
  int fw = f.w + 8;
  int top = f.cy - 14;
  int lx = f.leftEyeX - 4, rx = f.rightEyeX - 4;
  for (int ox : {lx, rx}) {
    display.fillRoundRect(ox, top, fw, 5, 2, SH110X_WHITE);
    display.fillRect(ox,       top + 4, 2, 9, SH110X_WHITE);
    display.fillRect(ox + fw - 2, top + 4, 2, 9, SH110X_WHITE);
  }
  display.fillRect(lx + fw - 1, top + 2, rx - (lx + fw) + 2, 2, SH110X_WHITE);
  display.drawFastHLine(lx - 6,  top + 2, 6, SH110X_WHITE);
  display.drawFastHLine(rx + fw, top + 2, 6, SH110X_WHITE);
}

static void acc_wayfarer_shades(const FaceFrame& f) {
  int top = f.cy - 10;
  int lx = f.leftEyeX - 4, rx = f.rightEyeX - 4;
  int lw = f.w + 8;
  display.fillRoundRect(lx - 1, top - 1, lw + 2, 20, 4, SH110X_BLACK);
  display.fillRoundRect(rx - 1, top - 1, lw + 2, 20, 4, SH110X_BLACK);
  display.fillRoundRect(lx, top, lw, 18, 4, SH110X_WHITE);
  display.fillRoundRect(rx, top, lw, 18, 4, SH110X_WHITE);
  display.fillRect(lx + lw - 2, top + 2, rx - (lx + lw) + 4, 4, SH110X_WHITE);
  display.fillRect(lx + 2,      top + 2, 2, 2, SH110X_BLACK);
  display.fillRect(rx + lw - 4, top + 2, 2, 2, SH110X_BLACK);
  for (int ox : {lx, rx}) {
    display.drawLine(ox + 8,  top + 4,  ox + 14, top + 14, SH110X_BLACK);
    display.drawLine(ox + 10, top + 4,  ox + 16, top + 14, SH110X_BLACK);
  }
}

static void acc_cat_eye_frames(const FaceFrame& f) {
  int top = f.cy - 11;
  int lx = f.leftEyeX - 3, rx = f.rightEyeX - 1;
  int lw = f.w + 4;
  drawHollowRoundRect(lx, top, lw, 22, 6, 2);
  drawHollowRoundRect(rx, top, lw, 22, 6, 2);
  display.fillTriangle(lx - 6,       top - 5, lx + 4,     top,     lx,       top + 6, SH110X_WHITE);
  display.fillTriangle(rx + lw + 6,  top - 5, rx + lw - 4, top,     rx + lw,  top + 6, SH110X_WHITE);
  drawInnerPupils(f, 12);
  display.fillRect(lx + lw - 1, top + 5, rx - (lx + lw) + 2, 2, SH110X_WHITE);
}

static void acc_dapper_monocle(const FaceFrame& f) {
  int size = f.w + 6, rad = size / 2;
  int rx = f.rightEyeX - 3;
  int top = f.cy - rad;
  drawHollowRoundRect(rx - 1, top - 1, size + 2, size + 2, rad + 1, 2);
  int ew = f.w - 10; if (ew < 10) ew = 10;
  int eh = (int)((f.h - 8) * f.openness); if (eh < 3) eh = 3;
  display.fillRoundRect(f.rightEyeX + (f.w - ew) / 2, f.cy - eh / 2, ew, eh,
                        (eh / 2 > 5 ? 5 : eh / 2), SH110X_WHITE);
  int chainX = rx + size + 2; if (chainX > 125) chainX = 125;
  for (int y = f.cy + 2; y < 62; y += 3) {
    display.drawPixel(chainX + (y % 6 == 0 ? 1 : 0), y, SH110X_WHITE);
  }
}

static void acc_doll_lashes(const FaceFrame& f) {
  int ty = f.eyeY + 2, by = f.eyeY + f.h - 3;
  int lx = f.leftEyeX, rx = f.rightEyeX + f.w - 1;
  display.drawLine(lx,     ty,     lx - 8, ty - 5, SH110X_WHITE);
  display.drawLine(lx,     ty + 1, lx - 8, ty - 4, SH110X_WHITE);
  display.drawLine(lx + 4, ty - 1, lx - 4, ty - 8, SH110X_WHITE);
  display.drawLine(lx + 9, ty - 2, lx + 4, ty - 8, SH110X_WHITE);
  display.drawLine(lx + 1, by,     lx - 4, by + 3, SH110X_WHITE);
  display.drawLine(rx,     ty,     rx + 8, ty - 5, SH110X_WHITE);
  display.drawLine(rx,     ty + 1, rx + 8, ty - 4, SH110X_WHITE);
  display.drawLine(rx - 4, ty - 1, rx + 4, ty - 8, SH110X_WHITE);
  display.drawLine(rx - 9, ty - 2, rx - 4, ty - 8, SH110X_WHITE);
  display.drawLine(rx - 1, by,     rx + 4, by + 3, SH110X_WHITE);
}

static void acc_aviator_shades(const FaceFrame& f) {
  int top = f.cy - 10;
  int lx = f.leftEyeX - 4, rx = f.rightEyeX - 4;
  int lw = f.w + 8;
  for (int ox : {lx, rx}) {
    display.fillRoundRect(ox - 1, top - 1, lw + 2, 22, 7, SH110X_BLACK);
    display.fillRoundRect(ox,     top,     lw,     16, 4, SH110X_WHITE);
    display.fillRoundRect(ox + 3, top + 12, lw - 6, 8, 4, SH110X_WHITE);
    display.drawLine(ox + 6, top + 3, ox + 12, top + 13, SH110X_BLACK);
  }
  display.drawFastHLine(lx + lw - 1, top + 1, rx - (lx + lw) + 2, SH110X_WHITE);
  display.drawFastHLine(lx + lw - 1, top + 5, rx - (lx + lw) + 2, SH110X_WHITE);
}

static void acc_shutter_shades(const FaceFrame& f) {
  int top = f.cy - 10;
  int lx = f.leftEyeX - 4, rx = f.rightEyeX - 4;
  int lw = f.w + 8;
  for (int ox : {lx, rx}) {
    display.fillRoundRect(ox - 1, top - 1, lw + 2, 22, 4, SH110X_BLACK);
    display.fillRoundRect(ox,     top,     lw,     20, 4, SH110X_WHITE);
    for (int sy = top + 3; sy < top + 17; sy += 4) {
      display.fillRect(ox + 3, sy, lw - 6, 2, SH110X_BLACK);
    }
  }
  display.fillRect(lx + lw - 1, top + 4, rx - (lx + lw) + 2, 3, SH110X_WHITE);
}

static void acc_pirate_patch(const FaceFrame& f) {
  int lx = f.leftEyeX - 3;
  int top = f.cy - 11;
  int lw = f.w + 6;
  display.drawLine(4, 3, lx + 4, top + 4, SH110X_WHITE);
  display.drawLine(4, 4, lx + 4, top + 5, SH110X_WHITE);
  display.drawLine(lx + lw - 2, top + 3, 122, 2, SH110X_WHITE);
  display.drawLine(lx + lw - 2, top + 4, 122, 3, SH110X_WHITE);
  display.fillRoundRect(lx - 1, top - 1, lw + 2, 24, 9, SH110X_BLACK);
  display.fillRoundRect(lx,     top,     lw,     22, 8, SH110X_WHITE);
  display.fillRect(lx + 6, top + 5, 3, 2, SH110X_BLACK);
}

static void acc_cyber_visor(const FaceFrame& f) {
  int x0 = f.leftEyeX - 6;  if (x0 < 6)   x0 = 6;
  int x1 = f.rightEyeX + f.w + 6; if (x1 > 122) x1 = 122;
  int w = x1 - x0;
  int top = f.cy - 10;
  display.fillRoundRect(x0 - 1, top - 1, w + 2, 22, 6, SH110X_BLACK);
  display.fillRoundRect(x0,     top,     w,     20, 5, SH110X_WHITE);
  display.fillRoundRect(x0 + 3, top + 3, w - 6, 14, 3, SH110X_BLACK);
  int slitH = (int)(4 * f.openness); if (slitH < 2) slitH = 2;
  display.fillRoundRect(x0 + 7, f.cy - slitH / 2, w - 14, slitH, 1, SH110X_WHITE);
  display.fillTriangle(f.cx - 5, top + 20, f.cx + 5, top + 20, f.cx, top + 14, SH110X_BLACK);
}

static void acc_heart_shades(const FaceFrame& f) {
  int c0 = f.leftEyeX  + f.w / 2;
  int c1 = f.rightEyeX + f.w / 2;
  for (int hx : {c0, c1}) {
    display.fillRoundRect(hx - 17, f.cy - 14, 34, 29, 7, SH110X_BLACK);
    display.fillCircle(hx - 7, f.cy - 4, 8, SH110X_WHITE);
    display.fillCircle(hx + 7, f.cy - 4, 8, SH110X_WHITE);
    display.fillTriangle(hx - 15, f.cy - 2, hx + 15, f.cy - 2, hx, f.cy + 14, SH110X_WHITE);
    display.fillRect(hx - 9, f.cy - 7, 3, 3, SH110X_BLACK);
    display.fillRect(hx - 6, f.cy - 4, 2, 2, SH110X_BLACK);
  }
  display.fillRect(c0 + 13, f.cy - 4, c1 - c0 - 26, 3, SH110X_WHITE);
}

static void acc_power_scouter(const FaceFrame& f) {
  int rx = f.rightEyeX - 5;
  int top = f.cy - 12;
  int w = f.w + 10;
  drawHollowRoundRect(rx, top, w, 24, 3, 2);
  display.fillRect(rx + w, top + 4, 8, 12, SH110X_WHITE);
  int rcx = f.rightEyeX + f.w / 2;
  display.drawFastHLine(rcx - 6, f.cy, 4, SH110X_WHITE);
  display.drawFastHLine(rcx + 3, f.cy, 4, SH110X_WHITE);
  display.drawFastVLine(rcx, f.cy - 6, 4, SH110X_WHITE);
  display.drawFastVLine(rcx, f.cy + 3, 4, SH110X_WHITE);
  display.drawPixel(rx + 4, top + 4, SH110X_WHITE);
  display.drawPixel(rx + 6, top + 4, SH110X_WHITE);
}

static void acc_star_shades(const FaceFrame& f) {
  int c0 = f.leftEyeX  + f.w / 2;
  int c1 = f.rightEyeX + f.w / 2;
  for (int sx : {c0, c1}) {
    display.fillRoundRect(sx - 17, f.cy - 17, 34, 34, 8, SH110X_BLACK);
    display.fillTriangle(sx, f.cy - 15, sx - 14, f.cy + 9,  sx + 14, f.cy + 9,  SH110X_WHITE);
    display.fillTriangle(sx, f.cy + 15, sx - 14, f.cy - 9,  sx + 14, f.cy - 9,  SH110X_WHITE);
    int ph = (int)(7 * f.openness); if (ph < 2) ph = 2;
    display.fillRoundRect(sx - 5, f.cy - ph / 2, 10, ph, 2, SH110X_BLACK);
  }
  display.fillRect(c0 + 11, f.cy - 1, c1 - c0 - 22, 3, SH110X_WHITE);
}

static void acc_grandmaster_visor(const FaceFrame& f) {
  int x0 = f.leftEyeX - 8;  if (x0 < 4)   x0 = 4;
  int x1 = f.rightEyeX + f.w + 8; if (x1 > 124) x1 = 124;
  int w = x1 - x0;
  int top = f.cy - 12;
  display.fillRoundRect(x0 - 1, top - 1, w + 2, 26, 5, SH110X_BLACK);
  display.fillRoundRect(x0,     top,     w,     24, 4, SH110X_WHITE);
  display.fillRoundRect(x0 + 3, top + 3, w - 6, 18, 2, SH110X_BLACK);
  display.fillRect(x0 + 6,  f.cy - 4, w - 12, 3, SH110X_WHITE);
  display.fillRect(x0 + 10, f.cy + 2, w - 20, 2, SH110X_WHITE);
  display.fillTriangle(f.cx - 6, top + 24, f.cx + 6, top + 24, f.cx, top + 16, SH110X_BLACK);
}

// ============================================================
//  MOUTH ACCESSORIES (cosmeticId 2,3,5,6,9,10,13,14,16,18,21,23,26,28,31,33,34,36)
// ============================================================

static void acc_round_blush(const FaceFrame& f) {
  int by = f.cy + f.h / 2 + 2; if (by > 55) by = 55;
  int lx = f.leftEyeX - 4;
  int rx = f.rightEyeX + f.w - 6;
  for (int bx : {lx, rx}) {
    display.fillRoundRect(bx, by, 10, 5, 2, SH110X_WHITE);
    display.drawPixel(bx + 2, by + 2, SH110X_BLACK);
    display.drawPixel(bx + 7, by + 2, SH110X_BLACK);
  }
}

static void acc_pixel_mustache(const FaceFrame& f) {
  int mx = f.mouthX, my = f.mouthY - 6;
  display.fillRoundRect(mx - 12, my,     24, 5, 2, SH110X_BLACK);
  display.fillRoundRect(mx - 10, my + 1,  9, 3, 1, SH110X_WHITE);
  display.fillRoundRect(mx + 1,  my + 1,  9, 3, 1, SH110X_WHITE);
  display.fillRoundRect(mx - 13, my + 2,  4, 3, 1, SH110X_WHITE);
  display.fillRoundRect(mx + 9,  my + 2,  4, 3, 1, SH110X_WHITE);
  display.fillRoundRect(mx - 2,  my,      4, 3, 1, SH110X_WHITE);
}

static void acc_beauty_mark(const FaceFrame& f) {
  int mx = f.rightEyeX + f.w - 4; if (mx > 118) mx = 118;
  int my = f.mouthY - 3;          if (my > 57)  my = 57;
  display.fillRoundRect(mx - 1, my - 1, 5, 5, 2, SH110X_BLACK);
  display.fillRoundRect(mx,     my,     3, 3, 1, SH110X_WHITE);
}

static void acc_anime_blush(const FaceFrame& f) {
  int by = f.cy + f.h / 2 + 2; if (by > 54) by = 54;
  int lx = f.leftEyeX - 2;
  int rx = f.rightEyeX + f.w - 9;
  for (int bx : {lx, rx}) {
    for (int k = 0; k < 3; k++) {
      display.drawLine(bx + k * 4, by + 5, bx + k * 4 + 3, by, SH110X_WHITE);
    }
  }
}

static void acc_pixel_freckles(const FaceFrame& f) {
  int by = f.cy + f.h / 2 + 3; if (by > 52) by = 52;
  int lx = f.leftEyeX - 2;
  int rx = f.rightEyeX + f.w - 10;
  static const int8_t pts[][2] = {{0,1},{4,0},{8,2},{2,4},{6,4}};
  for (int bx : {lx, rx}) {
    for (auto& p : pts) {
      display.fillRect(bx + p[0], by + p[1], 2, 2, SH110X_WHITE);
    }
  }
}

static void acc_cat_whiskers(const FaceFrame& f) {
  int wy = f.mouthY - 2;
  display.drawLine(8,  wy - 4, 24, wy - 1, SH110X_WHITE);
  display.drawFastHLine(6,  wy + 1, 18, SH110X_WHITE);
  display.drawLine(8,  wy + 6, 24, wy + 3, SH110X_WHITE);
  display.drawLine(104, wy - 1, 120, wy - 4, SH110X_WHITE);
  display.drawFastHLine(104, wy + 1, 18, SH110X_WHITE);
  display.drawLine(104, wy + 3, 120, wy + 6, SH110X_WHITE);
}

static void acc_cheek_bandage(const FaceFrame& f) {
  int bx = f.rightEyeX + f.w - 8; if (bx > 106) bx = 106;
  int by = f.cy + f.h / 2 + 2;    if (by > 52)  by = 52;
  display.fillRoundRect(bx - 1, by - 1, 16, 8, 2, SH110X_BLACK);
  display.fillRoundRect(bx,     by,     14, 6, 2, SH110X_WHITE);
  display.fillRect(bx + 5, by + 1, 4, 4, SH110X_BLACK);
  display.drawPixel(bx + 2,  by + 2, SH110X_BLACK);
  display.drawPixel(bx + 2,  by + 4, SH110X_BLACK);
  display.drawPixel(bx + 11, by + 2, SH110X_BLACK);
  display.drawPixel(bx + 11, by + 4, SH110X_BLACK);
}

static void acc_handlebar_stache(const FaceFrame& f) {
  int mx = f.mouthX, my = f.mouthY - 6;
  display.fillRoundRect(mx - 18, my - 1, 36, 7, 2, SH110X_BLACK);
  display.fillRoundRect(mx - 13, my + 2, 12, 3, 1, SH110X_WHITE);
  display.fillRoundRect(mx + 1,  my + 2, 12, 3, 1, SH110X_WHITE);
  display.fillRoundRect(mx - 3,  my + 1,  6, 3, 1, SH110X_WHITE);
  display.drawLine(mx - 13, my + 3, mx - 17, my + 1, SH110X_WHITE);
  display.drawLine(mx - 17, my + 1, mx - 16, my - 2, SH110X_WHITE);
  display.drawLine(mx - 16, my - 2, mx - 14, my - 2, SH110X_WHITE);
  display.drawLine(mx + 13, my + 3, mx + 17, my + 1, SH110X_WHITE);
  display.drawLine(mx + 17, my + 1, mx + 16, my - 2, SH110X_WHITE);
  display.drawLine(mx + 16, my - 2, mx + 14, my - 2, SH110X_WHITE);
}

static void acc_pixel_fangs(const FaceFrame& f) {
  int mx = f.mouthX, my = f.mouthY + 1;
  display.fillTriangle(mx - 8, my, mx - 4, my, mx - 6, my + 6, SH110X_WHITE);
  display.fillTriangle(mx + 4, my, mx + 8, my, mx + 6, my + 6, SH110X_WHITE);
}

static void acc_heart_cheeks(const FaceFrame& f) {
  int by = f.cy + f.h / 2 + 3; if (by > 52) by = 52;
  int lx = f.leftEyeX + 1;
  int rx = f.rightEyeX + f.w - 4;
  for (int hx : {lx, rx}) {
    display.fillCircle(hx - 2, by, 2, SH110X_WHITE);
    display.fillCircle(hx + 2, by, 2, SH110X_WHITE);
    display.fillTriangle(hx - 4, by + 1, hx + 4, by + 1, hx, by + 5, SH110X_WHITE);
  }
}

static void acc_pixel_goatee(const FaceFrame& f) {
  int mx = f.mouthX, my = f.mouthY + 6;
  int tip = my + 7; if (tip > 63) tip = 63;
  display.fillTriangle(mx - 6, my, mx + 6, my, mx, tip, SH110X_WHITE);
  display.drawFastHLine(mx - 3, my + 1, 7, SH110X_BLACK);
}

static void acc_sparkle_cheeks(const FaceFrame& f) {
  int by = f.cy + f.h / 2 + 4; if (by > 53) by = 53;
  int lx = f.leftEyeX + 1;
  int rx = f.rightEyeX + f.w - 3;
  for (int sx : {lx, rx}) {
    display.drawFastHLine(sx - 4, by, 9, SH110X_WHITE);
    display.drawFastVLine(sx, by - 4, 9, SH110X_WHITE);
    display.fillRect(sx - 1, by - 1, 3, 3, SH110X_WHITE);
  }
}

static void acc_chevron_stache(const FaceFrame& f) {
  int mx = f.mouthX, my = f.mouthY - 7;
  display.fillRoundRect(mx - 15, my, 30, 7, 2, SH110X_BLACK);
  display.fillTriangle(mx - 14, my + 6, mx - 10, my + 1, mx, my + 4, SH110X_WHITE);
  display.fillTriangle(mx + 14, my + 6, mx + 10, my + 1, mx, my + 4, SH110X_WHITE);
  display.fillRoundRect(mx - 11, my + 1, 22, 4, 1, SH110X_WHITE);
}

static void acc_swirl_cheeks(const FaceFrame& f) {
  int by = f.cy + f.h / 2 + 3; if (by > 52) by = 52;
  int lx = f.leftEyeX;
  int rx = f.rightEyeX + f.w - 9;
  for (int ox : {lx, rx}) {
    display.drawFastHLine(ox,     by,     8, SH110X_WHITE);
    display.drawFastVLine(ox + 8, by,     7, SH110X_WHITE);
    display.drawFastHLine(ox + 1, by + 6, 7, SH110X_WHITE);
    display.drawFastVLine(ox + 1, by + 2, 4, SH110X_WHITE);
    display.drawFastHLine(ox + 2, by + 2, 4, SH110X_WHITE);
    display.drawFastVLine(ox + 5, by + 2, 3, SH110X_WHITE);
    display.drawPixel(ox + 4, by + 4, SH110X_WHITE);
  }
}

static void acc_goofy_grin_tooth(const FaceFrame& f) {
  int mx = f.mouthX, my = f.mouthY + 1;
  display.fillRoundRect(mx - 4, my, 8, 7, 1, SH110X_WHITE);
  display.drawFastVLine(mx, my + 1, 6, SH110X_BLACK);
}

static void acc_sage_beard(const FaceFrame& f) {
  int mx = f.mouthX, my = f.mouthY - 6;
  display.fillRoundRect(mx - 15, my,     30, 5, 2, SH110X_BLACK);
  display.fillRoundRect(mx - 14, my + 1, 13, 3, 1, SH110X_WHITE);
  display.fillRoundRect(mx + 1,  my + 1, 13, 3, 1, SH110X_WHITE);
  display.fillTriangle(mx - 12, my + 11, mx + 12, my + 11, mx, 63, SH110X_WHITE);
  display.drawFastVLine(mx - 4, my + 12, 4, SH110X_BLACK);
  display.drawFastVLine(mx + 4, my + 12, 4, SH110X_BLACK);
}

static void acc_grandmaster_stache(const FaceFrame& f) {
  int mx = f.mouthX, my = f.mouthY - 7;
  display.fillRoundRect(mx - 19, my - 1, 38, 12, 3, SH110X_BLACK);
  display.fillRoundRect(mx - 15, my + 1, 14,  5, 2, SH110X_WHITE);
  display.fillRoundRect(mx + 1,  my + 1, 14,  5, 2, SH110X_WHITE);
  display.fillTriangle(mx - 15, my + 1, mx - 20, my + 7, mx - 10, my + 6, SH110X_WHITE);
  display.fillTriangle(mx + 15, my + 1, mx + 20, my + 7, mx + 10, my + 6, SH110X_WHITE);
  display.fillRoundRect(mx - 4, my, 8, 4, 1, SH110X_WHITE);
  display.fillTriangle(mx - 3, my + 9, mx + 3, my + 9, mx, 63, SH110X_WHITE);
}

static void acc_diamond_cheeks(const FaceFrame& f) {
  int by = f.cy + f.h / 2 + 4; if (by > 53) by = 53;
  int lx = f.leftEyeX;
  int rx = f.rightEyeX + f.w - 3;
  for (int dx : {lx, rx}) {
    display.fillTriangle(dx, by - 5, dx - 5, by, dx + 5, by, SH110X_WHITE);
    display.fillTriangle(dx, by + 5, dx - 5, by, dx + 5, by, SH110X_WHITE);
    display.fillRect(dx - 1, by - 1, 3, 3, SH110X_BLACK);
    display.drawPixel(dx, by, SH110X_WHITE);
  }
}

// ============================================================
//  SLOT LOOKUP — which slot a cosmeticId belongs to
//  0 = eyes, 1 = mouth, -1 = none / invalid
// ============================================================
static inline int8_t accessorySlot(uint8_t id) {
  switch (id) {
    case  1: case  4: case  7: case  8: case 11: case 12: case 15: case 17: case 19:
    case 20: case 22: case 24: case 25: case 27: case 29: case 30: case 32: case 35:
      return 0;
    case  2: case  3: case  5: case  6: case  9: case 10: case 13: case 14: case 16:
    case 18: case 21: case 23: case 26: case 28: case 31: case 33: case 34: case 36:
      return 1;
    default:
      return -1;
  }
}

// ============================================================
//  DISPATCHER — call this from drawFrame()
// ============================================================
static void drawAccessory(uint8_t id, const FaceFrame& f) {
  switch (id) {
    // Eyes
    case  1: acc_pixel_shades(f);        break;
    case  4: acc_flutter_lashes(f);      break;
    case  7: acc_wire_specs(f);          break;
    case  8: acc_retro_round(f);         break;
    case 11: acc_studio_frames(f);       break;
    case 12: acc_browline_half_rims(f);  break;
    case 15: acc_wayfarer_shades(f);     break;
    case 17: acc_cat_eye_frames(f);      break;
    case 19: acc_dapper_monocle(f);      break;
    case 20: acc_doll_lashes(f);         break;
    case 22: acc_aviator_shades(f);      break;
    case 24: acc_shutter_shades(f);      break;
    case 25: acc_pirate_patch(f);        break;
    case 27: acc_cyber_visor(f);         break;
    case 29: acc_heart_shades(f);        break;
    case 30: acc_power_scouter(f);       break;
    case 32: acc_star_shades(f);         break;
    case 35: acc_grandmaster_visor(f);   break;
    // Mouth
    case  2: acc_round_blush(f);         break;
    case  3: acc_pixel_mustache(f);      break;
    case  5: acc_beauty_mark(f);         break;
    case  6: acc_anime_blush(f);         break;
    case  9: acc_pixel_freckles(f);      break;
    case 10: acc_cat_whiskers(f);        break;
    case 13: acc_cheek_bandage(f);       break;
    case 14: acc_handlebar_stache(f);    break;
    case 16: acc_pixel_fangs(f);         break;
    case 18: acc_heart_cheeks(f);        break;
    case 21: acc_pixel_goatee(f);        break;
    case 23: acc_sparkle_cheeks(f);      break;
    case 26: acc_chevron_stache(f);      break;
    case 28: acc_swirl_cheeks(f);        break;
    case 31: acc_goofy_grin_tooth(f);    break;
    case 33: acc_sage_beard(f);          break;
    case 34: acc_grandmaster_stache(f);  break;
    case 36: acc_diamond_cheeks(f);      break;
    default: break;
  }
}

#endif // ACCESSORIES_H