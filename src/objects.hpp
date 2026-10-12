// data/objects.dat: NORMAL/SNOW class tables (tools/objects.py).
// Row i = OBJECT_POSITIONS class byte i (markers (000)..(140) confirm).
// Row: angle scale daylight shadow [file] [desc]; file is gfx path,
// <automatic>, disabled, or empty. Block select (season?) unconfirmed.
#pragma once
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace objects {

struct ObjDef {
  int angle = 0;
  double scale = 1.0;
  int daylight = 1, shadow = 1;
  std::string file, desc;
};

struct ObjTable {
  std::string name;  // NORMAL / SNOW
  std::vector<ObjDef> rows;
};

inline std::string Trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}
inline bool IsFileTok(const std::string &t) {
  return t == "<automatic>" || t == "disabled" ||
         t.compare(0, 3, "gfx") == 0;
}

// Whitespace tokens of one stripped row (empty when blank).
inline std::vector<std::string> Toks(const std::string &s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    size_t j = i;
    while (j < s.size() && s[j] != ' ' && s[j] != '\t') j++;
    if (j > i) out.push_back(s.substr(i, j - i));
    i = j;
  }
  return out;
}

inline std::vector<ObjTable> Parse(const std::string &d) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos <= d.size()) {
    size_t e = d.find('\n', pos);
    if (e == std::string::npos) e = d.size();
    std::string ln = d.substr(pos, e - pos);
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    lines.push_back(ln);
    if (e == d.size()) break;
    pos = e + 1;
  }
  std::vector<ObjTable> out;
  size_t i = 0;
  while (i < lines.size()) {
    std::string t = Trim(lines[i]);
    if (t.empty()) {
      i++;
      continue;
    }
    if (t[0] != '*') throw std::runtime_error("junk in objects.dat");
    if (i + 2 >= lines.size() || lines[i + 2].find("***") != 0)
      throw std::runtime_error("bad objects block header");
    out.push_back(ObjTable{});
    out.back().name = Trim(lines[i + 1]);
    i += 3;
    while (i < lines.size() && lines[i].find("***") != 0) {
      std::string v = Trim(lines[i++]);
      if (v.empty() || v.compare(0, 5, "Angle") == 0 || v[0] == '-')
        continue;
      auto tk = Toks(v);
      if (tk.size() < 4) throw std::runtime_error("short objects row");
      ObjDef r;
      r.angle = std::stoi(tk[0]);
      r.scale = std::strtod(tk[1].c_str(), nullptr);
      r.daylight = std::stoi(tk[2]);
      r.shadow = std::stoi(tk[3]);
      size_t k = 4;
      if (k < tk.size() && IsFileTok(tk[k])) r.file = tk[k++];
      for (; k < tk.size(); k++) {
        if (!r.desc.empty()) r.desc += " ";
        r.desc += tk[k];
      }
      out.back().rows.push_back(r);
    }
  }
  if (out.empty()) throw std::runtime_error("no objects blocks");
  return out;
}

}  // namespace objects
