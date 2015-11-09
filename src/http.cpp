#include "http.h"
#include "common.h"
#pragma comment(lib, "winhttp.lib")

HttpRequest::HttpRequest(std::string const& url, RequestType type)
  : type_(type)
  , session_(nullptr)
  , connect_(nullptr)
  , request_(nullptr)
{
  std::wstring url16 = utf8_to_utf16(url);
  URL_COMPONENTS urlComp;
  memset(&urlComp, 0, sizeof urlComp);
  urlComp.dwStructSize = sizeof urlComp;
  urlComp.dwSchemeLength = -1;
  urlComp.dwHostNameLength = -1;
  urlComp.dwUrlPathLength = -1;
  urlComp.dwExtraInfoLength = -1;

  if (!WinHttpCrackUrl(url16.c_str(), url.size(), 0, &urlComp)) {
    return;
  }

  std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
  std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

  session_ = WinHttpOpen(L"ShrineTips", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
  if (!session_) return;
  connect_ = WinHttpConnect(session_, host.c_str(), urlComp.nPort, 0);
  if (!connect_) return;
  request_ = WinHttpOpenRequest(
    connect_,
    type == GET ? L"GET" : L"POST",
    path.c_str(),
    L"HTTP/1.1",
    WINHTTP_NO_REFERER,
    WINHTTP_DEFAULT_ACCEPT_TYPES,
    urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
}
HttpRequest::~HttpRequest() {
  if (request_) WinHttpCloseHandle(request_);
  if (connect_) WinHttpCloseHandle(connect_);
  if (session_) WinHttpCloseHandle(session_);
}

void HttpRequest::addHeader(std::string const& header) {
  headers.append(utf8_to_utf16(header));
  headers.append(L"\r\n");
}

static std::string urlencode(std::string const& str) {
  std::string dst;
  for (unsigned char c : str) {
    if (isalnum(c)) {
      dst.push_back(c);
    } else if (c == ' ') {
      dst.push_back('+');
    } else {
      char hex[4];
      sprintf_s(hex, sizeof hex, "%%%02X", c);
      dst.append(hex);
    }
  }
  return dst;
}

void HttpRequest::addData(std::string const& key, std::string const& value) {
  if (!post.empty()) post.push_back('&');
  post.append(urlencode(key));
  post.push_back('=');
  post.append(urlencode(value));
}

bool HttpRequest::send() {
  if (!request_) return false;
  if (!WinHttpSendRequest(request_,
    headers.empty() ? nullptr : headers.c_str(), headers.size(),
    post.empty() ? nullptr : &post[0], post.size(), post.size(), 0)) {
    return false;
  }
  return WinHttpReceiveResponse(request_, NULL);
}

File HttpRequest::response() {
  if (!request_) return File();

  DWORD size = 0, read;
  std::string buffer;
  do {
    if (!WinHttpQueryDataAvailable(request_, &size)) return File();
    if (!size) break;
    size_t pos = buffer.size();
    buffer.resize(pos + size);
    if (!WinHttpReadData(request_, &buffer[pos], size, &read)) return File();
  } while (size);
  return File::memfile(&buffer[0], buffer.size(), true);
}
