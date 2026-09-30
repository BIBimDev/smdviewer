import { detectBrep } from "./brepDetector";
import { tokenizeSmd } from "./tokenizer";
import type { ParsedSmd, ProfileEdge, Token, Vec3 } from "./types";

function getNumber(token: Token | undefined): number | undefined {
  return token?.type === "number" ? token.value : undefined;
}

function parseHeaderAndBlockLines(text: string): {
  headerLines: string[];
  blockLines: string[];
} {
  const lines = text.split(/\r?\n/);
  const blockStart = lines.findIndex((line) => line.trim() === "#10125");

  if (blockStart < 0) {
    throw new Error("#10125 main geometry block was not found.");
  }

  return {
    headerLines: lines.slice(0, blockStart),
    blockLines: lines.slice(blockStart),
  };
}

function parseProfileEdges(tokens: Token[]): ProfileEdge[] {
  const firstEdgeIndex = tokens.findIndex(
    (token) => token.type === "string" && /^edge\[.+\]\[0\]$/.test(token.value),
  );

  if (firstEdgeIndex < 1) {
    return [];
  }

  const countToken = tokens[firstEdgeIndex - 1];
  const edgeCount = getNumber(countToken);
  if (edgeCount === undefined || !Number.isInteger(edgeCount) || edgeCount < 1) {
    return [];
  }

  const edges: ProfileEdge[] = [];
  let cursor = firstEdgeIndex;

  for (let i = 0; i < edgeCount; i += 1) {
    const idToken = tokens[cursor];
    const edgeType = getNumber(tokens[cursor + 1]);
    const x1 = getNumber(tokens[cursor + 2]);
    const y1 = getNumber(tokens[cursor + 3]);
    const x2 = getNumber(tokens[cursor + 4]);
    const y2 = getNumber(tokens[cursor + 5]);

    if (
      idToken?.type !== "string" ||
      edgeType === undefined ||
      x1 === undefined ||
      y1 === undefined ||
      x2 === undefined ||
      y2 === undefined
    ) {
      return [];
    }

    const match = idToken.value.match(/^edge\[(.+)\]\[(\d+)\]$/);
    if (!match) {
      return [];
    }

    edges.push({
      id: match[1],
      index: Number(match[2]),
      type: edgeType,
      start: { x: x1, y: y1 },
      end: { x: x2, y: y2 },
    });

    cursor += 6;
  }

  return edges;
}

function parseBasicMetadata(tokens: Token[]): {
  objectId?: string;
  name?: string;
  partType?: number;
  placementOrigin?: Vec3;
  xAxis?: Vec3;
  yAxis?: Vec3;
} {
  const blockIndex = tokens.findIndex(
    (token) => token.type === "block" && token.value === "#10125",
  );

  if (blockIndex < 0) {
    return {};
  }

  const objectIdToken = tokens[blockIndex + 2];
  const nameToken = tokens[blockIndex + 3];

  // In the confirmed #10125 samples, part type is at local block offset +11.
  const partType = getNumber(tokens[blockIndex + 11]);

  // In the confirmed samples the placement/orientation tuple begins at +17:
  // ox, oy, oz, Xaxis(3), Yaxis(3).
  const ox = getNumber(tokens[blockIndex + 17]);
  const oy = getNumber(tokens[blockIndex + 18]);
  const oz = getNumber(tokens[blockIndex + 19]);
  const xx = getNumber(tokens[blockIndex + 20]);
  const xy = getNumber(tokens[blockIndex + 21]);
  const xz = getNumber(tokens[blockIndex + 22]);
  const yx = getNumber(tokens[blockIndex + 23]);
  const yy = getNumber(tokens[blockIndex + 24]);
  const yz = getNumber(tokens[blockIndex + 25]);

  return {
    objectId: objectIdToken?.type === "string" ? objectIdToken.value : undefined,
    name: nameToken?.type === "string" ? nameToken.value : undefined,
    partType,
    placementOrigin:
      ox !== undefined && oy !== undefined && oz !== undefined
        ? { x: ox, y: oy, z: oz }
        : undefined,
    xAxis:
      xx !== undefined && xy !== undefined && xz !== undefined
        ? { x: xx, y: xy, z: xz }
        : undefined,
    yAxis:
      yx !== undefined && yy !== undefined && yz !== undefined
        ? { x: yx, y: yy, z: yz }
        : undefined,
  };
}

export function parseSmd10125(text: string): ParsedSmd {
  const { headerLines, blockLines } = parseHeaderAndBlockLines(text);
  const headerText = headerLines.join("\n");
  const geometryText = blockLines.join("\n");

  const headerTokens = tokenizeSmd(headerText);
  const geometryTokens = tokenizeSmd(geometryText);

  const firstHeaderString = headerTokens.find((token) => token.type === "string");
  const isDicadAsc = firstHeaderString?.value === "DICAD ASC";

  if (!isDicadAsc) {
    throw new Error('File does not begin with the expected "DICAD ASC" marker.');
  }

  const metadata = parseBasicMetadata(geometryTokens);
  const profileEdges = parseProfileEdges(geometryTokens);
  const brep = detectBrep(geometryTokens);

  return {
    isDicadAsc,
    headerLines,
    geometryBlockLines: blockLines,
    geometry: {
      ...metadata,
      profileEdges,
      brep,
    },
  };
}
