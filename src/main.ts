import "./style.css";
import { parseSmd10125 } from "./smd/parser10125";
import { parseNativeJson } from "./smd/nativeJson";
import type { Face, ParsedSmd, Plane, Vec3 } from "./smd/types";
import { boundingBoxText, createViewer } from "./viewer/renderer";

const app = document.querySelector<HTMLDivElement>("#app");
if (!app) {
  throw new Error("#app element not found");
}

app.innerHTML = `
  <div class="app-shell">
    <header class="toolbar">
      <div class="toolbar-brand">
        <div class="brand">STRAKON SMD Viewer <span>V1.0.3</span></div>
        <div class="subtitle">DICAD ASC + #10125 explicit BREP viewer and geometry inspector</div>
      </div>

      <div class="toolbar-groups">
        <section class="tool-group commands-group" aria-label="Commands">
          <div class="tool-group-title">Commands</div>
          <div class="tool-group-actions">
            <label class="button primary">
              Open .smd
              <input id="fileInput" type="file" accept=".smd,text/plain" hidden />
            </label>
            <label class="button" title="Open native C++ extractor output (JSON v1)">
              Open native JSON
              <input id="nativeJsonInput" type="file" accept=".json,application/json" hidden />
            </label>
            <button id="fitButton" class="button" type="button">Fit model</button>
          </div>
        </section>

        <section class="tool-group view-options-group" aria-label="View options">
          <div class="tool-group-title">View options</div>
          <div class="tool-group-actions">
            <button id="verticesButton" class="button" type="button" aria-pressed="false">Show vertices</button>
            <button id="faceSelectButton" class="button" type="button" aria-pressed="false">Select faces</button>
            <button id="faceColorsButton" class="button" type="button" aria-pressed="false">Face colors</button>
            <button id="transparencyButton" class="button" type="button" aria-pressed="false">Transparent</button>
            <button id="viewCubeButton" class="button active" type="button" aria-pressed="true">View cube</button>
          </div>
        </section>

        <section class="tool-group status-group" aria-label="Status">
          <div class="tool-group-title">Status</div>
          <div id="statusText" class="status-message">Ready.</div>
          <div class="status-chips" aria-label="Viewer status">
            <span id="renderModeStatus" class="status-chip">Solid</span>
            <span id="vertexModeStatus" class="status-chip">Vertices off</span>
            <span id="faceModeStatus" class="status-chip">Face select off</span>
          </div>
        </section>
      </div>
    </header>

    <main class="content">
      <aside class="sidebar">
        <section class="panel">
          <h2>File / Object</h2>
          <dl id="objectInfo" class="info-list">
            <dt>Status</dt><dd>Open an SMD file or native C++ JSON v1.</dd>
          </dl>
        </section>

        <section class="panel">
          <h2>Geometry</h2>
          <dl id="geometryInfo" class="info-list"></dl>
        </section>

        <section class="panel inspector-panel">
          <h2>Geometry Inspector</h2>
          <p class="inspector-help">
            Solid mode hides occluded back-side vertices. Transparent mode exposes them. Face-related vertices stay compact on the model; exact coordinates remain in this inspector.
          </p>

          <details id="vertexInspector" class="inspector-section" open>
            <summary>Vertices <span id="vertexCountBadge" class="count-badge">0</span></summary>
            <div class="table-scroll vertex-table-scroll">
              <table class="inspector-table">
                <thead>
                  <tr><th>Vertex</th><th>X</th><th>Y</th><th>Z</th></tr>
                </thead>
                <tbody id="vertexTableBody">
                  <tr><td colspan="4" class="empty-cell">Open an SMD file.</td></tr>
                </tbody>
              </table>
            </div>
          </details>

          <details id="faceInspector" class="inspector-section">
            <summary>Faces <span id="faceCountBadge" class="count-badge">0</span></summary>
            <div class="table-scroll face-table-scroll">
              <table class="inspector-table face-table">
                <thead>
                  <tr><th>Face</th><th>Name</th><th>Vertices</th></tr>
                </thead>
                <tbody id="faceTableBody">
                  <tr><td colspan="3" class="empty-cell">Open an SMD file.</td></tr>
                </tbody>
              </table>
            </div>
          </details>

          <div id="selectedFaceDetails" class="selected-face-details hidden"></div>
        </section>

        <section class="panel notes">
          <h2>V1.0.3 scope</h2>
          <p>V1.0.3 adds an interactive view cube, grouped commands/view/status controls, solid-vs-transparent vertex visibility, and a cleaner selected-face presentation.</p>
          <p>The V1 SMD parser and single-loop face model remain unchanged.</p>
        </section>
      </aside>

      <section id="viewer" class="viewer" tabindex="0">
        <div id="dropOverlay" class="drop-overlay">Drop .smd here</div>
      </section>
    </main>
  </div>
`;

function mustQuery<T extends Element>(selector: string): T {
  const element = document.querySelector<T>(selector);
  if (!element) {
    throw new Error(`Required UI element was not found: ${selector}`);
  }
  return element;
}

const viewerElement = mustQuery<HTMLElement>("#viewer");
const fileInput = mustQuery<HTMLInputElement>("#fileInput");
const nativeJsonInput = mustQuery<HTMLInputElement>("#nativeJsonInput");
const objectInfo = mustQuery<HTMLElement>("#objectInfo");
const geometryInfo = mustQuery<HTMLElement>("#geometryInfo");
const statusText = mustQuery<HTMLElement>("#statusText");
const dropOverlay = mustQuery<HTMLElement>("#dropOverlay");
const verticesButton = mustQuery<HTMLButtonElement>("#verticesButton");
const faceSelectButton = mustQuery<HTMLButtonElement>("#faceSelectButton");
const faceColorsButton = mustQuery<HTMLButtonElement>("#faceColorsButton");
const transparencyButton = mustQuery<HTMLButtonElement>("#transparencyButton");
const viewCubeButton = mustQuery<HTMLButtonElement>("#viewCubeButton");
const fitButton = mustQuery<HTMLButtonElement>("#fitButton");
const renderModeStatus = mustQuery<HTMLElement>("#renderModeStatus");
const vertexModeStatus = mustQuery<HTMLElement>("#vertexModeStatus");
const faceModeStatus = mustQuery<HTMLElement>("#faceModeStatus");
const vertexInspector = mustQuery<HTMLDetailsElement>("#vertexInspector");
const faceInspector = mustQuery<HTMLDetailsElement>("#faceInspector");
const vertexCountBadge = mustQuery<HTMLElement>("#vertexCountBadge");
const faceCountBadge = mustQuery<HTMLElement>("#faceCountBadge");
const vertexTableBody = mustQuery<HTMLTableSectionElement>("#vertexTableBody");
const faceTableBody = mustQuery<HTMLTableSectionElement>("#faceTableBody");
const selectedFaceDetails = mustQuery<HTMLElement>("#selectedFaceDetails");

const viewer = createViewer(viewerElement);

let currentParsed: ParsedSmd | null = null;
let verticesVisible = false;
let faceSelectMode = false;
let faceColorsEnabled = false;
let transparentMode = false;
let viewCubeVisible = true;
let uiHoveredVertex: number | null = null;
let uiHoveredFace: number | null = null;
let uiSelectedVertex: number | null = null;
let uiSelectedFace: number | null = null;

function esc(value: unknown): string {
  return String(value ?? "-")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function formatCoordinate(value: number): string {
  if (Math.abs(value - Math.round(value)) < 1e-6) {
    return String(Math.round(value));
  }
  return value.toFixed(3);
}

function profileSize(parsed: ParsedSmd): string {
  const edges = parsed.geometry.profileEdges;
  if (edges.length === 0) {
    return "not parsed";
  }

  const xs = edges.flatMap((edge) => [edge.start.x, edge.end.x]);
  const ys = edges.flatMap((edge) => [edge.start.y, edge.end.y]);
  const width = Math.max(...xs) - Math.min(...xs);
  const depth = Math.max(...ys) - Math.min(...ys);
  return `${width} × ${depth} mm`;
}

function planeSummary(plane?: Plane): string {
  if (!plane) {
    return "-";
  }

  const eps = 1e-6;
  const { a, b, c, d } = plane;

  if (Math.abs(a) > eps && Math.abs(b) < eps && Math.abs(c) < eps) {
    return `X = ${formatCoordinate(-d / a)}`;
  }
  if (Math.abs(b) > eps && Math.abs(a) < eps && Math.abs(c) < eps) {
    return `Y = ${formatCoordinate(-d / b)}`;
  }
  if (Math.abs(c) > eps && Math.abs(a) < eps && Math.abs(b) < eps) {
    return `Z = ${formatCoordinate(-d / c)}`;
  }

  return `${formatCoordinate(a)}x + ${formatCoordinate(b)}y + ${formatCoordinate(c)}z + ${formatCoordinate(d)} = 0`;
}

function updateInfo(parsed: ParsedSmd, sourceName: string): void {
  const g = parsed.geometry;
  const b = g.brep;

  objectInfo.innerHTML = `
    <dt>File</dt><dd>${esc(sourceName)}</dd>
    <dt>Format</dt><dd>DICAD ASC</dd>
    <dt>Block</dt><dd>#10125</dd>
    <dt>Name</dt><dd>${esc(g.name)}</dd>
    <dt>Object ID</dt><dd class="mono">${esc(g.objectId)}</dd>
    <dt>Teilart</dt><dd>${esc(g.partType)}</dd>
  `;

  geometryInfo.innerHTML = `
    <dt>Profile</dt><dd>${esc(profileSize(parsed))}</dd>
    <dt>Profile edges</dt><dd>${g.profileEdges.length}</dd>
    <dt>Vertices</dt><dd>${b.verticesWorld.length}</dd>
    <dt>Faces</dt><dd>${b.faces.length}</dd>
    <dt>Planes</dt><dd>${b.planes.length}</dd>
    <dt>Face names</dt><dd>${b.faceNames.length}</dd>
    <dt>BREP transform</dt><dd>${b.transform ? "yes" : "no"}</dd>
    <dt>Bounds</dt><dd>${esc(boundingBoxText(b.verticesWorld))}</dd>
  `;
}

function vertexRow(index: number): HTMLTableRowElement | null {
  return document.querySelector<HTMLTableRowElement>(`#vertex-row-${index}`);
}

function faceRow(index: number): HTMLTableRowElement | null {
  return document.querySelector<HTMLTableRowElement>(`#face-row-${index}`);
}

function scrollRowInsideTable(row: HTMLTableRowElement | null, center = false): void {
  if (!row) {
    return;
  }
  const scroller = row.closest<HTMLElement>(".table-scroll");
  if (!scroller) {
    return;
  }

  requestAnimationFrame(() => {
    const rowRect = row.getBoundingClientRect();
    const scrollerRect = scroller.getBoundingClientRect();
    const header = scroller.querySelector<HTMLElement>("thead");
    const stickyHeaderHeight = header?.getBoundingClientRect().height ?? 0;
    const visibleTop = scrollerRect.top + stickyHeaderHeight;
    const visibleBottom = scrollerRect.bottom;

    if (center) {
      const rowCenter = (rowRect.top + rowRect.bottom) / 2;
      const visibleCenter = (visibleTop + visibleBottom) / 2;
      scroller.scrollTop += rowCenter - visibleCenter;
      return;
    }

    if (rowRect.top < visibleTop) {
      scroller.scrollTop += rowRect.top - visibleTop;
    } else if (rowRect.bottom > visibleBottom) {
      scroller.scrollTop += rowRect.bottom - visibleBottom;
    }
  });
}

function setVertexRowHover(index: number | null, ensureVisible = false): void {
  if (uiHoveredVertex !== null) {
    vertexRow(uiHoveredVertex)?.classList.remove("is-hovered");
  }
  uiHoveredVertex = index;

  if (index !== null) {
    vertexInspector.open = true;
    const row = vertexRow(index);
    row?.classList.add("is-hovered");
    if (ensureVisible) {
      scrollRowInsideTable(row);
    }
  }
}

function setFaceRowHover(index: number | null, ensureVisible = false): void {
  if (uiHoveredFace !== null) {
    faceRow(uiHoveredFace)?.classList.remove("is-hovered");
  }
  uiHoveredFace = index;

  if (index !== null) {
    faceInspector.open = true;
    const row = faceRow(index);
    row?.classList.add("is-hovered");
    if (ensureVisible) {
      scrollRowInsideTable(row);
    }
  }
}

function faceVertexIndices(face: Face): number[] {
  return [...new Set(face.indices)];
}

function clearSelectedFaceVertexRows(): void {
  vertexTableBody.querySelectorAll("tr.is-face-vertex").forEach((row) => {
    row.classList.remove("is-face-vertex");
  });
}

function renderFaceDetails(faceIndex: number | null): void {
  if (!currentParsed || faceIndex === null) {
    selectedFaceDetails.classList.add("hidden");
    selectedFaceDetails.innerHTML = "";
    return;
  }

  const face = currentParsed.geometry.brep.faces[faceIndex];
  const vertices = currentParsed.geometry.brep.verticesWorld;
  const indices = faceVertexIndices(face);

  const chips = indices.map((index) => `
    <span class="face-vertex-chip" data-face-vertex-index="${index}" title="${esc(`V${index} (${formatCoordinate(vertices[index].x)}, ${formatCoordinate(vertices[index].y)}, ${formatCoordinate(vertices[index].z)})`)}">
      V${index}
    </span>
  `).join("");

  const coordinateRows = indices.map((index) => {
    const vertex = vertices[index];
    return `
      <tr>
        <td class="mono">V${index}</td>
        <td class="mono">${esc(formatCoordinate(vertex.x))}</td>
        <td class="mono">${esc(formatCoordinate(vertex.y))}</td>
        <td class="mono">${esc(formatCoordinate(vertex.z))}</td>
      </tr>
    `;
  }).join("");

  selectedFaceDetails.innerHTML = `
    <div class="selected-face-header">
      <span>Selected face F${faceIndex}</span>
      <span class="selected-face-name">${esc(face.name ?? "-")}</span>
    </div>
    <dl class="selected-face-meta">
      <dt>Plane</dt><dd class="mono">${esc(planeSummary(face.plane))}</dd>
      <dt>Vertices</dt><dd>${indices.length}</dd>
    </dl>
    <div class="selected-face-coordinates-title">Face vertices</div>
    <div class="face-vertex-chips">${chips}</div>
    <div class="selected-face-coordinates-title">Coordinates</div>
    <div class="table-scroll compact-table-scroll">
      <table class="inspector-table compact-table">
        <thead><tr><th>Vertex</th><th>X</th><th>Y</th><th>Z</th></tr></thead>
        <tbody>${coordinateRows}</tbody>
      </table>
    </div>
  `;
  selectedFaceDetails.classList.remove("hidden");

  for (const chip of selectedFaceDetails.querySelectorAll<HTMLElement>("[data-face-vertex-index]")) {
    const index = Number(chip.dataset.faceVertexIndex);
    chip.addEventListener("mouseenter", () => {
      viewer.setHoveredVertex(index);
      setVertexRowHover(index, true);
    });
    chip.addEventListener("mouseleave", () => {
      viewer.setHoveredVertex(null);
      setVertexRowHover(null);
    });
  }
}

function applySelectedVertexUi(vertexIndex: number | null): void {
  if (uiSelectedVertex !== null) {
    vertexRow(uiSelectedVertex)?.classList.remove("is-vertex-selected");
  }
  uiSelectedVertex = vertexIndex;

  if (vertexIndex === null) {
    return;
  }

  vertexInspector.open = true;
  const row = vertexRow(vertexIndex);
  row?.classList.add("is-vertex-selected");
  scrollRowInsideTable(row, true);
}

function applySelectedFaceUi(faceIndex: number | null): void {
  if (uiSelectedFace !== null) {
    faceRow(uiSelectedFace)?.classList.remove("is-selected");
  }
  clearSelectedFaceVertexRows();
  uiSelectedFace = faceIndex;

  if (!currentParsed || faceIndex === null) {
    renderFaceDetails(null);
    return;
  }

  faceInspector.open = true;
  vertexInspector.open = true;

  const row = faceRow(faceIndex);
  row?.classList.add("is-selected");
  scrollRowInsideTable(row, true);

  const face = currentParsed.geometry.brep.faces[faceIndex];
  const indices = faceVertexIndices(face);
  for (const vertexIndex of indices) {
    vertexRow(vertexIndex)?.classList.add("is-face-vertex");
  }
  if (indices.length > 0) {
    scrollRowInsideTable(vertexRow(indices[0]), true);
  }

  renderFaceDetails(faceIndex);
}

function updateSelectionModeClasses(): void {
  vertexInspector.classList.toggle("selection-enabled", verticesVisible);
  faceInspector.classList.toggle("selection-enabled", faceSelectMode);
}

function updateModeStatus(): void {
  renderModeStatus.textContent = transparentMode ? "Transparent" : "Solid";
  renderModeStatus.classList.toggle("active", transparentMode);
  vertexModeStatus.textContent = verticesVisible ? "Vertices on" : "Vertices off";
  vertexModeStatus.classList.toggle("active", verticesVisible);
  faceModeStatus.textContent = faceSelectMode ? "Face select on" : "Face select off";
  faceModeStatus.classList.toggle("active", faceSelectMode);
}

function populateInspector(parsed: ParsedSmd): void {
  const vertices = parsed.geometry.brep.verticesWorld;
  const faces = parsed.geometry.brep.faces;

  vertexCountBadge.textContent = String(vertices.length);
  faceCountBadge.textContent = String(faces.length);

  vertexTableBody.innerHTML = vertices.map((vertex: Vec3, index) => `
    <tr id="vertex-row-${index}" data-vertex-index="${index}" tabindex="0">
      <td class="mono vertex-id">V${index}</td>
      <td class="mono">${esc(formatCoordinate(vertex.x))}</td>
      <td class="mono">${esc(formatCoordinate(vertex.y))}</td>
      <td class="mono">${esc(formatCoordinate(vertex.z))}</td>
    </tr>
  `).join("");

  faceTableBody.innerHTML = faces.map((face, index) => `
    <tr id="face-row-${index}" data-face-index="${index}" tabindex="0">
      <td class="mono face-id">F${index}</td>
      <td title="${esc(face.name ?? "-")}">${esc(face.name ?? "-")}</td>
      <td class="mono face-vertices">${face.indices.map((vertexIndex) => `V${vertexIndex}`).join(", ")}</td>
    </tr>
  `).join("");

  for (const row of vertexTableBody.querySelectorAll<HTMLTableRowElement>("tr[data-vertex-index]")) {
    const index = Number(row.dataset.vertexIndex);

    row.addEventListener("mouseenter", () => {
      viewer.setHoveredVertex(index);
      setVertexRowHover(index);
    });
    row.addEventListener("mouseleave", () => {
      viewer.setHoveredVertex(null);
      setVertexRowHover(null);
    });
    row.addEventListener("focus", () => {
      viewer.setHoveredVertex(index);
      setVertexRowHover(index);
    });
    row.addEventListener("blur", () => {
      viewer.setHoveredVertex(null);
      setVertexRowHover(null);
    });
    row.addEventListener("click", () => {
      // Persistent vertex selection is intentionally disabled while Show vertices is OFF.
      if (!verticesVisible) {
        return;
      }
      const next = uiSelectedVertex === index ? null : index;
      viewer.selectVertex(next);
      applySelectedVertexUi(next);
    });
  }

  for (const row of faceTableBody.querySelectorAll<HTMLTableRowElement>("tr[data-face-index]")) {
    const index = Number(row.dataset.faceIndex);

    // Face hover remains useful in every mode. V1.0.3 keeps contributing
    // vertex labels compact while exact coordinates remain in the inspector.
    row.addEventListener("mouseenter", () => {
      viewer.setHoveredFace(index);
      setFaceRowHover(index);
    });
    row.addEventListener("mouseleave", () => {
      viewer.setHoveredFace(null);
      setFaceRowHover(null);
    });
    row.addEventListener("focus", () => {
      viewer.setHoveredFace(index);
      setFaceRowHover(index);
    });
    row.addEventListener("blur", () => {
      viewer.setHoveredFace(null);
      setFaceRowHover(null);
    });
    row.addEventListener("click", () => {
      // Clicking a face table row only creates persistent selection while
      // Face select mode is explicitly ON.
      if (!faceSelectMode) {
        return;
      }
      const next = uiSelectedFace === index ? null : index;
      viewer.selectFace(next);
      applySelectedFaceUi(next);
    });
  }

  applySelectedVertexUi(null);
  applySelectedFaceUi(null);
  setVertexRowHover(null);
  setFaceRowHover(null);
  updateSelectionModeClasses();
}

viewer.setInteractionHandlers({
  onVertexHover(vertexIndex) {
    setVertexRowHover(vertexIndex, true);
  },
  onVertexSelect(vertexIndex) {
    applySelectedVertexUi(vertexIndex);
  },
  onFaceHover(faceIndex) {
    setFaceRowHover(faceIndex, true);
  },
  onFaceSelect(faceIndex) {
    applySelectedFaceUi(faceIndex);
  },
});

function decodeBuffer(buffer: ArrayBuffer): string {
  try {
    return new TextDecoder("windows-1252").decode(buffer);
  } catch {
    return new TextDecoder("utf-8").decode(buffer);
  }
}

type InputSource = "TypeScript (SMD)" | "Native C++ (JSON v1)";

function loadParsed(parsed: ParsedSmd, sourceName: string, source: InputSource): void {
  currentParsed = parsed;
  viewer.load(parsed);
  viewer.setVerticesVisible(verticesVisible);
  viewer.setFaceSelectMode(faceSelectMode);
  viewer.setFaceColors(faceColorsEnabled);
  viewer.setTransparent(transparentMode);
  viewer.setViewCubeVisible(viewCubeVisible);
  updateModeStatus();
  updateInfo(parsed, sourceName);
  populateInspector(parsed);
  statusText.textContent = `Loaded ${sourceName} — ${source}: ${parsed.geometry.brep.verticesWorld.length} vertices, ${parsed.geometry.brep.faces.length} faces.`;
}

function loadError(error: unknown, sourceName: string, source: InputSource): void {
  currentParsed = null;
  viewer.clear();
  const message = error instanceof Error ? error.message : String(error);
  objectInfo.innerHTML = `<dt>Error</dt><dd>${esc(message)}</dd>`;
  geometryInfo.innerHTML = "";
  vertexCountBadge.textContent = "0";
  faceCountBadge.textContent = "0";
  vertexTableBody.innerHTML = `<tr><td colspan="4" class="empty-cell">No supported geometry.</td></tr>`;
  faceTableBody.innerHTML = `<tr><td colspan="3" class="empty-cell">No supported geometry.</td></tr>`;
  applySelectedVertexUi(null);
  applySelectedFaceUi(null);
  setVertexRowHover(null);
  setFaceRowHover(null);
  statusText.textContent = `Could not load ${sourceName} — ${source}: ${message}`;
}

// Only the newest file choice may update the viewer if file reads overlap.
let loadRequest = 0;

async function loadFile(file: File, source: InputSource = "TypeScript (SMD)"): Promise<void> {
  const request = ++loadRequest;
  statusText.textContent = `Reading ${file.name} — ${source}...`;
  try {
    const buffer = await file.arrayBuffer();
    if (request !== loadRequest) return;
    const parsed = source === "TypeScript (SMD)"
      ? parseSmd10125(decodeBuffer(buffer))
      : parseNativeJson(new TextDecoder("utf-8", { fatal: true }).decode(buffer));
    loadParsed(parsed, file.name, source);
  } catch (error) {
    if (request === loadRequest) loadError(error, file.name, source);
  }
}

fileInput.addEventListener("change", () => {
  const file = fileInput.files?.[0];
  fileInput.value = "";
  if (file) {
    void loadFile(file);
  }
});

nativeJsonInput.addEventListener("change", () => {
  const file = nativeJsonInput.files?.[0];
  nativeJsonInput.value = "";
  if (file) {
    void loadFile(file, "Native C++ (JSON v1)");
  }
});

verticesButton.addEventListener("click", () => {
  verticesVisible = !verticesVisible;
  viewer.setVerticesVisible(verticesVisible);
  verticesButton.classList.toggle("active", verticesVisible);
  verticesButton.setAttribute("aria-pressed", String(verticesVisible));
  verticesButton.textContent = verticesVisible ? "Vertices: ON" : "Show vertices";
  updateSelectionModeClasses();
  updateModeStatus();

  if (!verticesVisible) {
    applySelectedVertexUi(null);
  }
});

faceSelectButton.addEventListener("click", () => {
  faceSelectMode = !faceSelectMode;
  viewer.setFaceSelectMode(faceSelectMode);
  faceSelectButton.classList.toggle("active", faceSelectMode);
  faceSelectButton.setAttribute("aria-pressed", String(faceSelectMode));
  faceSelectButton.textContent = faceSelectMode ? "Face select: ON" : "Select faces";
  updateSelectionModeClasses();
  updateModeStatus();

  if (faceSelectMode) {
    faceInspector.open = true;
  } else {
    applySelectedFaceUi(null);
  }
});

faceColorsButton.addEventListener("click", () => {
  faceColorsEnabled = !faceColorsEnabled;
  viewer.setFaceColors(faceColorsEnabled);
  faceColorsButton.classList.toggle("active", faceColorsEnabled);
  faceColorsButton.setAttribute("aria-pressed", String(faceColorsEnabled));
});

transparencyButton.addEventListener("click", () => {
  transparentMode = !transparentMode;
  viewer.setTransparent(transparentMode);
  transparencyButton.classList.toggle("active", transparentMode);
  transparencyButton.setAttribute("aria-pressed", String(transparentMode));
  transparencyButton.textContent = transparentMode ? "Transparent: ON" : "Transparent";
  updateModeStatus();
});

viewCubeButton.addEventListener("click", () => {
  viewCubeVisible = !viewCubeVisible;
  viewer.setViewCubeVisible(viewCubeVisible);
  viewCubeButton.classList.toggle("active", viewCubeVisible);
  viewCubeButton.setAttribute("aria-pressed", String(viewCubeVisible));
});

fitButton.addEventListener("click", () => viewer.fit());

for (const eventName of ["dragenter", "dragover"]) {
  viewerElement.addEventListener(eventName, (event) => {
    event.preventDefault();
    dropOverlay.classList.add("visible");
  });
}

for (const eventName of ["dragleave", "drop"]) {
  viewerElement.addEventListener(eventName, (event) => {
    event.preventDefault();
    dropOverlay.classList.remove("visible");
  });
}

viewerElement.addEventListener("drop", (event) => {
  const file = event.dataTransfer?.files?.[0];
  if (file) {
    void loadFile(file);
  }
});


updateModeStatus();
