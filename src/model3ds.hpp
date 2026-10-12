// .3ds model-assembly script (custom text, NOT raw 3DS; tools/model3ds.py).
// STARTSTRUCTURE lines: <indent>path p0 p1 p2 p3 index (111 files, 858 parts).
// Indent spaces = tree depth. MOVES/SHADOWS sections open, not parsed.
// ponytail: structure only; keyframes deferred to animation port.
#pragma once
#include <cctype>
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

}  // namespace model3ds
