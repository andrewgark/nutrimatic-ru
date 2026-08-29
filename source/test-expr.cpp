#include "index.h"
#include "search.h"
#include "expr.h"

#include "fst/concat.h"

#include <algorithm>
#include <string>
#include <vector>

#include <stdio.h>
#include <stdlib.h>

using namespace fst;

static void TestIndex(const char *expr, const char *yes, const char *no) {
  // Write index

  FILE *fp = fopen("test-expr.index", "wb");
  if (fp == NULL) {
    fprintf(stderr, "FAIL: can't write test-expr.index\n");
    exit(1);
  }

  IndexWriter writer(fp);
  std::vector<std::string> str;
  if (yes != NULL) str.push_back(yes);
  if (no != NULL) str.push_back(no);
  std::sort(str.begin(), str.end());
  for (size_t i = 0; i < str.size(); ++i) writer.next(str[i].c_str(), 0, 1);
  writer.next(NULL, 0, 0);
  fclose(fp);

  // Parse expression

  if (getenv("DEBUG_FST") != NULL) fprintf(stderr, "### [%s]\n", expr);

  StdVectorFst fst;
  const char *p = ParseExpr(expr, &fst, false);
  if (p == NULL || *p != '\0') {
    fprintf(stderr, "FAIL: can't parse \"%s\"\n", p ? p : expr);
    exit(1);
  }

  // Read index

  fp = fopen("test-expr.index", "rb");
  if (fp == NULL) {
    fprintf(stderr, "FAIL: can't open test-expr.index\n");
    exit(1);
  }

  IndexReader reader(fp);
  ExprFilter filter(fst);
  SearchDriver sd(&reader, &filter, filter.start(), 1e-6);
  sd.next();

  // Verify results

  if (sd.text == NULL && yes == NULL) {
    if (getenv("DEBUG_FST") != NULL) fprintf(stderr, "-> NULL (ok)\n");
    return;
  }

  if (sd.text == NULL) {
    fprintf(stderr, "FAIL: [%s] -> NULL (expected \"%s\")\n", expr, yes);
    exit(1);
  }

  if (yes == NULL) {
    fprintf(stderr, "FAIL: [%s] -> \"%s\" (expected NULL)\n", expr, sd.text);
    exit(1);
  }

  if (strcmp(yes, sd.text)) {
    fprintf(stderr, "FAIL: [%s] -> \"%s\" (expected \"%s\")\n", expr, sd.text, yes);
    exit(1);
  }

  if (getenv("DEBUG_FST") != NULL) fprintf(stderr, "-> \"%s\" (ok)\n", yes);

  if (sd.text != NULL) {
    double score = sd.score;
    sd.next();
    if (sd.text != NULL && sd.score >= score) {
      fprintf(stderr, "FAIL: [%s] -> \"%s\" (extra)\n", expr, sd.text);
      exit(1);
    }
  }

  fclose(fp);
  remove("test-expr.index");
}

int main(int argc, char *argv[]) {
  TestIndex(
      "foo&bar",
      NULL,
      " ");

  TestIndex(
      "\"(((((m?o)?c)?h)?i)t?)_(h(a(t(o(ry?)?)?)?)?)?&_{5,}\" ",
      "chitchat ",
      "itch ");

  TestIndex(
      "(\"<(-may)?(-sit)?(tit)?(ble)?(com)?(iks)?(ial)?(im-b)?(-mon)?>\"&_{18}) ",
      "mayim bialiks sitcom ",
      "mayim bialiks common ");

  TestIndex(
      "([aehimnprsw]*&_*a_*&_*e_*&_*h_*&_*i_*&_*m_*&_*n_*&_*p_*&_*r_*&_*s_*&_*w_*) ",
      "new hampshire ",
      "minesweeper ship ");

  TestIndex(
      "<eelqsuuu> ",
      "equuleus ",
      "equus ");

  TestIndex(
      "(c?h?a?r?m?&____)(e?l?t?o?n?&____)(c?h?e?s?t?&____)(o?n?e?&__) ",
      "charlton heston ",
      "charmton heston ");

  TestIndex(
      "(<(cerb)?(ecto)?(lonm)?(ddog)?(fblo)?(iero)?(skey)?(ells)?(dwhi)?(atra)?(subj)?(odan)?(thel)?>&_{24}) ",
      "subject of blood and whiskey ",
      "subject of blood and whisubj ");

  TestIndex(
      "\"<(cs)(dy)(er)(i)(mo)(n)(th)(__?)>\" ",
      "thermodynamics ",
      "thermodyanmics ");

  TestIndex(
      "(<waterhegm>&_*w_*a_*t_*e_*r_*) ",
      "wheat germ ",
      "merge what ");

  TestIndex(
      "<het><ral><seg><tan><rut><bla><oody><afl><ndi><cin><awe><ter> ",
      "the largest natural body of land in ice water ",
      "the largest natural body of water in iceland ");

  /* Literal: a-z, 0-9, space */
  TestIndex(
      "abc 123 ",
      "abc 123 ",
      "abc 12 ");

  /* Alternation | */
  TestIndex(
      "(cat|dog) ",
      "dog ",
      "car ");

  /* . (any single character) */
  TestIndex(
      "c.t ",
      "cat ",
      "ct ");

  /* + (one or more) */
  TestIndex(
      "a+ ",
      "aaa ",
      " ");

  /* {} repetition */
  TestIndex(
      "a{2,3} ",
      "aaa ",
      "a ");

  /* # (digit class) */
  TestIndex(
      "### ",
      "123 ",
      "12 ");

  /* 867 + 4 digits (phone-style); - would be optional space in full pattern */
  TestIndex(
      "867 #### ",
      "867 5309 ",
      "867 53 ");

  /* - (hyphen) = optional space: a-b matches "a b" or "ab", not "axb" */
  TestIndex(
      "a-b ",
      "a b ",
      "axb ");

  /* _ (underscore) = alphanumeric: Latin [a-z0-9] + Cyrillic letters (UTF-8) */
  TestIndex(
      "_+ ",
      "x1 ",
      " ");

  /* A (alphabetic [a-z]) */
  TestIndex(
      "A+ ",
      "abc ",
      "123 ");

  /* C (consonant), V (vowel) */
  TestIndex(
      "V+ ",
      "aeiou ",
      "xyz ");

  TestIndex(
      "C* ",
      "xyz ",
      "ae ");

  /* "expr" = no word break inside; "ab" matches "ab" not "a b" */
  TestIndex(
      "\"ab\" ",
      "ab ",
      "a b ");

  /* Example: facetiously = C*aC*eC*iC*oC*uC*yC* */
  TestIndex(
      "C*aC*eC*iC*oC*uC*yC* ",
      "facetiously ",
      "facetious ");

  /* Example: _ _ _ _*burger (single-letter words then burger) */
  TestIndex(
      "_ _ _ _*burger ",
      "l o l burger ",
      "lol burger ");

  /* [] character class and range [a-z] */
  TestIndex(
      "[a-z]+ ",
      "hello ",
      "123 ");

  TestIndex(
      "[0-9]+ ",
      "42 ",
      "4a ");

  /* Russian (Cyrillic UTF-8): anagram <привет> */
  TestIndex(
      "<\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82> ",
      "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 " /* привет */,
      "\xd0\xbf\xd1\x80\xd0\xb5\xd0\xb2\xd0\xb5\xd1\x82 " /* превет (wrong) */);

  /* Russian: literal phrase москва */
  TestIndex(
      "\xd0\xbc\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 " /* москва */,
      "\xd0\xbc\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 " /* москва */,
      "\xd0\xbc\xd0\xb0\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 " /* масква */);

  /* Russian: _ is one letter/digit; "____ва" matches москва (four letters + ва) */
  TestIndex(
      "\"____\xd0\xb2\xd0\xb0\" " /* "____ва" */,
      "\xd0\xbc\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 " /* москва */,
      "\xd0\xb2\xd0\xb0 " /* ва */);

  /* Russian: . is one letter/digit/space (UTF-8), not one high byte.
     "...ь" must match день (4 letters), not "и ь" (letter + space + ь). */
  TestIndex(
      "\"...\xd1\x8c\" " /* "...ь" */,
      "\xd0\xb4\xd0\xb5\xd0\xbd\xd1\x8c " /* день */,
      "\xd0\xb8 \xd1\x8c " /* и ь */);

  /* Russian: [^б] forbids only б as a letter, not the shared UTF-8 lead byte.
     "[^б]ень" matches день; must not treat D0 as a forbidden raw byte. */
  TestIndex(
      "\"[^\xd0\xb1]\xd0\xb5\xd0\xbd\xd1\x8c\" " /* "[^б]ень" */,
      "\xd0\xb4\xd0\xb5\xd0\xbd\xd1\x8c " /* день */,
      "\xd0\xb1\xd0\xb5\xd0\xbd\xd1\x8c " /* бень */);

  /* Russian: [^а]а matches ба (Cyrillic), not only Latin/digit + а. */
  TestIndex(
      "\"[^\xd0\xb0]\xd0\xb0\" " /* "[^а]а" */,
      "\xd0\xb1\xd0\xb0 " /* ба */,
      "\xd0\xb0\xd0\xb0 " /* аа */);

  /* Russian: code-point character class [мо][мо]сква matches москва */
  TestIndex(
      "[\xd0\xbc\xd0\xbe][\xd0\xbc\xd0\xbe]\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 ",
      "\xd0\xbc\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 " /* москва */,
      "\xd0\xbc\xd0\xb0\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 " /* масква */);

  /* Russian: range [а-я] matches one Cyrillic lowercase letter */
  TestIndex(
      "[\xd0\xb0-\xd1\x8f]\xd0\xbe\xd0\xb3\xd0\xbe " /* [а-я]ого */,
      "\xd1\x82\xd0\xbe\xd0\xb3\xd0\xbe " /* того */,
      "0\xd0\xbe\xd0\xb3\xd0\xbe " /* 0ого (digit not in [а-я]) */);

  /* R = Cyrillic letter [а-яё] */
  TestIndex(
      "R+ ",
      "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 " /* привет */,
      "123 ");

  /* G = Cyrillic vowel; S = Cyrillic consonant */
  TestIndex(
      "G+ ",
      "\xd0\xb0\xd1\x83 " /* ау */,
      "\xd0\xb1\xd0\xb2 " /* бв */);

  TestIndex(
      "S+ ",
      "\xd0\xb1\xd0\xb2\xd0\xb3 " /* бвг */,
      "\xd0\xb0\xd0\xb5 " /* ае */);

  /* _ = same letter/digit set as former L+ (Latin + Cyrillic) */
  TestIndex(
      "_+ ",
      "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 " /* привет */,
      " ");

  /* Cyrillic + Latin: hello + R+ */
  TestIndex(
      "helloR+ ",
      "hello\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 " /* helloпривет */,
      "hello world ");

  /* Latin + Cyrillic: A*R+ */
  TestIndex(
      "A*R+ ",
      "\xd0\xbc\xd0\xb8\xd1\x80 " /* мир */,
      "123 ");

  /* Cyrillic + numbers: R+#+ */
  TestIndex(
      "R+#+ ",
      "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82""123 " /* привет123 */,
      "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82""abc " /* приветabc */);

  /* _ mixes Latin, Cyrillic, digits (same as former L*99L*) */
  TestIndex(
      "_*99_* ",
      "x99\xd0\xb0 " /* x99а */,
      "x98\xd0\xb0 " /* x98а - 98 not 99 */);

  return 0;
}
