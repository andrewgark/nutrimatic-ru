#include "index.h"
#include "search.h"
#include "expr.h"

#include "fst/closure.h"
#include "fst/concat.h"
#include "fst/union.h"
#include "fst/vector-fst.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <set>
#include <vector>

using namespace fst;

typedef StdArc::StateId State;
typedef StdArc::Weight Weight;

/* UTF-8: decode next code point, advance *p, return code point or -1. */
static int utf8_decode(const char **p) {
  const unsigned char *s = (const unsigned char *)*p;
  if (*s == 0) return -1;
  int cp;
  if (s[0] < 0x80) {
    cp = s[0];
    *p = (const char *)(s + 1);
    return cp;
  }
  if (s[0] >= 0xC2 && s[0] < 0xE0 && s[1] != 0 && (s[1] & 0xC0) == 0x80) {
    cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    *p = (const char *)(s + 2);
    return cp;
  }
  if (s[0] >= 0xE0 && s[0] < 0xF0 && s[1] != 0 && s[2] != 0 &&
      (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
    cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    if (cp >= 0x800) {
      *p = (const char *)(s + 3);
      return cp;
    }
  }
  if (s[0] >= 0xF0 && s[0] < 0xF5 && s[1] != 0 && s[2] != 0 && s[3] != 0 &&
      (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
    cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) |
         (s[3] & 0x3F);
    if (cp >= 0x10000) {
      *p = (const char *)(s + 4);
      return cp;
    }
  }
  return -1;
}

/* Append UTF-8 encoding of code point cp to out. */
static void utf8_encode(int cp, std::vector<unsigned char> *out) {
  if (cp < 0) return;
  if (cp < 0x80) {
    out->push_back((unsigned char)cp);
  } else if (cp < 0x800) {
    out->push_back((unsigned char)(0xC0 | (cp >> 6)));
    out->push_back((unsigned char)(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back((unsigned char)(0xE0 | (cp >> 12)));
    out->push_back((unsigned char)(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back((unsigned char)(0x80 | (cp & 0x3F)));
  } else if (cp < 0x110000) {
    out->push_back((unsigned char)(0xF0 | (cp >> 18)));
    out->push_back((unsigned char)(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back((unsigned char)(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back((unsigned char)(0x80 | (cp & 0x3F)));
  }
}

/* Decode a single UTF-8 code point from byte sequence; return -1 if not exactly one. */
static int utf8_bytes_to_cp(const std::vector<unsigned char> &bytes) {
  if (bytes.empty()) return -1;
  const char *p = (const char *)bytes.data();
  const char *end = (const char *)bytes.data() + bytes.size();
  int cp = utf8_decode(&p);
  if (cp < 0 || p != end) return -1;
  return cp;
}

const char *ParseExpr(const char *p, StdMutableFst* fst, bool quoted) {
  p = ParseBranch(p, fst, quoted);
  while (p != NULL && *p == '|') {
    StdVectorFst branch;
    p = ParseBranch(p + 1, &branch, quoted);
    Union(fst, branch);
  }
  return p;
}

const char *ParseBranch(const char *p, StdMutableFst* fst, bool quoted) {
  std::vector<StdVectorFst> to_intersect;
  StdVectorFst first;
  p = ParseFactor(p, &first, quoted);
  to_intersect.push_back(first);
  while (p != NULL && *p == '&') {
    StdVectorFst next;
    p = ParseFactor(p + 1, &next, quoted);
    to_intersect.push_back(next);
  }
  IntersectExprs(to_intersect, fst);
  return p;
}

const char *ParseFactor(const char *p, StdMutableFst* fst, bool quoted) {
  fst->SetStart(fst->AddState());
  fst->SetFinal(fst->Start(), Weight::One());
  for (;;) {
    StdVectorFst piece;
    const char *n = ParsePiece(p, &piece, quoted);
    if (n == NULL) return p;
    Concat(fst, piece);
    p = n;
  }
}

const char *ParsePiece(const char *p, StdMutableFst* fst, bool quoted) {
  StdVectorFst one;
  p = ParseAtom(p, &one, quoted);
  if (p == NULL) return NULL;

  int min, max;
  if (*p == '*') {
    min = 0;
    max = INT_MAX;
    ++p;
  } else if (*p == '+') {
    min = 1;
    max = INT_MAX;
    ++p;
  } else if (*p == '?') {
    min = 0;
    max = 1;
    ++p;
  } else if (*p == '{') {
    min = strtoul(p + 1, (char**) &p, 10);
    if (*p == ',' && *(p + 1) == '}') {
      max = INT_MAX;
      ++p;
    } else if (*p == ',') {
      max = strtoul(p + 1, (char**) &p, 10);
    } else {
      max = min;
    }
    if (*p != '}' || max < min || (max > 255 && max < INT_MAX)) return NULL;
    ++p;
  } else {
    min = max = 1;
  }

  StdVectorFst many;
  many.SetStart(many.AddState());
  many.SetFinal(many.Start(), Weight::One());

  assert(max >= min && min >= 0);
  for (int i = 0; i <= min || (i <= max && max < INT_MAX); ++i) {
    if (i >= min) Union(fst, many);
    Concat(&many, one);
  }

  if (max >= INT_MAX) {
    Closure(&one, CLOSURE_STAR);
    Concat(&many, one);
    Union(fst, many);
  }

  return p;
}

/* Parse one character-class element (single char or ., _, etc.); append one or more
   code-point byte sequences to elements. Returns new pointer or NULL. */
typedef std::vector<std::vector<unsigned char>> CharClassElements;
static const char *ParseCharClassElement(const char *p, CharClassElements *elements) {
  if (p == NULL || *p == 0) return NULL;
  if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == ' ') {
    elements->push_back(std::vector<unsigned char>(1, (unsigned char)*p));
    return p + 1;
  }
  if ((unsigned char)*p >= 0x80) {
    const char *q = p;
    int cp = utf8_decode(&q);
    if (cp < 0) return NULL;
    std::vector<unsigned char> bytes;
    utf8_encode(cp, &bytes);
    elements->push_back(bytes);
    return q;
  }
  if (*p == '-') {
    elements->push_back(std::vector<unsigned char>(1, 0));
    elements->push_back(std::vector<unsigned char>(1, ' '));
    return p + 1;
  }
  if (*p == '.') {
    for (int ch = '0'; ch <= '9'; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    for (int ch = 'a'; ch <= 'z'; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    elements->push_back(std::vector<unsigned char>(1, ' '));
    for (int ch = 0x80; ch <= 0xFF; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    return p + 1;
  }
  if (*p == '_') {
    for (int ch = '0'; ch <= '9'; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    for (int ch = 'a'; ch <= 'z'; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    for (int ch = 0x80; ch <= 0xFF; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    return p + 1;
  }
  if (*p == '#') {
    for (int ch = '0'; ch <= '9'; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    return p + 1;
  }
  if (*p == 'A') {
    for (int ch = 'a'; ch <= 'z'; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    return p + 1;
  }
  if (*p == 'C') {
    for (int ch = 'a'; ch <= 'z'; ++ch)
      if (!strchr("aeiou", ch))
        elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    return p + 1;
  }
  if (*p == 'V') {
    for (int ch = 'a'; ch <= 'z'; ++ch)
      if (strchr("aeiou", ch))
        elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    return p + 1;
  }
  /* R = Cyrillic letter [а-яё]; S = Cyrillic consonant; G = Cyrillic vowel;
     L = alphanumeric including Cyrillic [a-z0-9а-яё] */
  if (*p == 'R') {
    for (int cp = 0x0430; cp <= 0x044F; ++cp) {
      std::vector<unsigned char> bytes;
      utf8_encode(cp, &bytes);
      elements->push_back(bytes);
    }
    std::vector<unsigned char> yo;
    utf8_encode(0x0451, &yo);  /* ё */
    elements->push_back(yo);
    return p + 1;
  }
  if (*p == 'G') {
    static const int cyr_vowels[] = {
      0x0430, 0x0435, 0x0451, 0x0438, 0x043E, 0x0443,
      0x044B, 0x044D, 0x044E, 0x044F
    };
    for (int i = 0; i < (int)(sizeof cyr_vowels / sizeof cyr_vowels[0]); ++i) {
      std::vector<unsigned char> bytes;
      utf8_encode(cyr_vowels[i], &bytes);
      elements->push_back(bytes);
    }
    return p + 1;
  }
  if (*p == 'S') {
    for (int cp = 0x0430; cp <= 0x044F; ++cp) {
      if (cp == 0x0430 || cp == 0x0435 || cp == 0x0438 || cp == 0x043E ||
          cp == 0x0443 || cp == 0x044B || cp == 0x044D || cp == 0x044E ||
          cp == 0x044F) continue;  /* skip vowels */
      std::vector<unsigned char> bytes;
      utf8_encode(cp, &bytes);
      elements->push_back(bytes);
    }
    /* ё is vowel, skip */
    return p + 1;
  }
  if (*p == 'L') {
    for (int ch = '0'; ch <= '9'; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    for (int ch = 'a'; ch <= 'z'; ++ch)
      elements->push_back(std::vector<unsigned char>(1, (unsigned char)ch));
    for (int cp = 0x0430; cp <= 0x044F; ++cp) {
      std::vector<unsigned char> bytes;
      utf8_encode(cp, &bytes);
      elements->push_back(bytes);
    }
    std::vector<unsigned char> yo;
    utf8_encode(0x0451, &yo);
    elements->push_back(yo);
    return p + 1;
  }
  return NULL;
}

const char *ParseAtom(const char *p, StdMutableFst* fst, bool quoted) {
  if (p == NULL) return NULL;

  if (*p == '"' && !quoted) {
    p = ParseExpr(p + 1, fst, true);
    if (p == NULL || *p != '"') return NULL;
    return p + 1;
  } else if (*p == '(') {
    p = ParseExpr(p + 1, fst, quoted);
    if (p == NULL || *p != ')') return NULL;
    return p + 1;
  } else if (*p == '<') {
    p = ParseAnagram(p + 1, fst, quoted);
    if (p == NULL || *p != '>') return NULL;
    return p + 1;
  }

  CharClassElements elements;
  bool negate = false;

  if (*p == '[') {
    if (*++p == '^') {
      negate = true;
      ++p;
    }
    while (*p != ']' && *p != '\0') {
      if (*p == '-') {
        if (elements.empty()) return NULL;
        std::vector<unsigned char> start_bytes = elements.back();
        elements.pop_back();
        ++p;
        int end_cp = utf8_decode(&p);
        if (end_cp < 0) return NULL;
        int start_cp = utf8_bytes_to_cp(start_bytes);
        if (start_cp < 0 || start_cp > end_cp) return NULL;
        for (int cp = start_cp; cp <= end_cp; ++cp) {
          std::vector<unsigned char> bytes;
          utf8_encode(cp, &bytes);
          elements.push_back(bytes);
        }
      } else {
        p = ParseCharClassElement(p, &elements);
        if (p == NULL) return NULL;
      }
    }
    if (*p != ']') return NULL;
    ++p;
  } else {
    p = ParseCharClassElement(p, &elements);
    if (p == NULL) return NULL;
  }

  State start = fst->AddState(), final = fst->AddState();
  fst->SetStart(start);
  fst->SetFinal(final, Weight::One());
  if (negate) {
    std::set<unsigned char> forbidden;
    for (size_t i = 0; i < elements.size(); ++i)
      for (size_t j = 0; j < elements[i].size(); ++j)
        forbidden.insert(elements[i][j]);
    for (int ch = '0'; ch <= '9'; ++ch)
      if (forbidden.find((unsigned char)ch) == forbidden.end())
        fst->AddArc(start, StdArc(ch, ch, Weight::One(), final));
    for (int ch = 'a'; ch <= 'z'; ++ch)
      if (forbidden.find((unsigned char)ch) == forbidden.end())
        fst->AddArc(start, StdArc(ch, ch, Weight::One(), final));
    if (forbidden.find(' ') == forbidden.end())
      fst->AddArc(start, StdArc(' ', ' ', Weight::One(), final));
    for (int ch = 0x80; ch <= 0xFF; ++ch)
      if (forbidden.find((unsigned char)ch) == forbidden.end())
        fst->AddArc(start, StdArc((char)(unsigned char)ch, (char)(unsigned char)ch,
                                  Weight::One(), final));
  } else {
    for (size_t i = 0; i < elements.size(); ++i) {
      const std::vector<unsigned char> &bytes = elements[i];
      if (bytes.size() == 1) {
        fst->AddArc(start, StdArc(bytes[0], bytes[0], Weight::One(), final));
      } else {
        State prev = start;
        for (size_t j = 0; j < bytes.size(); ++j) {
          State next_state = (j == bytes.size() - 1) ? final : fst->AddState();
          fst->AddArc(prev, StdArc(bytes[j], bytes[j], Weight::One(), next_state));
          prev = next_state;
        }
      }
    }
  }

  if (!quoted) {
    fst->AddArc(start, StdArc(' ', ' ', Weight::One(), start));
    fst->AddArc(final, StdArc(' ', ' ', Weight::One(), final));
  }

  return p;
}

const char *ParseCharClass(const char *p, std::vector<char>* out) {
  CharClassElements elements;
  const char *q = ParseCharClassElement(p, &elements);
  if (q == NULL) return NULL;
  out->clear();
  for (size_t i = 0; i < elements.size(); ++i)
    for (size_t j = 0; j < elements[i].size(); ++j)
      out->push_back((char)elements[i][j]);
  return q;
}
