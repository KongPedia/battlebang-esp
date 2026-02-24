#!/usr/bin/env bash
# 게임 로직 단위 테스트 빌드 및 실행
# 필요: C++17, Google Test (brew install googletest 또는 conda install gtest)

set -e
cd "$(dirname "$0")/.."
ROOT="$PWD"

# Google Test 경로 (Homebrew / Miniconda / 시스템)
GTEST_INC=""
GTEST_LIB=""
GTEST_LIB_DIR=""
if [ -d "/opt/homebrew/include" ] && [ -f "/opt/homebrew/include/gtest/gtest.h" ]; then
  GTEST_INC="-I/opt/homebrew/include"
  GTEST_LIB="-L/opt/homebrew/lib -lgtest -pthread"
  GTEST_LIB_DIR="/opt/homebrew/lib"
elif [ -d "/usr/local/include/gtest" ]; then
  GTEST_INC="-I/usr/local/include"
  GTEST_LIB="-L/usr/local/lib -lgtest -pthread"
  GTEST_LIB_DIR="/usr/local/lib"
else
  # Miniconda 등에 설치된 gtest
  for dir in /opt/homebrew/Caskroom/miniconda/base/pkgs/gtest-* "/opt/homebrew/opt/googletest" "$CONDA_PREFIX"; do
    [ -z "$dir" ] && continue
    [ ! -f "$dir/include/gtest/gtest.h" ] && continue
    [ ! -d "$dir/lib" ] && continue
    GTEST_INC="-I$dir/include"
    GTEST_LIB="-L$dir/lib -lgtest -pthread"
    GTEST_LIB_DIR="$dir/lib"
    break
  done
fi

if [ -z "$GTEST_INC" ]; then
  echo "Google Test를 찾을 수 없습니다. 다음 중 하나를 실행하세요:"
  echo "  brew install googletest"
  echo "  또는: pip install platformio && pio test -e native"
  exit 1
fi

echo "Building tests..."
${CXX:-g++} -std=c++17 -O0 -g \
  $GTEST_INC -I"$ROOT/lib/game_logic" \
  "$ROOT/lib/game_logic/game_logic.cpp" \
  "$ROOT/tests/test_game_logic.cpp" \
  $GTEST_LIB \
  -o "$ROOT/.test_runner" 2>&1

echo "Running tests..."
if [ -n "$GTEST_LIB_DIR" ]; then
  export DYLD_LIBRARY_PATH="$GTEST_LIB_DIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
  export LD_LIBRARY_PATH="$GTEST_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
"$ROOT/.test_runner" 2>&1
echo "All tests passed."
