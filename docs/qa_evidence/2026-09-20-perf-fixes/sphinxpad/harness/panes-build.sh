#!/usr/bin/env bash
# #PF4K sphinxpad re-measure (#MDSG): build the two bench test binaries from the BEFORE tree and
# the AFTER tree, given the SAME bench code (the implementer's test files), so the only difference
# measured is src/Conversations.cpp and src/FilePanes.cpp.
set -u
O=/tmp/rx/out; mkdir -p $O
for t in pb pa; do
  for q in 5 6; do
    d=/tmp/rx/build-$t-qt$q
    echo "=== configure $t qt$q $(date +%H:%M:%S)"
    cmake -S /tmp/rx/$t -B $d -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRELAY_QT_MAJOR=$q \
          -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer" > $O/panes-configure-$t-$q.log 2>&1 \
      || { echo "$t qt$q CONFIGURE FAILED"; tail -5 $O/panes-configure-$t-$q.log; continue; }
    echo "=== build $t qt$q $(date +%H:%M:%S)"
    nice cmake --build $d -j6 --target relay-conversations-tests relay-filepanes-tests \
      > $O/panes-build-$t-$q.log 2>&1 \
      && echo "$t qt$q BUILD OK" || { echo "$t qt$q BUILD FAILED"; grep -E "error:" $O/panes-build-$t-$q.log | head -8; }
  done
done
echo PANES_BUILD_DONE
