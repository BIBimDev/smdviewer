import type { Token } from "./types";

const NUMBER_RE = /^[+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?$/;

function decodeHexEscapes(value: string): string {
  return value.replace(/\\x([0-9a-fA-F]{2})/g, (_match, hex: string) =>
    String.fromCharCode(Number.parseInt(hex, 16)),
  );
}

export function tokenizeSmd(text: string): Token[] {
  const tokens: Token[] = [];
  const lines = text.split(/\r?\n/);

  lines.forEach((sourceLine, index) => {
    const lineNumber = index + 1;
    const raw = sourceLine.trim();

    if (raw.length === 0) {
      return;
    }

    if (/^#\d+$/.test(raw)) {
      tokens.push({ type: "block", value: raw, raw, line: lineNumber });
      return;
    }

    if (raw.startsWith('"') && raw.endsWith('"')) {
      const inner = raw.slice(1, -1);
      tokens.push({
        type: "string",
        value: decodeHexEscapes(inner),
        raw,
        line: lineNumber,
      });
      return;
    }

    if (NUMBER_RE.test(raw)) {
      tokens.push({ type: "number", value: Number(raw), raw, line: lineNumber });
      return;
    }

    // Preserve unexpected non-empty lines as strings so the parser can continue.
    tokens.push({ type: "string", value: raw, raw, line: lineNumber });
  });

  return tokens;
}
