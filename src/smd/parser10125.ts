import { detectBrep } from "./brepDetector";
import { tokenizeSmd } from "./tokenizer";
import type { ParsedSmd, ProfileEdge, Token, Vec3 } from "./types";

function getNumber(token: Token | undefined): number | undefined {
  return token?.type === "number" ? token.value : undefined;
}

function extractDocumentDesignation(headerLines: string[]): string | undefined {
  const tokens = tokenizeSmd(headerLines.join("\n"));
  const konIndex = tokens.findIndex(
    (token) => token.type === "string" && /\.kon$/i.test(token.value),
  );

  if (konIndex < 0) {
    return undefined;
  }

  // In BI301 the drawing designation is the nearest preceding non-empty
  // string before the source-KON path. Keep this deliberately heuristic
  // until further production samples confirm the header layout.
  for (let i = konIndex - 1; i >= 0; i -= 1) {
    const token = tokens[i];
    if (
      token.type === "string" &&
      token.value.length > 0 &&
      token.value !== "DICAD ASC" &&
      token.value !== "NotListRelevant"
    ) {
      return token.value;
    }
  }

  return undefined;
}

function splitIntoBlocks(lines: string[], firstBlockStart: number): Array<{
  tag: string;
  lines: string[];
}> {
  const starts: number[] = [];

  for (let i = firstBlockStart; i < lines.length; i += 1) {
    if (/^#\d+$/.test(lines[i].trim())) {
      starts.push(i);
    }
  }

  return starts.map((start, index) => {
    const end = index + 1 < starts.length ? starts[index + 1] : lines.length;
    return {
      tag: lines[start].trim(),
      lines: lines.slice(start, end),
    };
  });
}

function blockDesignation(tokens: Token[]): string | undefined {
  // BI301 evidence: record layout begins
  //   #tag, numeric record/version, object-id, name, designation, ...
  const token = tokens[4];
  return token?.type === "string" && token.value.length > 0 ? token.value : undefined;
}

function containsSupportedBrep(tokens: Token[]): boolean {
  try {
    detectBrep(tokens);
    return true;
  } catch {
    return false;
  }
}

function parseHeaderAndBlockLines(text: string): {
  headerLines: string[];
  blockLines: string[];
  blockTag: string;
  documentDesignation?: string;
} {
  const lines = text.split(/\r?\n/);
  const firstBlockStart = lines.findIndex((line) => /^#\d+$/.test(line.trim()));

  if (firstBlockStart < 0) {
    throw new Error("No STRAKON object block was found.");
  }

  const headerLines = lines.slice(0, firstBlockStart);
  const documentDesignation = extractDocumentDesignation(headerLines);
  const blocks = splitIntoBlocks(lines, firstBlockStart);

  // Production-file experiment (BI301): prefer the object whose own
  // designation matches the drawing designation from the SMD header.
  // Require a structurally valid explicit BREP so subordinate records
  // carrying the same text cannot be selected accidentally.
  if (documentDesignation) {
    for (const block of blocks) {
      const tokens = tokenizeSmd(block.lines.join("\n"));
      if (
        blockDesignation(tokens) === documentDesignation &&
        containsSupportedBrep(tokens)
      ) {
        return {
          headerLines,
          blockLines: block.lines,
          blockTag: block.tag,
          documentDesignation,
        };
      }
    }
  }

  // Preserve the original V1 behaviour for the plain/pocket laboratory
  // samples and for files in which no drawing-subject match is available.
  const fallback = blocks.find((block) => block.tag === "#10125");
  if (!fallback) {
    throw new Error(
      "No drawing-subject BREP match and no #10125 fallback block were found.",
    );
  }

  return {
    headerLines,
    blockLines: fallback.lines,
    blockTag: fallback.tag,
    documentDesignation,
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

function parseBasicMetadata(tokens: Token[], blockTag: string): {
  objectId?: string;
  name?: string;
  designation?: string;
  partType?: number;
  placementOrigin?: Vec3;
  xAxis?: Vec3;
  yAxis?: Vec3;
} {
  const blockIndex = tokens.findIndex(
    (token) => token.type === "block" && token.value === blockTag,
  );

  if (blockIndex < 0) {
    return {};
  }

  const objectIdToken = tokens[blockIndex + 2];
  const nameToken = tokens[blockIndex + 3];
  const designationToken = tokens[blockIndex + 4];

  const common = {
    objectId: objectIdToken?.type === "string" ? objectIdToken.value : undefined,
    name: nameToken?.type === "string" ? nameToken.value : undefined,
    designation:
      designationToken?.type === "string" && designationToken.value.length > 0
        ? designationToken.value
        : undefined,
  };

  // The remaining offsets are only confirmed for #10125. Do not apply
  // them to BI301's #10071 drawing-subject record.
  if (blockTag !== "#10125") {
    return common;
  }

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
    ...common,
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
  const { headerLines, blockLines, blockTag, documentDesignation } =
    parseHeaderAndBlockLines(text);
  const headerText = headerLines.join("\n");
  const geometryText = blockLines.join("\n");

  const headerTokens = tokenizeSmd(headerText);
  const geometryTokens = tokenizeSmd(geometryText);

  const firstHeaderString = headerTokens.find((token) => token.type === "string");
  const isDicadAsc = firstHeaderString?.value === "DICAD ASC";

  if (!isDicadAsc) {
    throw new Error('File does not begin with the expected "DICAD ASC" marker.');
  }

  const metadata = parseBasicMetadata(geometryTokens, blockTag);
  const profileEdges = parseProfileEdges(geometryTokens);
  const brep = detectBrep(geometryTokens);

  return {
    isDicadAsc,
    headerLines,
    geometryBlockLines: blockLines,
    geometryBlockTag: blockTag,
    documentDesignation,
    geometry: {
      ...metadata,
      profileEdges,
      brep,
    },
  };
}
