# SPDX-License-Identifier: GPL-2.0
include ../../common/Makefile.module

# Calibration blobs for the tests, generated on the host.
all: firmware
firmware:
	python3 tools/mkcal.py firmware/lfd430
.PHONY: firmware
