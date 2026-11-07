// SPDX-License-Identifier: MPL-2.0
int frames = 0;
void main() { print("runaway loaded"); }
void onFrame() { frames++; while (true) {} }
