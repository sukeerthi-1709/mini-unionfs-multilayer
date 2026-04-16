#!/bin/bash
# ─────────────────────────────────────────────────────────────────────
# Mini-UnionFS Test Suite — Multi-Lower-Layer Edition
# Tests the original 3 cases PLUS multi-layer specific behavior.
# ─────────────────────────────────────────────────────────────────────
FUSE_BINARY="./mini_unionfs"
TEST_DIR="./unionfs_test_env"
UPPER_DIR="$TEST_DIR/upper"
LOWER1="$TEST_DIR/lower1"   # top-most lower (highest priority)
LOWER2="$TEST_DIR/lower2"   # bottom lower
MOUNT_DIR="$TEST_DIR/mnt"

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASSED${NC}"; }
fail() { echo -e "${RED}FAILED${NC}"; [ -n "$1" ] && echo "  $1"; }

echo -e "${CYAN}Starting Mini-UnionFS Multi-Layer Test Suite...${NC}"
echo

# ── Setup ─────────────────────────────────────────────────────────────
rm -rf "$TEST_DIR"
mkdir -p "$UPPER_DIR" "$LOWER1" "$LOWER2" "$MOUNT_DIR"

# Files for original tests
echo "base_only_content"    > "$LOWER1/base.txt"
echo "to_be_deleted"        > "$LOWER1/delete_me.txt"

# Files for multi-layer tests
echo "from_lower1_v1"       > "$LOWER1/priority.txt"   # lower1 (top) = wins
echo "from_lower2_v2"       > "$LOWER2/priority.txt"   # lower2 = hidden by lower1

echo "only_in_lower2"       > "$LOWER2/bottom_only.txt"  # only in lower2

echo "upper_wins"           > "$UPPER_DIR/override.txt"  # pre-placed in upper
echo "from_lower1"          > "$LOWER1/override.txt"     # should be hidden
echo "from_lower2"          > "$LOWER2/override.txt"     # should be hidden

# ── Mount ─────────────────────────────────────────────────────────────
$FUSE_BINARY "$UPPER_DIR" "$LOWER1" "$LOWER2" "$MOUNT_DIR"
sleep 1

# ── Test 1: Layer Visibility (lower1 file visible) ────────────────────
echo -n "Test 1: Lower-layer file visible in mount... "
if grep -q "base_only_content" "$MOUNT_DIR/base.txt" 2>/dev/null; then
    pass
else
    fail "base.txt not readable through mount"
fi

# ── Test 2: Copy-on-Write ─────────────────────────────────────────────
echo -n "Test 2: Copy-on-Write (lower1 → upper)... "
echo "modified_content" >> "$MOUNT_DIR/base.txt" 2>/dev/null
sleep 0.2

MNT_HAS=$(grep -c "modified_content" "$MOUNT_DIR/base.txt" 2>/dev/null)
UPP_HAS=$(grep -c "modified_content" "$UPPER_DIR/base.txt" 2>/dev/null)
LOW_HAS=$(grep -c "modified_content" "$LOWER1/base.txt" 2>/dev/null)

if [ "$MNT_HAS" -eq 1 ] && [ "$UPP_HAS" -eq 1 ] && [ "$LOW_HAS" -eq 0 ]; then
    pass
else
    fail "mnt=$MNT_HAS upper=$UPP_HAS lower=$LOW_HAS (want 1,1,0)"
fi

# ── Test 3: Whiteout ──────────────────────────────────────────────────
echo -n "Test 3: Whiteout (delete lower1 file)... "
rm "$MOUNT_DIR/delete_me.txt" 2>/dev/null
sleep 0.2

MNT_GONE=0; [ ! -f "$MOUNT_DIR/delete_me.txt" ] && MNT_GONE=1
LOW_SAFE=0;  [ -f "$LOWER1/delete_me.txt" ]       && LOW_SAFE=1
WH_EXISTS=0; [ -f "$UPPER_DIR/.wh.delete_me.txt" ] && WH_EXISTS=1

if [ "$MNT_GONE" -eq 1 ] && [ "$LOW_SAFE" -eq 1 ] && [ "$WH_EXISTS" -eq 1 ]; then
    pass
else
    fail "gone=$MNT_GONE lower_safe=$LOW_SAFE whiteout=$WH_EXISTS (want 1,1,1)"
fi

# ── Test 4: Upper wins over ALL lowers ───────────────────────────────
echo -n "Test 4: Upper-dir takes priority over all lowers... "
CONTENT=$(cat "$MOUNT_DIR/override.txt" 2>/dev/null)
if [ "$CONTENT" = "upper_wins" ]; then
    pass
else
    fail "got '$CONTENT', expected 'upper_wins'"
fi

# ── Test 5: Top lower wins over bottom lower ──────────────────────────
echo -n "Test 5: Top-most lower wins over deeper lower... "
CONTENT=$(cat "$MOUNT_DIR/priority.txt" 2>/dev/null)
if [ "$CONTENT" = "from_lower1_v1" ]; then
    pass
else
    fail "got '$CONTENT', expected 'from_lower1_v1'"
fi

# ── Test 6: File only in bottom lower is visible ─────────────────────
echo -n "Test 6: File only in lower2 (bottom) is visible... "
CONTENT=$(cat "$MOUNT_DIR/bottom_only.txt" 2>/dev/null)
if [ "$CONTENT" = "only_in_lower2" ]; then
    pass
else
    fail "got '$CONTENT', expected 'only_in_lower2'"
fi

# ── Test 7: CoW copies from top-most lower ───────────────────────────
echo -n "Test 7: CoW copies from top-most lower (lower1), not lower2... "
echo "mutated" >> "$MOUNT_DIR/priority.txt" 2>/dev/null
sleep 0.2

# upper should have been seeded from lower1 content
UPP_SRC=$(grep -c "from_lower1_v1" "$UPPER_DIR/priority.txt" 2>/dev/null)
LOW1_CLEAN=$(grep -c "mutated" "$LOWER1/priority.txt" 2>/dev/null)
LOW2_CLEAN=$(grep -c "mutated" "$LOWER2/priority.txt" 2>/dev/null)

if [ "$UPP_SRC" -ge 1 ] && [ "$LOW1_CLEAN" -eq 0 ] && [ "$LOW2_CLEAN" -eq 0 ]; then
    pass
else
    fail "upper_has_src=$UPP_SRC lower1_mutated=$LOW1_CLEAN lower2_mutated=$LOW2_CLEAN"
fi

# ── Test 8: Whiteout hides file across ALL lowers ────────────────────
echo -n "Test 8: Single whiteout hides file present in multiple lowers... "
rm "$MOUNT_DIR/priority.txt" 2>/dev/null   # may have been CoW'd into upper
sleep 0.2

MNT_GONE=0;  [ ! -f "$MOUNT_DIR/priority.txt" ]     && MNT_GONE=1
L1_SAFE=0;   [ -f "$LOWER1/priority.txt" ]           && L1_SAFE=1
L2_SAFE=0;   [ -f "$LOWER2/priority.txt" ]           && L2_SAFE=1
WH_OK=0;     [ -f "$UPPER_DIR/.wh.priority.txt" ]    && WH_OK=1

if [ "$MNT_GONE" -eq 1 ] && [ "$L1_SAFE" -eq 1 ] && \
   [ "$L2_SAFE" -eq 1 ] && [ "$WH_OK" -eq 1 ]; then
    pass
else
    fail "gone=$MNT_GONE l1_safe=$L1_SAFE l2_safe=$L2_SAFE whiteout=$WH_OK"
fi

# ── Test 9: Readdir shows merged unique entries ───────────────────────
echo -n "Test 9: Readdir merges all layers, no duplicates... "
LISTING=$(ls "$MOUNT_DIR" 2>/dev/null)
# override.txt, bottom_only.txt should appear; base.txt should appear
HAS_OVERRIDE=$(echo "$LISTING" | grep -c "^override\.txt$")
HAS_BOTTOM=$(echo "$LISTING"   | grep -c "^bottom_only\.txt$")
HAS_BASE=$(echo "$LISTING"     | grep -c "^base\.txt$")
# priority.txt was deleted in test 8 — should NOT appear
HAS_PRIORITY=$(echo "$LISTING" | grep -c "^priority\.txt$")

if [ "$HAS_OVERRIDE" -eq 1 ] && [ "$HAS_BOTTOM" -eq 1 ] && \
   [ "$HAS_BASE" -eq 1 ] && [ "$HAS_PRIORITY" -eq 0 ]; then
    pass
else
    fail "override=$HAS_OVERRIDE bottom=$HAS_BOTTOM base=$HAS_BASE priority=$HAS_PRIORITY"
    echo "  Full listing: $LISTING"
fi

# ── Teardown ──────────────────────────────────────────────────────────
echo
fusermount -u "$MOUNT_DIR" 2>/dev/null || umount "$MOUNT_DIR" 2>/dev/null
rm -rf "$TEST_DIR"
echo -e "${CYAN}Test Suite Completed.${NC}"
