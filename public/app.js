"use strict";

// Squares match the server: a1 = 0, h8 = 63. The board renders rank 8 first.
const GLYPH = {
  P: "♙", N: "♘", B: "♗", R: "♖", Q: "♕", K: "♔",
  p: "♟", n: "♞", b: "♝", r: "♜", q: "♛", k: "♚",
};

const boardEl   = document.getElementById("board");
const statusEl  = document.getElementById("status");
const evalEl    = document.getElementById("eval");
const evalFill  = document.getElementById("evalfill");
const depthEl   = document.getElementById("depth");
const nodesEl   = document.getElementById("nodes");
const searchEl  = document.getElementById("searchms");
const p50El     = document.getElementById("p50");
const p99El     = document.getElementById("p99");
const samplesEl = document.getElementById("samples");
const fenEl     = document.getElementById("fen");
const promoEl   = document.getElementById("promo");

let state = { pieces: new Array(64).fill(""), turn: "w", legal: [], last: "", status: "ongoing" };
let selected = null;
let pendingPromo = null;
let ws = null;

// ---------------------------------------------------------------- rendering

const squares = [];
for (let rank = 7; rank >= 0; rank--) {
  for (let file = 0; file < 8; file++) {
    const idx = rank * 8 + file;
    const el = document.createElement("div");
    el.className = "sq " + ((rank + file) % 2 === 0 ? "dark" : "light");
    el.dataset.idx = String(idx);
    el.addEventListener("click", () => onSquare(idx));
    boardEl.appendChild(el);
    squares[idx] = el;
  }
}

function nameOf(idx) {
  return "abcdefgh"[idx % 8] + (Math.floor(idx / 8) + 1);
}

function parseFen(fen) {
  const pieces = new Array(64).fill("");
  const placement = fen.split(" ")[0];
  let rank = 7, file = 0;
  for (const ch of placement) {
    if (ch === "/") { rank--; file = 0; }
    else if (ch >= "1" && ch <= "8") file += Number(ch);
    else { pieces[rank * 8 + file] = ch; file++; }
  }
  return pieces;
}

function findKing(white) {
  const want = white ? "K" : "k";
  return state.pieces.findIndex((p) => p === want);
}

function render() {
  const targets = selected === null
    ? []
    : state.legal.filter((m) => m.slice(0, 2) === nameOf(selected)).map((m) => m.slice(2, 4));

  for (let i = 0; i < 64; i++) {
    const el = squares[i];
    const piece = state.pieces[i];

    el.classList.remove("sel", "last", "check", "white-piece", "black-piece");
    el.innerHTML = "";

    if (piece) {
      const span = document.createElement("span");
      span.className = "piece";
      span.textContent = GLYPH[piece];
      el.appendChild(span);
      el.classList.add(piece === piece.toUpperCase() ? "white-piece" : "black-piece");
    }

    if (selected === i) el.classList.add("sel");
    if (state.last && (nameOf(i) === state.last.slice(0, 2) || nameOf(i) === state.last.slice(2, 4))) {
      el.classList.add("last");
    }

    if (targets.includes(nameOf(i))) {
      const mark = document.createElement("div");
      mark.className = piece ? "ring" : "dot";
      el.appendChild(mark);
    }
  }

  if (state.status === "check" || state.status === "checkmate") {
    const k = findKing(state.turn === "w");
    if (k >= 0) squares[k].classList.add("check");
  }

  fenEl.textContent = state.fen || "";
}

const MESSAGES = {
  ongoing: (t) => (t === "w" ? "Your move." : "Thinking…"),
  check: (t) => (t === "w" ? "You are in check." : "Bot is in check."),
  checkmate: (t) => (t === "w" ? "Checkmate — you lose." : "Checkmate — you win!"),
  stalemate: () => "Stalemate — draw.",
  draw_fifty: () => "Draw.",
  draw_material: () => "Draw — insufficient material.",
};

function setStatus() {
  const fn = MESSAGES[state.status] || MESSAGES.ongoing;
  statusEl.textContent = fn(state.turn);
  const over = ["checkmate", "stalemate", "draw_fifty", "draw_material"].includes(state.status);
  statusEl.classList.toggle("bad", over || state.status === "check");
}

function setEval(cp) {
  const pawns = cp / 100;
  evalEl.textContent = (pawns >= 0 ? "+" : "") + pawns.toFixed(2);
  // Squash to a percentage; ±6 pawns is visually "winning".
  const pct = 50 + 50 * Math.tanh(pawns / 4);
  evalFill.style.height = Math.max(2, Math.min(98, pct)) + "%";
}

// ---------------------------------------------------------------- interaction

function gameOver() {
  return ["checkmate", "stalemate", "draw_fifty", "draw_material"].includes(state.status);
}

function onSquare(idx) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  if (state.turn !== "w" || gameOver()) return;

  if (selected === null) {
    if (state.legal.some((m) => m.slice(0, 2) === nameOf(idx))) {
      selected = idx;
      render();
    }
    return;
  }

  if (selected === idx) { selected = null; render(); return; }

  const from = nameOf(selected), to = nameOf(idx);
  const matches = state.legal.filter((m) => m.slice(0, 2) === from && m.slice(2, 4) === to);

  if (matches.length === 0) {
    // Not a legal target: treat the click as selecting a different piece.
    selected = state.legal.some((m) => m.slice(0, 2) === to) ? idx : null;
    render();
    return;
  }

  if (matches.length > 1 && matches[0].length === 5) {
    // Same from/to, four different promotion pieces. Ask.
    pendingPromo = { from, to };
    promoEl.classList.remove("hidden");
    return;
  }

  sendMove(matches[0]);
}

function sendMove(uci) {
  selected = null;
  state.turn = "b";
  setStatus();
  render();
  ws.send(JSON.stringify({ t: "move", uci }));
}

promoEl.addEventListener("click", (e) => {
  const btn = e.target.closest("button");
  if (!btn || !pendingPromo) return;
  const uci = pendingPromo.from + pendingPromo.to + btn.dataset.p;
  pendingPromo = null;
  promoEl.classList.add("hidden");
  sendMove(uci);
});

document.getElementById("newgame").addEventListener("click", () => {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ t: "new" }));
});

// ---------------------------------------------------------------- latency HUD

const rtts = [];
const inflight = new Map();
let pingId = 0;

function percentile(sorted, p) {
  if (!sorted.length) return null;
  const i = Math.min(sorted.length - 1, Math.floor((p / 100) * sorted.length));
  return sorted[i];
}

function recordRtt(ms) {
  rtts.push(ms);
  if (rtts.length > 500) rtts.shift();
  const sorted = [...rtts].sort((a, b) => a - b);
  p50El.textContent = percentile(sorted, 50).toFixed(2) + " ms";
  p99El.textContent = percentile(sorted, 99).toFixed(2) + " ms";
  samplesEl.textContent = String(rtts.length);
}

function startPinging() {
  setInterval(() => {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    const id = ++pingId;
    inflight.set(id, performance.now());
    ws.send(JSON.stringify({ t: "ping", id }));
  }, 500);
}

// ---------------------------------------------------------------- transport

function connect() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(proto + "//" + location.host + "/");

  ws.onopen = () => { statusEl.textContent = "connected"; };

  ws.onmessage = (e) => {
    const msg = JSON.parse(e.data);

    if (msg.t === "pong") {
      const sent = inflight.get(msg.id);
      if (sent !== undefined) { inflight.delete(msg.id); recordRtt(performance.now() - sent); }
      return;
    }

    if (msg.t === "error") { statusEl.textContent = msg.msg; return; }
    if (msg.t !== "state") return;

    state = {
      pieces: parseFen(msg.fen),
      fen: msg.fen,
      turn: msg.turn,
      legal: msg.legal,
      last: msg.last,
      status: msg.status,
    };
    selected = null;

    setEval(msg.eval);
    depthEl.textContent  = msg.depth  ? String(msg.depth) : "–";
    nodesEl.textContent  = msg.nodes  ? msg.nodes.toLocaleString() : "–";
    searchEl.textContent = msg.depth  ? msg.searchMs + " ms" : "–";

    setStatus();
    render();
  };

  ws.onclose = () => {
    statusEl.textContent = "disconnected — retrying…";
    setTimeout(connect, 1000);
  };

  ws.onerror = () => ws.close();
}

render();
connect();
startPinging();
