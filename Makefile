# monk_mouth — Makefile
# a doorway: the host rig lives in extras/host, and this forwards to it so
# `make test`, `make tool` and `make corpus` work from the repository root.
# (the Arduino build ignores this file.)

.PHONY: all test tool corpus clean

all test tool corpus clean:
	$(MAKE) -C extras/host $@
