#!/usr/bin/env bash
# Run after wikiextractor has finished (text/ must exist).
# Builds index from text/ and merges into wiki-merged.index.

set -e
cd "$(dirname "$0")"
PREFIX=ruwiki

echo "Building index (this may take hours)..."
find text -type f -print0 | xargs -0 cat | build/make-index "$PREFIX"

echo "Merging indexes (stage 1)..."
for x in 0 1 2 3 4 5 6 7 8 9; do
  build/merge-indexes 2 "$PREFIX".????"$x".index wiki-merged."$x".index
done

echo "Merging indexes (stage 2)..."
build/merge-indexes 5 wiki-merged.*.index wiki-merged.index

echo "Done. Use: build/find-expr wiki-merged.index '<query>'"
