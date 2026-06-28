#!/usr/bin/env node
'use strict';

// Interactive client for the aiie emulator's built-in debugger (nix/debugger.cpp).
//
// It connects to the debug socket, prints everything the debugger sends to the
// terminal, forwards your keystrokes as commands, and -- crucially -- detaches
// SAFELY on exit by sending the 'q' command first.
//
// Why 'q' matters: the debugger's read loop checks for read() == -1, but a plain
// TCP close makes its blocking read() return 0, which it never tests for -- so it
// spins in a tight loop and wedges the emulator, and any breakpoints stay set.
// 'q' makes the server remove all breakpoints, close the socket, and resume the
// CPU. So we always send it before disconnecting.
//
// Usage:
//   node tools/aiie-debug.js [host] [port]      (defaults: 127.0.0.1 12345)
//   AIIE_DEBUG_HOST / AIIE_DEBUG_PORT env vars also work.
//
// Press Ctrl-C to detach safely and exit.

const net = require('net');

const HOST = process.argv[2] || process.env.AIIE_DEBUG_HOST || '127.0.0.1';
const PORT = parseInt(process.argv[3] || process.env.AIIE_DEBUG_PORT || '12345', 10);

let exiting = false;

const socket = net.createConnection({ host: HOST, port: PORT }, () => {
  process.stderr.write(`Connected to aiie debugger at ${HOST}:${PORT}\r\n`);
  process.stderr.write(`Type debugger commands; press Ctrl-C to detach safely.\r\n`);

  // Raw mode so single-character commands (s, c, d, T, ...) reach the debugger
  // immediately instead of waiting for a line. The debugger does not echo, so
  // we echo locally to make typing visible.
  if (process.stdin.isTTY) process.stdin.setRawMode(true);
  process.stdin.resume();
});

// Show everything the debugger sends.
socket.on('data', (chunk) => process.stdout.write(chunk));

// Forward keystrokes to the debugger, intercepting Ctrl-C (0x03) for safe exit.
// (In raw mode Ctrl-C arrives as a byte rather than as SIGINT.)
process.stdin.on('data', (chunk) => {
  if (chunk.includes(0x03)) { safeExit(0); return; }
  process.stdout.write(chunk); // local echo
  socket.write(chunk);
});

function restoreTerminal() {
  if (process.stdin.isTTY) {
    try { process.stdin.setRawMode(false); } catch (_) {}
  }
  process.stdin.pause();
}

function safeExit(code) {
  if (exiting) return;
  exiting = true;

  const finish = () => { restoreTerminal(); process.exit(code); };

  if (socket.writable) {
    // Detach cleanly: remove breakpoints, close server-side, resume the CPU.
    socket.write('q', () => socket.end());
    socket.once('close', finish);
    setTimeout(finish, 500); // fallback if the server never closes
  } else {
    finish();
  }
}

socket.on('error', (err) => {
  process.stderr.write(`\r\nsocket error: ${err.message}\r\n`);
  restoreTerminal();
  process.exit(1);
});

socket.on('close', () => {
  if (!exiting) {
    process.stderr.write(`\r\nDebugger closed the connection.\r\n`);
    restoreTerminal();
    process.exit(0);
  }
});

// Backstops for when stdin is not a raw-mode TTY (e.g. piped input).
process.on('SIGINT', () => safeExit(0));
process.on('SIGTERM', () => safeExit(0));
