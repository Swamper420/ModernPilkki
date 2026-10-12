// .3ds model-assembly script (custom text, NOT raw 3DS; tools/model3ds.py).
// STARTSTRUCTURE lines: <indent>path p0 p1 p2 p3 index (111 files, 858 parts).
// Indent spaces = tree depth. STARTMOVES (99 files, 768 funcs): FUNCTION
// blocks with NEXT id list, LENGTH ms, DISABLE part list (`-` = none),
// PART blocks of (time,x,y,z) keyframes; short rows keep previous trailing
// values (sscanf-reuse guess — real rows are 4-wide except 21×3, 3×5),
// extras ignored. STARTSHADOWS (10 files): obj shadow-obj pairs.
#pragma once
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace model3ds {

struct Part {
  std::string path;
  int p[4] = {};
  int index = 0;
  int depth = 0;
};

// Parse one raw STRUCTURE line (leading spaces significant).
inline Part ParseLine(const std::string &ln) {
  size_t i = 0;
  while (i < ln.size() && ln[i] == ' ') i++;
  size_t ps = i;
  while (i < ln.size() && ln[i] != ' ' && ln[i] != '\t') i++;
  if (ps == i) throw std::runtime_error("bad 3ds line");
  Part pt;
  pt.depth = (int)ps;
  pt.path = ln.substr(ps, i - ps);
  if (pt.path.size() < 5 ||
      pt.path.compare(pt.path.size() - 4, 4, ".obj") != 0)
    throw std::runtime_error("bad 3ds path");
  int v[5], nv = 0;
  size_t j = i;
  while (nv < 5) {
    while (j < ln.size() && (ln[j] == ' ' || ln[j] == '\t')) j++;
    if (j >= ln.size()) break;
    size_t k = j + (ln[j] == '-' ? 1 : 0);
    if (k >= ln.size() || !isdigit((unsigned char)ln[k]))
      throw std::runtime_error("bad 3ds number");
    while (k < ln.size() && isdigit((unsigned char)ln[k])) k++;
    v[nv++] = std::stoi(ln.substr(j, k - j));
    j = k;
  }
  if (nv != 5) throw std::runtime_error("bad 3ds fields");
  while (j < ln.size() && (ln[j] == ' ' || ln[j] == '\t')) j++;
  if (j != ln.size()) throw std::runtime_error("trailing 3ds text");
  for (int q = 0; q < 4; q++) pt.p[q] = v[q];
  pt.index = v[4];
  return pt;
}

// Parse full file text: lines between <STARTSTRUCTURE>/<ENDSTRUCTURE>.
inline std::vector<Part> ParseStructure(const std::string &d) {
  const char *k0 = "<STARTSTRUCTURE>", *k1 = "<ENDSTRUCTURE>";
  size_t a = d.find(k0), b = d.find(k1);
  if (a == std::string::npos || b == std::string::npos || b < a)
    throw std::runtime_error("no 3ds STRUCTURE");
  std::vector<Part> out;
  size_t pos = a + 16;
  while (pos < b) {
    size_t e = d.find('\n', pos);
    if (e == std::string::npos || e > b) e = b;
    std::string ln = d.substr(pos, e - pos);
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    size_t s = 0;
    while (s < ln.size() && (ln[s] == ' ' || ln[s] == '\t')) s++;
    if (s < ln.size() && ln[s] != '/' && ln[s] != '<')
      out.push_back(ParseLine(ln));
    pos = e + 1;
  }
  return out;
}

// ---- MOVES keyframes ----

struct Key {
  int t = 0, x = 0, y = 0, z = 0;
};

struct MovePart {
  int index = 0;
  std::string name;
  std::vector<Key> keys;
};

struct Move {
  int id = 0;
  std::string name;
  std::vector<int> next;     // <NEXT=a[,b]> chain targets
  int length = 0;            // <LENGTH=ms>
  std::vector<int> disable;  // hidden part indices (`-` = none)
  std::vector<MovePart> parts;
};

struct Shadow {
  std::string obj, shadow;
};

// Comma int list; `-`/empty = none.
inline std::vector<int> ParseIdList(const std::string &s) {
  if (s.empty() || s == "-") return {};
  std::vector<int> out;
  size_t pos = 0;
  while (pos <= s.size()) {
    size_t e = s.find(',', pos);
    if (e == std::string::npos) e = s.size();
    out.push_back(std::stoi(s.substr(pos, e - pos)));
    pos = e + 1;
  }
  return out;
}

inline bool Starts(const std::string &s, const char *k) {
  return s.compare(0, strlen(k), k) == 0;
}

// Keyframe row: leading ints; missing trailing keep previous (0 at start).
inline Key ParseKeyRow(const char *p, const Key &prev, bool &havePrev) {
  Key k = havePrev ? prev : Key{};
  int *f[4] = {&k.t, &k.x, &k.y, &k.z};
  int n = 0;
  while (n < 4) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '\r' || *p == '\n') break;
    char *e;
    long v = std::strtol(p, &e, 10);
    if (e == p) throw std::runtime_error("bad keyframe row");
    *f[n++] = (int)v;
    p = e;
  }
  if (!n) throw std::runtime_error("empty keyframe row");
  havePrev = true;
  return k;
}

inline std::vector<Move> ParseMoves(const std::string &d) {
  const char *k0 = "<STARTMOVES>", *k1 = "<ENDMOVES>";
  size_t a = d.find(k0), b = d.find(k1);
  if (a == std::string::npos || b == std::string::npos || b < a)
    throw std::runtime_error("no 3ds MOVES");
  std::vector<Move> out;
  Move *mv = nullptr;
  MovePart *pt = nullptr;
  Key prev{};
  bool havePrev = false;
  size_t pos = a + strlen(k0);
  while (pos < b) {
    size_t e = d.find('\n', pos);
    if (e == std::string::npos || e > b) e = b;
    std::string ln = d.substr(pos, e - pos);
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    size_t s = 0;
    while (s < ln.size() && (ln[s] == ' ' || ln[s] == '\t')) s++;
    // rtrim: lure files pad tags (e.g. `<FUNCTION=0=X>   `).
    size_t te = ln.size();
    while (te > s && (ln[te - 1] == ' ' || ln[te - 1] == '\t')) te--;
    std::string tstr = ln.substr(s, te - s);
    pos = e + 1;
    if (tstr.empty() || tstr[0] == '*') continue;
    if (tstr[0] == '<') {
      if (Starts(tstr, "<FUNCTION=")) {
        size_t eq = tstr.find('=', 10);
        if (eq == std::string::npos || tstr.back() != '>')
          throw std::runtime_error("bad FUNCTION tag");
        out.push_back(Move{});
        mv = &out.back();
        mv->id = std::stoi(tstr.substr(10, eq - 10));
        mv->name = tstr.substr(eq + 1, tstr.size() - eq - 2);
        pt = nullptr;
        havePrev = false;
      } else if (Starts(tstr, "<NEXT=") || Starts(tstr, "<DISABLE=")) {
        if (!mv || tstr.back() != '>') throw std::runtime_error("stray tag");
        bool dis = tstr[1] == 'D';
        size_t v0 = dis ? 9 : 6;
        auto ids = ParseIdList(tstr.substr(v0, tstr.size() - v0 - 1));
        if (dis)
          mv->disable = ids;
        else
          mv->next = ids;
      } else if (Starts(tstr, "<LENGTH=")) {
        if (!mv || tstr.back() != '>') throw std::runtime_error("stray tag");
        mv->length = std::stoi(tstr.substr(8, tstr.size() - 9));
      } else if (Starts(tstr, "<PART=")) {
        if (!mv) throw std::runtime_error("PART outside FUNCTION");
        size_t eq = tstr.find('=', 6);
        if (eq == std::string::npos || tstr.back() != '>')
          throw std::runtime_error("bad PART tag");
        mv->parts.push_back(MovePart{});
        pt = &mv->parts.back();
        pt->index = std::stoi(tstr.substr(6, eq - 6));
        pt->name = tstr.substr(eq + 1, tstr.size() - eq - 2);
        havePrev = false;
      } else {
        throw std::runtime_error("unknown moves tag: " + tstr);
      }
      continue;
    }
    if (!pt) throw std::runtime_error("keyframes outside PART");
    pt->keys.push_back(ParseKeyRow(tstr.c_str(), prev, havePrev));
    prev = pt->keys.back();
  }
  if (out.empty()) throw std::runtime_error("empty MOVES");
  return out;
}

// <IMPORTMOVES> path <ENDIMPORTMOVES>: moves shared from another .3ds
// (12 files, e.g. human2_t.3ds -> human2.3ds). "" when inline MOVES used.
inline std::string ParseImportMoves(const std::string &d) {
  const char *k0 = "<IMPORTMOVES>", *k1 = "<ENDIMPORTMOVES>";
  size_t a = d.find(k0), b = d.find(k1);
  if (a == std::string::npos || b == std::string::npos || b < a) return "";
  size_t s = a + strlen(k0), t = b;
  while (s < t && isspace((unsigned char)d[s])) s++;
  while (t > s && isspace((unsigned char)d[t - 1])) t--;
  std::string p = d.substr(s, t - s);
  if (p.size() < 5 || p.compare(p.size() - 4, 4, ".3ds") != 0 ||
      p.find('\n') != std::string::npos)
    throw std::runtime_error("bad importmoves path");
  return p;
}

inline std::vector<Shadow> ParseShadows(const std::string &d) {
  const char *k0 = "<STARTSHADOWS>", *k1 = "<ENDSHADOWS>";
  size_t a = d.find(k0), b = d.find(k1);
  if (a == std::string::npos || b == std::string::npos || b < a)
    throw std::runtime_error("no 3ds SHADOWS");
  std::vector<Shadow> out;
  size_t pos = a + strlen(k0);
  while (pos < b) {
    size_t e = d.find('\n', pos);
    if (e == std::string::npos || e > b) e = b;
    std::string ln = d.substr(pos, e - pos);
    size_t s = 0, t = ln.size();
    while (s < t && isspace((unsigned char)ln[s])) s++;
    while (t > s && isspace((unsigned char)ln[t - 1])) t--;
    pos = e + 1;
    if (s == t) continue;
    size_t m = s;
    while (m < t && !isspace((unsigned char)ln[m])) m++;
    size_t n = m;
    while (n < t && isspace((unsigned char)ln[n])) n++;
    size_t q = n;
    while (q < t && !isspace((unsigned char)ln[q])) q++;
    if (m == s || n == q || q != t) throw std::runtime_error("bad shadow pair");
    Shadow sh{ln.substr(s, m - s), ln.substr(n, q - n)};
    if (sh.obj.size() < 5 || sh.shadow.size() < 5 ||
        sh.obj.compare(sh.obj.size() - 4, 4, ".obj") != 0 ||
        sh.shadow.compare(sh.shadow.size() - 4, 4, ".obj") != 0)
      throw std::runtime_error("bad shadow pair");
    out.push_back(sh);
  }
  return out;
}

struct Pose {
  double x = 0, y = 0, z = 0;
};

// Linear interp between keyframes, clamped. ponytail: linear assumed;
// blend mode unconfirmed in decomp, revisit if motion mismatches.
inline Pose Sample(const std::vector<Key> &ks, int t) {
  if (ks.empty()) throw std::runtime_error("sample of empty keys");
  if (t <= ks.front().t || ks.size() == 1)
    return {(double)ks.front().x, (double)ks.front().y, (double)ks.front().z};
  if (t >= ks.back().t)
    return {(double)ks.back().x, (double)ks.back().y, (double)ks.back().z};
  for (size_t i = 1; i < ks.size(); i++) {
    if (t <= ks[i].t) {
      double f = (double)(t - ks[i - 1].t) / (ks[i].t - ks[i - 1].t);
      return {ks[i - 1].x + f * (ks[i].x - ks[i - 1].x),
              ks[i - 1].y + f * (ks[i].y - ks[i - 1].y),
              ks[i - 1].z + f * (ks[i].z - ks[i - 1].z)};
    }
  }
  return {(double)ks.back().x, (double)ks.back().y, (double)ks.back().z};
}

}  // namespace model3ds
