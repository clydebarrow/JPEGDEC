// Checks that decoding in steps with decodeBegin()/decodeRows() gives exactly the same draw callbacks and
// pixels as a single decode(), for every JPEG file named on the command line.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/JPEGDEC.h"
#include "../../src/JPEGDEC.cpp"

struct Record {
  std::vector<int> geometry;
  std::vector<uint8_t> pixels;
  int abort_after{-1};  // draw callback returns 0 on this call
  int calls{0};
  const uint8_t *pixel_base{nullptr};
};

static int draw_cb(JPEGDRAW *d) {
  auto *r = static_cast<Record *>(d->pUser);
  r->geometry.insert(r->geometry.end(),
                     {d->x, d->y, d->iWidth, d->iWidthUsed, d->iHeight, d->iBpp,
                      static_cast<int>(reinterpret_cast<const uint8_t *>(d->pPixels) - r->pixel_base)});
  const size_t bytes = static_cast<size_t>(d->iWidth) * d->iHeight * d->iBpp / 8;
  const auto *p = reinterpret_cast<const uint8_t *>(d->pPixels);
  r->pixels.insert(r->pixels.end(), p, p + bytes);
  return ++r->calls == r->abort_after ? 0 : 1;
}

struct MemFile {
  const std::vector<uint8_t> *data;
};

static int32_t read_cb(JPEGFILE *f, uint8_t *buf, int32_t len) {
  auto *m = static_cast<MemFile *>(f->fHandle);
  const int32_t n = std::min<int32_t>(len, static_cast<int32_t>(m->data->size()) - f->iPos);
  memcpy(buf, m->data->data() + f->iPos, n);
  f->iPos += n;
  return n;
}

static int32_t seek_cb(JPEGFILE *f, int32_t pos) {
  f->iPos = pos;
  return pos;
}

static void close_cb(void *) {}

struct Config {
  const char *name;
  int pixel_type;
  int options;
  bool crop;
  bool framebuffer;
  int max_mcus;  // 0 leaves the default
  int abort_after;
  bool skips_blocks;  // JPEGDEC 1.8.4 already crashes on progressive images when it skips blocks
};

struct Result {
  int status{0};
  int more_count{0};
  int error{0};
  Record record;
  std::vector<uint8_t> framebuffer;
};

// step 0 means a single decode()
static Result run(const std::vector<uint8_t> &file, const Config &c, int step, bool stream) {
  static JPEGDEC jpeg;  // large; keep it off the stack
  Result res;
  MemFile mem{&file};
  int ok = stream ? jpeg.open(&mem, static_cast<int>(file.size()), close_cb, read_cb, seek_cb, draw_cb)
                  : jpeg.openRAM(const_cast<uint8_t *>(file.data()), static_cast<int>(file.size()), draw_cb);
  if (!ok) {
    res.error = jpeg.getLastError();
    return res;
  }
  res.record.abort_after = c.abort_after;
  jpeg.setUserPointer(&res.record);
  jpeg.setPixelType(c.pixel_type);
  if (c.max_mcus)
    jpeg.setMaxOutputSize(c.max_mcus);
  if (c.crop)
    jpeg.setCropArea(16, 8, jpeg.getWidth() / 2, jpeg.getHeight() / 2);
  if (c.framebuffer) {
    const int bpp = c.pixel_type == RGB8888 ? 4 : (c.pixel_type == EIGHT_BIT_GRAYSCALE ? 1 : 2);
    res.framebuffer.assign(static_cast<size_t>(jpeg.getWidth() + 16) * (jpeg.getHeight() + 16) * bpp, 0xA5);
    jpeg.setFramebuffer(res.framebuffer.data());
  }
  // Every run uses the same decoder object, so pixel pointers relative to it also compare the DMA ping-pong offset
  res.record.pixel_base = reinterpret_cast<const uint8_t *>(&jpeg);
  if (step == 0) {
    res.status = jpeg.decode(0, 0, c.options);
  } else if (!jpeg.decodeBegin(0, 0, c.options)) {
    res.status = 0;
  } else {
    while ((res.status = jpeg.decodeRows(step)) == JPEG_DECODE_MORE)
      res.more_count++;
  }
  res.error = jpeg.getLastError();
  jpeg.close();
  return res;
}

int main(int argc, char **argv) {
  static const Config configs[] = {
      {"rgb565", RGB565_LITTLE_ENDIAN, 0, false, false, 0, -1, false},
      {"rgb565be", RGB565_BIG_ENDIAN, 0, false, false, 0, -1, false},
      {"rgb8888", RGB8888, 0, false, false, 0, -1, false},
      {"gray", EIGHT_BIT_GRAYSCALE, 0, false, false, 0, -1, true},
      {"luma", RGB565_LITTLE_ENDIAN, JPEG_LUMA_ONLY, false, false, 0, -1, true},
      {"half", RGB565_LITTLE_ENDIAN, JPEG_SCALE_HALF, false, false, 0, -1, false},
      {"quarter", RGB565_LITTLE_ENDIAN, JPEG_SCALE_QUARTER, false, false, 0, -1, false},
      {"eighth", RGB8888, JPEG_SCALE_EIGHTH, false, false, 0, -1, false},
      {"crop", RGB565_LITTLE_ENDIAN, 0, true, false, 0, -1, true},
      {"dma", RGB565_LITTLE_ENDIAN, JPEG_USES_DMA, false, false, 0, -1, false},
      {"max1", RGB565_LITTLE_ENDIAN, 0, false, false, 1, -1, false},
      {"framebuffer", RGB565_LITTLE_ENDIAN, 0, false, true, 0, -1, false},
      {"framebuffer8888", RGB8888, 0, false, true, 0, -1, false},
      {"abort", RGB565_LITTLE_ENDIAN, 0, false, false, 0, 3, false},
  };
  static const int steps[] = {1, 2, 3, 1000};
  int failures = 0, checks = 0, skipped = 0;
  for (int a = 1; a < argc; a++) {
    FILE *f = fopen(argv[a], "rb");
    if (f == nullptr) {
      printf("cannot open %s\n", argv[a]);
      return 2;
    }
    std::vector<uint8_t> file;
    int ch;
    while ((ch = fgetc(f)) != EOF)
      file.push_back(static_cast<uint8_t>(ch));
    fclose(f);
    static JPEGDEC probe;
    probe.openRAM(file.data(), static_cast<int>(file.size()), draw_cb);
    const bool progressive = probe.getJPEGType() == JPEG_MODE_PROGRESSIVE;
    probe.close();
    for (const Config &c : configs) {
      if (progressive && c.skips_blocks) {
        skipped++;
        continue;
      }
      const Result ref = run(file, c, 0, false);
      if (ref.status != 1) {
        printf("FAIL %s %s: reference decode status %d error %d calls %d\n", argv[a], c.name, ref.status,
               ref.error, ref.record.calls);
        failures++;
        continue;
      }
      for (bool stream : {false, true}) {
        for (int step : steps) {
          checks++;
          const Result got = run(file, c, step, stream);
          const char *why = nullptr;
          if (got.status != ref.status)
            why = "status";
          else if (got.record.geometry != ref.record.geometry)
            why = "callback geometry";
          else if (got.record.pixels != ref.record.pixels)
            why = "pixels";
          else if (got.framebuffer != ref.framebuffer)
            why = "framebuffer";
          if (why != nullptr) {
            printf("FAIL %s %s step %d%s: %s differs (status %d/%d, calls %d/%d)\n", argv[a], c.name, step,
                   stream ? " stream" : "", why, got.status, ref.status, got.record.calls, ref.record.calls);
            failures++;
          }
        }
      }
    }
  }
  // decodeRows() without decodeBegin() is refused
  {
    static JPEGDEC jpeg;
    checks++;
    if (jpeg.decodeRows(1) != 0 || jpeg.getLastError() != JPEG_INVALID_PARAMETER) {
      printf("FAIL decodeRows without decodeBegin was not refused\n");
      failures++;
    }
  }
  printf("%d checks, %d failures, %d progressive combinations skipped\n", checks, failures, skipped);
  return failures != 0;
}
