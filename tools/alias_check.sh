#!/bin/sh
# Both option spellings must configure the same non-default build.
set -eu
a=$(mktemp -d)
b=$(mktemp -d)
trap 'rm -r "$a" "$b"' EXIT
cmake -S . -B "$a" -DNSHVC_HOST=none -DNSHVC_RENDER=soft -DNSHVC_TESTS=ON -DNSHVC_EXAMPLES=ON >/dev/null
cmake -S . -B "$b" -DNK_HOST=none -DNK_RENDER=soft -DNK_TESTS=ON -DNK_EXAMPLES=ON >/dev/null
for pair in HOST=none RENDER=soft TESTS=ON EXAMPLES=ON; do
    key=${pair%%=*} value=${pair#*=}
    for dir in "$a" "$b"; do
        grep -Eq "^NK_${key}:[^=]+=${value}$" "$dir/CMakeCache.txt"
    done
done
cmake --build "$a" --target help | sort > "$a/targets"
cmake --build "$b" --target help | sort > "$b/targets"
cmp "$a/targets" "$b/targets"
cmake -S . -B "$a" -DNSHVC_HOST=null >/dev/null
grep -Eq '^NK_HOST:[^=]+=null$' "$a/CMakeCache.txt"
cmake -S . -B "$a" -DNK_HOST=none >/dev/null
grep -Eq '^NSHVC_HOST:[^=]+=none$' "$a/CMakeCache.txt"
echo 'alias_check: ok'
