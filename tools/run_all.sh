#!/bin/bash
# Full verification: every C++ suite (-Werror) + every UI suite. Usage: tools/run_all.sh [cpp|ui]
cd "$(dirname "$0")/.."
if [ "$1" != "ui" ]; then
for t in test_core test_regime test_validation test_probe test_viz test_scope test_normalize test_multibook test_hedge test_volrisk test_multibook_model test_llm test_volidx test_strike_insight test_session_track test_pricing_analysis; do
  g++ -std=c++23 -UNDEBUG -I include -I . -Wall -Wextra -Werror -DVT_SRC_DIR="\"$PWD\"" tests/$t.cpp -o /tmp/t_$t 2>&1 | grep -E "error" | head -2
  echo -n "$t: "; /tmp/t_$t 2>&1 | tail -1 | cut -c1-80
done
fi
if [ "$1" != "cpp" ]; then
cd tests/ui
for t in verify_scope verify_lens verify_cards verify_riskblocks verify_keylevels verify_sidebar verify_sidebar_xb verify_views verify_viewlayout verify_viewctl verify_volatility verify_pricing verify_chart verify_mbmodel verify_dte verify_bubbles; do
  echo -n "$t "; python3 $t.py 2>&1 | tail -1 | cut -c1-160
done
fi
