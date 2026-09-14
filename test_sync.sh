#!/bin/bash
set -e

DIR="/home/krshnsaboo/IIITH/SEMESTER 1/AOS/2026201025_A3"
cd "$DIR"

echo "=== [Step 0] Cleaning up any old processes ==="
pkill -f "tracker_info.txt" 2>/dev/null || true
sleep 1

echo "=== [Step 1] Launching Tracker 1 and Tracker 2 ==="
./tracker/tracker tracker_info.txt 1 > tracker1.log 2>&1 &
PID_T1=$!
./tracker/tracker tracker_info.txt 2 > tracker2.log 2>&1 &
PID_T2=$!

sleep 2

echo "=== [Step 2] Client 1 on Tracker 1: Registers user1, logs in, creates groupA ==="
printf "create_user user1 pass1\nlogin user1 pass1\ncreate_group groupA\nquit\n" | ./client/client 127.0.0.1:9000 tracker_info.txt

sleep 1

echo "=== [Step 3] Client 2 on Tracker 2: Verifies groupA is replicated, requests to join ==="
# Temporary tracker info with Tracker 2 as primary to force connecting to Tracker 2
printf "127.0.0.1 8001\n127.0.0.1 8000\n" > tracker_info_t2.txt
OUTPUT_C2=$(printf "create_user user2 pass2\nlogin user2 pass2\nlist_groups\njoin_group groupA\nquit\n" | ./client/client 127.0.0.1:9001 tracker_info_t2.txt)
echo "$OUTPUT_C2"

if echo "$OUTPUT_C2" | grep -q "groupA"; then
    echo ">>> PASS: groupA was successfully replicated from Tracker 1 to Tracker 2!"
else
    echo ">>> FAIL: groupA was not found on Tracker 2!"
    exit 1
fi

sleep 1

echo "=== [Step 4] Client 1 on Tracker 1: Verifies join request from user2 is replicated, and accepts it ==="
OUTPUT_C1_REQ=$(printf "login user1 pass1\nlist_requests groupA\naccept_request groupA user2\nlist_requests groupA\nquit\n" | ./client/client 127.0.0.1:9000 tracker_info.txt)
echo "$OUTPUT_C1_REQ"

if echo "$OUTPUT_C1_REQ" | grep -q "user2"; then
    echo ">>> PASS: Join request from user2 was replicated to Tracker 1 and accepted!"
else
    echo ">>> FAIL: Join request for user2 was not found on Tracker 1!"
    exit 1
fi

echo "=== [Step 5] Killing Tracker 1 to test Client Failover ==="
kill -9 $PID_T1
sleep 1

echo "=== [Step 6] Client 1 executes commands with Tracker 1 offline (automatic failover to Tracker 2) ==="
OUTPUT_FAILOVER=$(printf "login user1 pass1\nlist_groups\nquit\n" | ./client/client 127.0.0.1:9000 tracker_info.txt)
echo "$OUTPUT_FAILOVER"

if echo "$OUTPUT_FAILOVER" | grep -q "Connected to Tracker 2"; then
    echo ">>> PASS: Client 1 successfully failed over to Tracker 2!"
else
    echo ">>> FAIL: Client 1 failover to Tracker 2 failed!"
    exit 1
fi

echo "=== [Step 7] Client 2 creates groupB on Tracker 2 while Tracker 1 is offline ==="
printf "login user2 pass2\ncreate_group groupB\nquit\n" | ./client/client 127.0.0.1:9001 tracker_info_t2.txt

echo "=== [Step 8] Reviving Tracker 1 to test Reconnection Catch-Up (WAL Replay) ==="
./tracker/tracker tracker_info.txt 1 > tracker1_revived.log 2>&1 &
PID_T1_NEW=$!

sleep 3

echo "=== [Step 9] Client 1 queries Tracker 1: Verifies groupB was caught up ==="
OUTPUT_REPLAY=$(printf "login user1 pass1\nlist_groups\nquit\n" | ./client/client 127.0.0.1:9000 tracker_info.txt)
echo "$OUTPUT_REPLAY"

if echo "$OUTPUT_REPLAY" | grep -q "groupB"; then
    echo ">>> PASS: Tracker 1 caught up missed groupB mutation from Tracker 2!"
else
    echo ">>> FAIL: Tracker 1 failed to catch up groupB!"
    exit 1
fi

echo "=== [Step 10] Clean teardown of all servers ==="
kill -9 $PID_T2 2>/dev/null || true
kill -9 $PID_T1_NEW 2>/dev/null || true
rm -f tracker_info_t2.txt tracker1.log tracker2.log tracker1_revived.log

echo ""
echo "=========================================================================="
echo "🎉 ALL MULTI-TRACKER SYNCHRONIZATION AND FAILOVER TESTS PASSED! 🎉"
echo "=========================================================================="
