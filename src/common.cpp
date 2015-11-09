#include <stdarg.h>
#include <clocale>
#include <algorithm>
#include "common.h"

std::string fmtstring(char const* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::string res = varfmtstring(fmt, ap);
  va_end(ap);
  return res;
}
std::string varfmtstring(char const* fmt, va_list list) {
  uint32 len = _vscprintf(fmt, list);
  std::string dst;
  dst.resize(len + 1);
  vsprintf(&dst[0], fmt, list);
  dst.resize(len);
  return dst;
}

Exception::Exception(char const* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  uint32 len = _vscprintf(fmt, ap);
  std::string dst;
  dst.resize(len + 1);
  vsprintf(&dst[0], fmt, ap);
  dst.resize(len);

  buf_.str(dst);
}

uint32 RefCounted::addref() {
  return InterlockedIncrement(&ref_);
}
uint32 RefCounted::release() {
  if (!this) {
    return 0;
  }
  uint32 result = InterlockedDecrement(&ref_);
  if (!result) {
    delete this;
  }
  return result;
}

void _qmemset(uint32* mem, uint32 fill, uint32 count) {
  while (count--) {
    *mem++ = fill;
  }
}

std::string strlower(std::string const& str) {
  std::string dest(str.size(), ' ');
  std::transform(str.begin(), str.end(), dest.begin(), std::tolower);
  return dest;
}

std::vector<std::string> split(std::string const& str, char sep) {
  std::vector<std::string> res;
  std::string cur;
  for (char c : str) {
    if (c == sep) {
      res.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  res.push_back(cur);
  return res;
}
std::string join(std::vector<std::string> const& list, char sep) {
  std::string res;
  for (auto& str : list) {
    if (!res.empty()) res.push_back(' ');
    res.append(str);
  }
  return res;
}
std::string join(std::vector<std::string> const& list, std::string const& sep) {
  std::string res;
  for (auto& str : list) {
    if (!res.empty()) res.append(sep);
    res.append(str);
  }
  return res;
}

std::wstring utf8_to_utf16(std::string const& str) {
  std::wstring dst;
  for (size_t i = 0; i < str.size();) {
    uint32 cp = (unsigned char) str[i++];
    size_t next = 0;
    if (cp <= 0x7F) {
      // do nothing
    } else if (cp <= 0xBF) {
      throw Exception("not a valid utf-8 string");
    } else if (cp <= 0xDF) {
      cp &= 0x1F;
      next = 1;
    } else if (cp <= 0xEF) {
      cp &= 0x0F;
      next = 2;
    } else if (cp <= 0xF7) {
      cp &= 0x07;
      next = 3;
    } else {
      throw Exception("not a valid utf-8 string");
    }
    while (next--) {
      if (i >= str.size() || str[i] < 0x80 || str[i] > 0xBF) {
        throw Exception("not a valid utf-8 string");
      }
      cp = (cp << 6) | (str[i++] & 0x3F);
    }
    if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
      throw Exception("not a valid utf-8 string");
    }

    if (cp <= 0xFFFF) {
      dst.push_back(cp);
    } else {
      cp -= 0x10000;
      dst.push_back((cp >> 10) + 0xD800);
      dst.push_back((cp & 0x3FF) + 0xDC00);
    }
  }
  return dst;
}

std::string utf16_to_utf8(std::wstring const& str) {
  std::string dst;
  for (size_t i = 0; i < str.size();) {
    uint32 cp = str[i++];
    if (cp >= 0xD800 && cp <= 0xDFFF) {
      if (cp >= 0xDC00) throw Exception("not a valid utf-16 string");
      if (i >= str.size() || str[i] < 0xDC00 || str[i] > 0xDFFF) throw Exception("not a valid utf-16 string");
      cp = 0x10000 + ((cp - 0xD800) << 10) + (str[i++] - 0xDC00);
    }
    if (cp >= 0x10FFFF) throw Exception("not a valid utf-16 string");
    if (cp <= 0x7F) {
      dst.push_back(cp);
    } else if (cp <= 0x7FF) {
      dst.push_back((cp >> 6) | 0xC0);
      dst.push_back((cp & 0x3F) | 0x80);
    } else if (cp <= 0xFFFF) {
      dst.push_back((cp >> 12) | 0xE0);
      dst.push_back(((cp >> 6) & 0x3F) | 0x80);
      dst.push_back((cp & 0x3F) | 0x80);
    } else {
      dst.push_back((cp >> 18) | 0xF0);
      dst.push_back(((cp >> 12) & 0x3F) | 0x80);
      dst.push_back(((cp >> 6) & 0x3F) | 0x80);
      dst.push_back((cp & 0x3F) | 0x80);
    }
  }
  return dst;
}

std::string trim(std::string const& str) {
  size_t left = 0, right = str.size();
  while (left < str.length() && isspace((unsigned char)str[left])) ++left;
  while (right > left && isspace((unsigned char)str[right - 1])) --right;
  return str.substr(left, right - left);
}
