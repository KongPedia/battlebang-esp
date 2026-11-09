"""Executable PR #59 ADC characterization before module extraction."""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def adc_reference(tmp_path_factory: pytest.TempPathFactory) -> Path:
    compiler = shutil.which("g++") or shutil.which("clang++")
    assert compiler, "A native C++ compiler is required"
    tmp_path = tmp_path_factory.mktemp("go2_piezo_adc_reference")
    source = tmp_path / "main.cpp"
    source.write_text(
        r'''
#include <cassert>
#include <cstdint>
#include <string>
#include "tests/fixtures/go2_full/piezo_adc_reference.h"
using namespace go2_full_reference;

static void qualify_and_arm(PiezoReference& piezo, uint32_t start = 100) {
  Candidate out;
  assert(!piezo.process({100, 100, 100}, start, out));
  assert(!piezo.process({100, 100, 100}, start + 30, out));
  assert(!piezo.process({100, 100, 100}, start + 60, out));
  assert(piezo.armed());
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string test = argv[1];
  Candidate out;

  if (test == "off") {
    PiezoReference piezo(Config{});
    assert(!piezo.process({3000, 3000, 3000}, 100, out));
    assert(!piezo.armed() && !piezo.capturing());
    return 0;
  }
  if (test == "qualify") {
    Config cfg; cfg.mask = Left | Right | Front;
    PiezoReference piezo(cfg); piezo.setEnabled(true);
    assert(!piezo.process({100, 2500, -1}, 100, out));
    assert(!piezo.process({100, 2500, -1}, 130, out));
    assert(piezo.qualified(Left));
    assert(!piezo.qualified(Right));
    assert(!piezo.qualified(Front));
    assert(!piezo.process({2500, 100, 100}, 160, out));
    assert(!piezo.process({2500, 100, 100}, 190, out));
    assert(piezo.qualified(Left));
    assert(piezo.qualified(Right));
    assert(piezo.qualified(Front));
    return 0;
  }
  if (test == "mask") {
    Config cfg; cfg.mask = Right;
    PiezoReference piezo(cfg); piezo.setEnabled(true);
    assert(!piezo.process({4095, 100, 4095}, 100, out));
    assert(!piezo.process({4095, 100, 4095}, 130, out));
    assert(!piezo.qualified(Left) && piezo.qualified(Right) && !piezo.qualified(Front));
    return 0;
  }
  if (test == "peak") {
    Config cfg; cfg.mask = Left | Right | Front;
    PiezoReference piezo(cfg); piezo.setEnabled(true); qualify_and_arm(piezo);
    assert(!piezo.process({2500, 2600, 2700}, 200, out));
    assert(piezo.capturing());
    assert(!piezo.process({2800, 3000, 2900}, 210, out));
    assert(piezo.process({2900, 2950, 2850}, 230, out));
    assert(out.target == 2 && out.peak == 3000 && out.event_ms == 200);
    return 0;
  }
  if (test == "cooldown") {
    Config cfg; cfg.cooldown_ms = 100;
    PiezoReference piezo(cfg); piezo.setEnabled(true); qualify_and_arm(piezo);
    assert(!piezo.process({2500, 100, -1}, 200, out));
    assert(piezo.process({2600, 100, -1}, 230, out));
    assert(!piezo.process({100, 100, -1}, 300, out));
    assert(!piezo.process({100, 100, -1}, 330, out));
    assert(!piezo.armed());
    assert(!piezo.process({100, 100, -1}, 360, out));
    assert(piezo.armed());
    return 0;
  }
  if (test == "toggle") {
    PiezoReference piezo(Config{}); piezo.setEnabled(true); qualify_and_arm(piezo);
    assert(!piezo.process({2600, 100, -1}, 200, out));
    assert(piezo.capturing());
    piezo.setEnabled(false);
    assert(!piezo.capturing() && !piezo.armed());
    assert(!piezo.process({3000, 3000, -1}, 240, out));
    piezo.setEnabled(true);
    assert(!piezo.process({100, 100, -1}, 250, out));
    assert(!piezo.capturing());
    return 0;
  }
  if (test == "wrap") {
    PiezoReference piezo(Config{}); piezo.setEnabled(true);
    const uint32_t start = 0xfffffff0u;
    assert(!piezo.process({100, 100, -1}, start, out));
    assert(!piezo.process({100, 100, -1}, 0x0eu, out));
    assert(!piezo.process({100, 100, -1}, 0x2cu, out));
    assert(piezo.armed());
    return 0;
  }
  return 2;
}
''',
        encoding="utf-8",
    )
    binary = tmp_path / "adc_reference"
    subprocess.run(
        [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT), str(source), "-o", str(binary)],
        check=True,
    )
    return binary


@pytest.mark.parametrize(
    "case",
    ("off", "qualify", "mask", "peak", "cooldown", "toggle", "wrap"),
)
def test_pr59_adc_characterization(adc_reference: Path, case: str) -> None:
    subprocess.run([str(adc_reference), case], check=True)
