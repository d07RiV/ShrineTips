#pragma once

#include <windows.h>
#include <winhttp.h>
#include <memory>
#include <string>
#include "file.h"

class HttpRequest {
public:
  enum RequestType {GET, POST};

  HttpRequest(std::string const& url, RequestType type = GET);
  ~HttpRequest();

  void addHeader(std::string const& header);
  void addData(std::string const& key, std::string const& value);

  bool send();
  File response();

private:
  RequestType type_;
  HINTERNET session_;
  HINTERNET connect_;
  HINTERNET request_;
  std::wstring headers;
  std::string post;
};
