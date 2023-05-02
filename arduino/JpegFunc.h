/*******************************************************************************
 * JPEGDEC related function
 *
 * Dependent libraries:
 * JPEGDEC: https://github.com/bitbank2/JPEGDEC.git
 ******************************************************************************/
#ifndef _JPEGFUNC_H_
#define _JPEGFUNC_H_

#include <JPEGDEC.h>

static JPEGDEC _jpeg;
static int _x, _y, _x_bound, _y_bound;

static File _f;

static void jpegCloseHttpStream(void *pHandle) {
}

static int32_t readStream(WiFiClient *http_stream, uint8_t *pBuf, int32_t iLen) {
  uint8_t wait = 0;
  size_t a = http_stream->available();
  size_t r = 0;
  while ((r < iLen) && (wait < 10)) {
    if (a) {
      wait = 0; // reset wait count once available
      r += http_stream->readBytes(pBuf + r, iLen - r);
    }
    else {
      delay(100);
      wait++;
    }
    a = http_stream->available();
  }
  return r;
}

static int32_t jpegReadHttpStream(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  WiFiClient *http_stream = (WiFiClient *)pFile->fHandle;
  return readStream(http_stream, pBuf, iLen);
}

static int32_t jpegSeekHttpStream(JPEGFILE *pFile, int32_t iPosition) {
  WiFiClient *http_stream = (WiFiClient *)pFile->fHandle;
  http_stream->readBytes((uint8_t *)nullptr, iPosition - pFile->iPos);
  return iPosition;
}

static int jpegOpenHttpStreamWithBuffer(WiFiClient *http_stream, uint8_t *buf, int32_t dataSize, 
  JPEG_DRAW_CALLBACK *jpegDrawCallback)
{
  int32_t r = readStream(http_stream, buf, dataSize);
  if (r != dataSize) {
    return 0;
  }
  return _jpeg.openRAM(buf, dataSize, jpegDrawCallback);
}

static int jpegOpenHttpStream(WiFiClient *http_stream, int32_t dataSize, 
  JPEG_DRAW_CALLBACK *jpegDrawCallback)
{
  return _jpeg.open(http_stream, dataSize, jpegCloseHttpStream, jpegReadHttpStream, jpegSeekHttpStream, jpegDrawCallback);
}

static void *jpegOpenFile(const char *szFilename, int32_t *pFileSize) {
  _f = SD.open(szFilename, "r");
  *pFileSize = _f.size();
  return &_f;
}

static void jpegCloseFile(void *pHandle) {
  File *f = static_cast<File *>(pHandle);
  f->close();
}

static int32_t jpegReadFile(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    File *f = static_cast<File *>(pFile->fHandle);
    size_t r = f->read(pBuf, iLen);
    return r;
}

static int32_t jpegSeekFile(JPEGFILE *pFile, int32_t iPosition) {
    File *f = static_cast<File *>(pFile->fHandle);
    f->seek(iPosition);
    return iPosition;
}

static int jpegDraw(
    const char *filename, JPEG_DRAW_CALLBACK *jpegDrawCallbackSD, bool useBigEndian,
    int x, int y, int widthLimit, int heightLimit, bool drawMode) 
{
  _x = x;
  _y = y;
  _x_bound = _x + widthLimit - 1;
  _y_bound = _y + heightLimit - 1;

  if(!drawMode) {
    _jpeg.open(filename, jpegOpenFile, jpegCloseFile, jpegReadFile, jpegSeekFile, jpegDrawCallbackSD);
  }

  // scale to fit height
  int _scale;
  int iMaxMCUs;
  float ratio = (float)_jpeg.getHeight() / heightLimit;
  if (ratio <= 1) {
    _scale = 0;
    iMaxMCUs = widthLimit / 16;
  }
  else if (ratio <= 2) {
    _scale = JPEG_SCALE_HALF;
    iMaxMCUs = widthLimit / 8;
  }
  else if (ratio <= 4) {
    _scale = JPEG_SCALE_QUARTER;
    iMaxMCUs = widthLimit / 4;
  }
  else {
    _scale = JPEG_SCALE_EIGHTH;
    iMaxMCUs = widthLimit / 2;
  }
  _jpeg.setMaxOutputSize(iMaxMCUs);
  if (useBigEndian) {
    _jpeg.setPixelType(RGB565_BIG_ENDIAN);
  }
  int decode_result;
  if(drawMode) {
    decode_result = _jpeg.decode(x, y, _scale);
  }
  else {
    _jpeg.decode(x, y, _scale);
  }
  _jpeg.close();
  if (drawMode) {
    return decode_result;
  }
  else {
    return 1;
  }
}

#endif // _JPEGFUNC_H_
