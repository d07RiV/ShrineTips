#define NOMINMAX
#include <windows.h>
#include "http.h"
#include "json.h"
#include "regexp.h"
#include "resource.h"
#include <list>
#include <algorithm>

struct KeyValue {
  std::string key;
  std::string value;
  KeyValue() {}
  KeyValue(std::string const& k, std::string const& v)
    : key(k)
    , value(v)
  {}
};

struct ItemTip {
  std::string rarity;
  std::string name;
  std::string base;

  std::vector<KeyValue> baseStats;
  std::vector<KeyValue> requirements;
  std::string sockets;
  int ilvl;
  std::vector<std::vector<std::string>> sections;

  ItemTip() {}
  bool parse(std::string const& data);
};

bool ItemTip::parse(std::string const& data) {
  static re::Prog reRarity(R"(Rarity: (\w+))");
  static re::Prog reJunk(R"(<<set:\w+>>)");
  static re::Prog reKeyValue(R"(([^:]+): (.+))");
  static re::Prog reSockets(R"(Sockets: ([RGB \-]+))");
  static re::Prog reLevel(R"((Itemlevel|Item Level): (\d+))");

  std::vector<std::string> lines = split(data, '\n');
  std::vector<std::string> sub;
  int section = 0, line = 0, baseSection = -1;
  for (auto& str : lines) {
    str = trim(str);
    if (str.empty()) continue;
    if (str == "--------") {
      ++section;
      line = 0;
    } else {
      switch (section) {
      case 0:
        switch (line) {
        case 0:
          if (!reRarity.match(str, &sub)) return false;
          rarity = strlower(sub[1]);
          break;
        case 1:
          name = reJunk.replace(str, "");
          break;
        case 2:
          base = str;
          break;
        }
        break;
      case 1:
        if (reKeyValue.match(str, &sub)) {
          baseStats.emplace_back(sub[1], sub[2]);
        } else {
          baseStats.emplace_back(str, "");
        }
        break;
      case 2:
        if (str == "Requirements:" || line > 0) {
          if (line > 0) {
            if (!reKeyValue.match(str, &sub)) return false;
            requirements.emplace_back(sub[1], sub[2]);
          } else if (str != "Requirements:") {
            return false;
          }
          break;
        } else {
          ++section;
          // fall through
        }
      case 3:
        if (reSockets.match(str, &sub)) {
          sockets = sub[1];
          break;
        } else {
          ++section;
          // fall through
        }
      case 4:
        if (reLevel.match(str, &sub)) {
          ilvl = atoi(sub[1].c_str());
          break;
        } else {
          ++section;
          // fall through
        }
      default:
        if (baseSection < 0) baseSection = section;
        if (section - baseSection >= sections.size()) sections.resize(section - baseSection + 1);
        sections[section - baseSection].push_back(str);
      }
      ++line;
    }
  }
  return !(rarity.empty() || name.empty());
}

typedef std::vector<std::vector<std::string>> MatchData;

class ShrineData {
public:
  ShrineData() {
    update();
  }

  MatchData match(ItemTip const& tip);

  int version() {
    return effects[0].getInteger();
  }

  bool update() {
    matchers.clear();
    HttpRequest request("http://poe.rivsoft.net/shrines/shrines.js");
    if (!request.send()) return false;
    File data = request.response();
    if (!data) return false;
    if (!json::parse(data, effects)) return false;

    for (size_t i = 0; i < effects.length(); ++i) {
      if (effects[i].type() != json::Value::tArray) continue;
      for (size_t j = 2; j < effects[i].length(); ++j) {
        auto& reg = effects[i][j];
        if (reg.type() == json::Value::tArray) {
          matchers.emplace_back(makeRe(reg[0].getString()), i, reg[1].getString());
        } else {
          matchers.emplace_back(makeRe(reg.getString()), i, "");
        }
      }
    }
    return true;
  }
private:
  json::Value effects;
  struct Matcher {
    re::Prog prog;
    int index;
    std::string req;
    Matcher(std::string const& regex, int i, std::string const& r)
      : prog(regex, -1, re::Prog::CaseInsensitive)
      , index(i)
      , req(r)
    {}
  };
  std::list<Matcher> matchers;
  std::string makeRe(std::string const& src) {
    std::string dst;
    for (char c : src) {
      if (c == '+') dst.append("\\+");
      else if (c == '#') dst.append("[0-9.]+");
      else dst.push_back(c);
    }
    return dst;
  }
  bool checkReq(std::string const& req, std::string const& type) {
    if (req.empty()) return true;
    if (type.empty()) return false;
    std::vector<std::string> parts;
    bool def;
    if (req.substr(0, 5) == "type+") {
      def = true;
      parts = split(req, '+');
    } else if (req.substr(0, 5) == "type-") {
      def = false;
      parts = split(req, '-');
    } else {
      return true;
    }
    std::string tlow = strlower(type);
    for (size_t i = 1; i < parts.size(); ++i) {
      if (tlow.find(parts[i]) != std::string::npos) return def;
    }
    return !def;
  }
};

MatchData ShrineData::match(ItemTip const& tip) {
  std::map<int, std::vector<std::string>> matched;
  std::vector<std::string> unknown;
  size_t hasImplicit = 0;
  for (size_t i = 0; i < tip.sections.size(); ++i) {
    if (tip.sections[i].size() == 1 && i == 0 && tip.sections.size() > 1) {
      hasImplicit = 1;
      continue;
    }
    for (auto& str : tip.sections[i]) {
      bool found = false;
      for (auto& m : matchers) {
        if (m.prog.match(str) && checkReq(m.req, tip.base)) {
          matched[m.index].push_back(str);
          found = true;
        }
      }
      if (!found && i == hasImplicit) unknown.push_back(str);
    }
  }
  MatchData res;
  for (auto& kv : matched) {
    res.emplace_back();
    auto& dst = res.back();
    dst.push_back(effects[kv.first][0].getString());
    dst.push_back(effects[kv.first][1].getString());
    dst.insert(dst.end(), kv.second.begin(), kv.second.end());
  }
  if (!unknown.empty()) {
    res.emplace_back();
    auto& dst = res.back();
    dst.push_back("Unknown");
    dst.insert(dst.end(), unknown.begin(), unknown.end());
  }
  return res;
}

class TooltipWindow {
public:
  TooltipWindow(HINSTANCE hInstance);
  ~TooltipWindow();

  bool getClipboard(std::wstring& text);
  void clearClipboard();

private:
  static HRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
  int render(HDC hDC);
  HWND hWnd_;
  MatchData data_;
  int attempts_;
  ShrineData shrines_;
  POINT cursor_;
  HMENU tray_;
  bool hooked_;
  int version_;
  void checkVersion();
};

enum {MenuRefresh = 100, MenuExit = 101, WM_TRAYNOTIFY = WM_USER + 104};

TooltipWindow::TooltipWindow(HINSTANCE hInstance) {
  attempts_ = 0;
  hooked_ = false;
  version_ = 102;
  WNDCLASSEX wcx;
  memset(&wcx, 0, sizeof wcx);
  wcx.cbSize = sizeof wcx;
  wcx.lpfnWndProc = WndProc;
  wcx.hInstance = hInstance;
  wcx.lpszClassName = L"Shrine Tooltip";
  wcx.hbrBackground = CreateSolidBrush(0x121212);
  RegisterClassEx(&wcx);
  hWnd_ = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, L"Shrine Tooltip",
    NULL, WS_POPUP, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL,
    hInstance, this);

  tray_ = CreatePopupMenu();

  MENUITEMINFO mii;
  memset(&mii, 0, sizeof mii);
  mii.cbSize = sizeof mii;
  mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_STATE | MIIM_ID;
  mii.fType = MFT_STRING;
  mii.fState = MFS_DEFAULT;
  mii.dwTypeData = L"Reload data";
  mii.cch = wcslen(mii.dwTypeData);
  mii.wID = MenuRefresh;
  InsertMenuItem(tray_, 0, TRUE, &mii);

  mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID;
  mii.fType = MFT_STRING;
  mii.dwTypeData = L"Exit";
  mii.cch = wcslen(mii.dwTypeData);
  mii.wID = MenuExit;
  InsertMenuItem(tray_, 1, TRUE, &mii);

  NOTIFYICONDATA nid;
  memset(&nid, 0, sizeof nid);
  nid.cbSize = NOTIFYICONDATA_V3_SIZE;
  nid.hWnd = hWnd_;
  nid.uID = 123;
  nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
  nid.uCallbackMessage = WM_TRAYNOTIFY;
  nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
  wcscpy(nid.szTip, L"Shrine Effect Tooltip");
  Shell_NotifyIcon(NIM_ADD, &nid);
}
TooltipWindow::~TooltipWindow() {
  DestroyMenu(tray_);
}
void TooltipWindow::checkVersion() {
  int ver = shrines_.version();
  if (ver > version_) {
    if (MessageBox(hWnd_, L"A new version has been released. Would you like to open navigate to the downloads page?", L"Update",
        MB_YESNO) == IDYES) {
      ShellExecute(NULL, L"open", L"https://github.com/d07RiV/ShrineTips/releases", NULL, NULL, SW_SHOWNORMAL);
    }
    version_ = ver;
  }
}

struct LineDrawer {
  LineDrawer(HWND hwnd, HDC hdc)
    : hWnd(hwnd)
    , hDC(hdc)
  {
    HPEN hPen = CreatePen(PS_SOLID, 1, 0x666666);
    SelectObject(hDC, hPen);
    LOGFONT lf;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -11;
    lf.lfWeight = FW_NORMAL;
    wcscpy(lf.lfFaceName, L"Verdana");
    lf.lfCharSet = DEFAULT_CHARSET;
    hFontSmall = CreateFontIndirect(&lf);
    lf.lfHeight = -16;
    hFontLarge = CreateFontIndirect(&lf);
    SetBkColor(hDC, 0x121212);

    GetClientRect(hWnd, &rc);
    wrc = rc;
    rc.top = rc.bottom = 5;
    rc.left += 5;
    rc.right -= 5;
  }
  ~LineDrawer() {
    DeleteObject(hPen);
    DeleteObject(hFontSmall);
    DeleteObject(hFontLarge);
  }

  void line() {
    MoveToEx(hDC, 0, rc.bottom + 5, NULL);
    LineTo(hDC, wrc.right, rc.bottom + 5);
    rc.top = (rc.bottom += 10);
  }
  void fill(int delta) {
    wrc.top = rc.top + delta;
    ExtTextOut(hDC, 0, 0, ETO_OPAQUE, &wrc, NULL, 0, NULL);
  }
  void text(std::string const& str, uint32 color = 0x000000, bool large = false) {
    SetTextColor(hDC, color);
    if (large) SelectObject(hDC, hFontLarge);
    else SelectObject(hDC, hFontSmall);
    SIZE sz;
    std::wstring wstr = utf8_to_utf16(str);
    rc.bottom = rc.top + 256;
    rc.bottom = (rc.top += DrawText(hDC, wstr.c_str(), wstr.size(), &rc, DT_LEFT | DT_TOP | DT_WORDBREAK));
  }

  HWND hWnd;
  HDC hDC;
  HPEN hPen;
  HFONT hFontLarge;
  HFONT hFontSmall;
  RECT rc, wrc;
};

static uint32 Qualities[] = {
  0x000000,
  0x12129A, // 1
  0x121278, // 2
  0x121256, // 3
  0x121234, // 4
  0x121212, // 5
  0x123412, // 6
  0x125612, // 7
  0x127812, // 8
  0x129A12, // 9
};

int TooltipWindow::render(HDC hDC) {
  LineDrawer painter(hWnd_, hDC);

  for (auto& group : data_) {
    if (!group.size()) continue;
    if (&group != &data_[0]) {
      painter.line();
    }
    size_t index = 0;
    if (group[0] == "Unknown") {
      painter.text(group[0], 0x0000FF, true);
      index = 1;
    } else if (group.size() > 1) {
      std::string effect = group[1];
      if (effect.size() >= 2 && effect[0] == '$') {
        SetBkColor(hDC, Qualities[effect[1] - '0']);
        painter.fill(-4);
        effect = effect.substr(2);
      }
      painter.text(group[0], 0xFFFFFF, true);
      painter.text(effect, 0xFFFFFF, false);
      index = 2;
    }
    while (index < group.size()) {
      painter.text(group[index++], 0x999999, false);
    }
    SetBkColor(hDC, 0x121212);
    painter.fill(5);
  }

  return painter.rc.bottom;
}

enum { TimerUpdate = 102, TimerClipboard = 100, TimerCursor = 101, TimerForeground = 103, HotkeyId = 108 };

HRESULT CALLBACK TooltipWindow::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  TooltipWindow* wnd = nullptr;
  if (uMsg == WM_CREATE) {
    CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
    wnd = reinterpret_cast<TooltipWindow*>(cs->lpCreateParams);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(wnd));
  } else {
    wnd = reinterpret_cast<TooltipWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  }
  if (!wnd) return DefWindowProc(hWnd, uMsg, wParam, lParam);
  switch (uMsg) {
  case WM_CREATE:
    SetTimer(hWnd, TimerUpdate, 1000 * 1800, NULL);
    SetTimer(hWnd, TimerForeground, 1000, NULL);
    wnd->checkVersion();
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    wnd->render(BeginPaint(hWnd, &ps));
    EndPaint(hWnd, &ps);
    return 0;
  }
  case WM_HOTKEY:
    if (wParam == HotkeyId) {
      wnd->clearClipboard();
      wnd->attempts_ = 0;

      INPUT inp;
      memset(&inp, 0, sizeof inp);
      inp.type = INPUT_KEYBOARD;
      inp.ki.wVk = VK_CONTROL;
      SendInput(1, &inp, sizeof inp);
      inp.ki.wVk = 'C';
      SendInput(1, &inp, sizeof inp);
      inp.ki.dwFlags = KEYEVENTF_KEYUP;
      SendInput(1, &inp, sizeof inp);
      inp.ki.wVk = VK_CONTROL;
      SendInput(1, &inp, sizeof inp);

      SetTimer(hWnd, TimerClipboard, 100, NULL);
    }
    return 0;
  case WM_TIMER:
    if (wParam == TimerClipboard) {
      std::wstring clip;
      ItemTip item;
      if (!wnd->getClipboard(clip) ||
          !item.parse(utf16_to_utf8(clip)) ||
          (item.rarity != "rare" && item.rarity != "magic")) {
        if (++wnd->attempts_ > 10) {
          KillTimer(hWnd, wParam);
        }
      } else {
        wnd->data_ = wnd->shrines_.match(item);
        SetWindowPos(hWnd, NULL, 0, 0, 300, 1024, SWP_NOZORDER | SWP_NOMOVE | SWP_HIDEWINDOW | SWP_NOACTIVATE);
        HDC hDC = GetDC(hWnd);
        int height = wnd->render(hDC);
        ReleaseDC(hWnd, hDC);
        POINT pt;
        GetCursorPos(&pt);
        int width = GetSystemMetrics(SM_CXSCREEN);
        SetWindowPos(hWnd, HWND_TOPMOST,
          pt.x + 10 + 300 > width - 50 ? pt.x - 10 - 300 : pt.x + 10,
          std::max<int>(pt.y - height, 50), 300, height + 5,
          SWP_SHOWWINDOW | SWP_NOACTIVATE);
        KillTimer(hWnd, wParam);
        wnd->cursor_ = pt;
        SetTimer(hWnd, TimerCursor, 50, NULL);
      }
    } else if (wParam == TimerUpdate) {
      wnd->shrines_.update();
      wnd->checkVersion();
    } else if (wParam == TimerCursor) {
      POINT pt;
      GetCursorPos(&pt);
      if (std::abs(pt.x - wnd->cursor_.x) > 15 || std::abs(pt.y - wnd->cursor_.y) > 15) {
        ShowWindow(hWnd, SW_HIDE);
        KillTimer(hWnd, wParam);
      }
    } else if (wParam == TimerForeground) {
      HWND hForeground = GetForegroundWindow();
      std::vector<wchar_t> fgBuf(GetWindowTextLength(hForeground) + 1);
      GetWindowText(hForeground, fgBuf.data(), fgBuf.size());
      std::wstring fgName(fgBuf.data());
      if (fgName == L"Path of Exile") {
        if (!wnd->hooked_) {
          RegisterHotKey(hWnd, HotkeyId, MOD_CONTROL, 'D');
          wnd->hooked_ = true;
        }
      } else {
        if (wnd->hooked_) {
          UnregisterHotKey(hWnd, HotkeyId);
          wnd->hooked_ = false;
        }
      }
    }
    return 0;
  case WM_NCHITTEST:
    return HTNOWHERE;
  case WM_TRAYNOTIFY: {
    int mode = LOWORD(lParam);
    if (lParam != WM_LBUTTONUP && lParam != WM_RBUTTONUP) return 0;
    POINT pt;
    GetCursorPos(&pt);
    int result = TrackPopupMenuEx(wnd->tray_, TPM_HORIZONTAL | TPM_LEFTALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
      pt.x, pt.y, hWnd, NULL);
    if (result == MenuRefresh) {
      wnd->shrines_.update();
      wnd->checkVersion();
    } else {
      PostQuitMessage(0);
    }
    return 0;
  }
  default:
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
  }
}

bool TooltipWindow::getClipboard(std::wstring& text) {
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
  if (!OpenClipboard(hWnd_)) return false;
  text.clear();
  HGLOBAL hGlobal = GetClipboardData(CF_UNICODETEXT);
  if (hGlobal) {
    wchar_t const* src = reinterpret_cast<wchar_t*>(GlobalLock(hGlobal));
    if (src) {
      text = src;
      GlobalUnlock(hGlobal);
    }
  }
  CloseClipboard();
  return !text.empty();
}
void TooltipWindow::clearClipboard() {
  if (!OpenClipboard(hWnd_)) return;
  EmptyClipboard();
  CloseClipboard();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
  TooltipWindow window(hInstance);

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return msg.wParam;
}
