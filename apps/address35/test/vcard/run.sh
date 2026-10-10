#!/bin/bash
# Host-side regression test for the vCard engine (vcard.c) - pure logic, no device.
# Round-trips a 23-field record and parses a hand-authored vCard 3.0.
set -e
cd "$(dirname "$0")"
cc -I shim -I ../.. -o vctest test.c stubs.c ../../vcard.c ../../vcardw.c
./vctest
