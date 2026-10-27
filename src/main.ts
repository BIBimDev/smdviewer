import "./style.css";

import {
  parseSmd10125
} from "./smd/parser10125";

import type {
  ParsedSmd,
  Vec3
} from "./smd/types";

import {
  boundingBoxText,
  createViewer
} from "./viewer/renderer";


const app =
  document.querySelector<HTMLDivElement>(
    "#app"
  );

if (!app) {
  throw new Error(
    "#app element not found"
  );
}


app.innerHTML = `
  <div class="app-shell">

    <header class="toolbar">

      <div>
        <div class="brand">
          STRAKON SMD Viewer
          <span>V1.0.1-dev</span>
        </div>

        <div class="subtitle">
          DICAD ASC + #10125 explicit BREP viewer
        </div>
      </div>


      <div class="toolbar-actions">

        <label class="button primary">
          Open .smd

          <input
            id="fileInput"
            type="file"
            accept=".smd,text/plain"
            hidden
          />
        </label>


        <button
          id="verticesButton"
          class="button"
          type="button"
        >
          Show vertices
        </button>


        <button
          id="faceSelectButton"
          class="button"
          type="button"
        >
          Select faces
        </button>


        <button
          id="fitButton"
          class="button"
          type="button"
        >
          Fit model
        </button>

      </div>

    </header>


    <main class="content">

      <aside class="sidebar">

        <section class="panel">

          <h2>File / Object</h2>

          <dl
            id="objectInfo"
            class="info-list"
          >
            <dt>Status</dt>
            <dd>Open an SMD file.</dd>
          </dl>

        </section>


        <section class="panel">

          <h2>Geometry</h2>

          <dl
            id="geometryInfo"
            class="info-list"
          ></dl>

        </section>


        <section class="panel inspector-panel">

          <div class="inspector-panel-title">
            <h2>
              Geometry Inspector
            </h2>
          </div>


          <div class="inspector-section-header">

            <div class="inspector-section-title">
              Vertices
            </div>

            <span
              id="vertexCountBadge"
              class="inspector-badge"
            >
              0
            </span>

          </div>


          <div
            id="vertexTableWrap"
            class="vertex-table-wrap"
          >

            <table class="inspector-table">

              <thead>
                <tr>
                  <th>Vertex</th>
                  <th>X</th>
                  <th>Y</th>
                  <th>Z</th>
                </tr>
              </thead>

              <tbody id="vertexTableBody">

                <tr class="empty-row">
                  <td colspan="4">
                    Open an SMD file.
                  </td>
                </tr>

              </tbody>

            </table>

          </div>


          <div class="inspector-section-header face-section-header">

            <div class="inspector-section-title">
              Faces
            </div>

            <span
              id="faceCountBadge"
              class="inspector-badge"
            >
              0
            </span>

          </div>


          <div
            id="faceTableWrap"
            class="face-table-wrap"
          >

            <table class="inspector-table face-table">

              <thead>
                <tr>
                  <th>Face</th>
                  <th>Name</th>
                  <th>Vertices</th>
                </tr>
              </thead>

              <tbody id="faceTableBody">

                <tr class="empty-row">
                  <td colspan="3">
                    Open an SMD file.
                  </td>
                </tr>

              </tbody>

            </table>

          </div>


          <div
            id="selectedFaceInfo"
            class="selected-face-info"
          >
            Selected face: none
          </div>

        </section>


        <section class="panel notes">

          <h2>V1.0.1 development</h2>

          <p>
            individual BREP face meshes,
            face-table inspection, face picking and
            linked vertex highlighting.
          </p>

        </section>

      </aside>


      <section
        id="viewer"
        class="viewer"
        tabindex="0"
      >

        <div
          id="dropOverlay"
          class="drop-overlay"
        >
          Drop .smd here
        </div>

      </section>

    </main>


    <footer class="statusbar">

      <span id="statusText">
        Ready.
      </span>

    </footer>

  </div>
`;


function mustQuery<T extends Element>(
  selector: string
): T {

  const element =
    document.querySelector<T>(
      selector
    );

  if (!element) {
    throw new Error(
      `Required UI element was not found: ${selector}`
    );
  }

  return element;
}


const viewerElement =
  mustQuery<HTMLElement>(
    "#viewer"
  );

const fileInput =
  mustQuery<HTMLInputElement>(
    "#fileInput"
  );

const objectInfo =
  mustQuery<HTMLElement>(
    "#objectInfo"
  );

const geometryInfo =
  mustQuery<HTMLElement>(
    "#geometryInfo"
  );

const statusText =
  mustQuery<HTMLElement>(
    "#statusText"
  );

const dropOverlay =
  mustQuery<HTMLElement>(
    "#dropOverlay"
  );

const verticesButton =
  mustQuery<HTMLButtonElement>(
    "#verticesButton"
  );

const faceSelectButton =
  mustQuery<HTMLButtonElement>(
    "#faceSelectButton"
  );

const fitButton =
  mustQuery<HTMLButtonElement>(
    "#fitButton"
  );

const vertexTableBody =
  mustQuery<HTMLTableSectionElement>(
    "#vertexTableBody"
  );

const faceTableBody =
  mustQuery<HTMLTableSectionElement>(
    "#faceTableBody"
  );

const vertexCountBadge =
  mustQuery<HTMLElement>(
    "#vertexCountBadge"
  );

const faceCountBadge =
  mustQuery<HTMLElement>(
    "#faceCountBadge"
  );

const selectedFaceInfo =
  mustQuery<HTMLElement>(
    "#selectedFaceInfo"
  );


const viewer =
  createViewer(
    viewerElement
  );


let currentParsed:
  ParsedSmd | null =
  null;

let verticesVisible =
  false;

let faceSelectMode =
  false;

let hoveredVertexRow:
  number | null =
  null;

let hoveredFaceRow:
  number | null =
  null;

let selectedFaceRow:
  number | null =
  null;


function esc(
  value: unknown
): string {

  return String(
    value ?? "-"
  )
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}


function formatCoordinate(
  value: number
): string {

  const normalized =
    Math.abs(value) < 1e-9
      ? 0
      : value;

  const rounded =
    Math.round(normalized);

  if (
    Math.abs(
      normalized - rounded
    ) < 1e-6
  ) {
    return String(
      rounded
    );
  }

  return normalized.toFixed(3);
}


function profileSize(
  parsed: ParsedSmd
): string {

  const edges =
    parsed.geometry.profileEdges;

  if (
    edges.length === 0
  ) {
    return "not parsed";
  }

  const xs =
    edges.flatMap(
      (edge) => [
        edge.start.x,
        edge.end.x
      ]
    );

  const ys =
    edges.flatMap(
      (edge) => [
        edge.start.y,
        edge.end.y
      ]
    );

  const width =
    Math.max(...xs)
    -
    Math.min(...xs);

  const depth =
    Math.max(...ys)
    -
    Math.min(...ys);

  return `${width} × ${depth} mm`;
}


function updateInfo(
  parsed: ParsedSmd,
  sourceName: string
): void {

  const g =
    parsed.geometry;

  const b =
    g.brep;

  objectInfo.innerHTML = `
    <dt>File</dt>
    <dd>${esc(sourceName)}</dd>

    <dt>Format</dt>
    <dd>DICAD ASC</dd>

    <dt>Block</dt>
    <dd>#10125</dd>

    <dt>Name</dt>
    <dd>${esc(g.name)}</dd>

    <dt>Object ID</dt>
    <dd class="mono">
      ${esc(g.objectId)}
    </dd>

    <dt>Teilart</dt>
    <dd>${esc(g.partType)}</dd>
  `;

  geometryInfo.innerHTML = `
    <dt>Profile</dt>
    <dd>
      ${esc(profileSize(parsed))}
    </dd>

    <dt>Profile edges</dt>
    <dd>${g.profileEdges.length}</dd>

    <dt>Vertices</dt>
    <dd>${b.verticesWorld.length}</dd>

    <dt>Faces</dt>
    <dd>${b.faces.length}</dd>

    <dt>Planes</dt>
    <dd>${b.planes.length}</dd>

    <dt>Face names</dt>
    <dd>${b.faceNames.length}</dd>

    <dt>BREP transform</dt>
    <dd>
      ${b.transform ? "yes" : "no"}
    </dd>

    <dt>Bounds</dt>
    <dd>
      ${esc(
        boundingBoxText(
          b.verticesWorld
        )
      )}
    </dd>
  `;
}


function getVertexRow(
  index: number
): HTMLTableRowElement | null {

  return vertexTableBody
    .querySelector<HTMLTableRowElement>(
      `tr[data-vertex-index="${index}"]`
    );
}


function getFaceRow(
  index: number
): HTMLTableRowElement | null {

  return faceTableBody
    .querySelector<HTMLTableRowElement>(
      `tr[data-face-index="${index}"]`
    );
}


function setVertexRowHover(
  index: number | null,
  scrollToRow = false
): void {

  if (
    hoveredVertexRow !== null
  ) {
    getVertexRow(
      hoveredVertexRow
    )?.classList.remove(
      "is-hovered"
    );
  }

  hoveredVertexRow =
    index;

  if (
    index === null
  ) {
    return;
  }

  const row =
    getVertexRow(index);

  if (!row) {
    return;
  }

  row.classList.add(
    "is-hovered"
  );

  if (
    scrollToRow
  ) {
    row.scrollIntoView({
      block: "nearest",
      inline: "nearest"
    });
  }
}


function clearFaceRelatedVertexRows(): void {

  vertexTableBody
    .querySelectorAll<HTMLTableRowElement>(
      ".vertex-row.is-face-related"
    )
    .forEach(
      (row) => {
        row.classList.remove(
          "is-face-related"
        );
      }
    );
}


function refreshFaceRelatedVertexRows(): void {

  clearFaceRelatedVertexRows();

  if (
    !currentParsed
  ) {
    return;
  }

  const activeFaceIndex =
    hoveredFaceRow !== null
      ? hoveredFaceRow
      : selectedFaceRow;

  if (
    activeFaceIndex === null
  ) {
    return;
  }

  const face =
    currentParsed
      .geometry
      .brep
      .faces[
        activeFaceIndex
      ];

  if (!face) {
    return;
  }

  for (
    const vertexIndex of
    face.indices
  ) {

    getVertexRow(
      vertexIndex
    )?.classList.add(
      "is-face-related"
    );
  }
}


function setFaceRowHover(
  index: number | null,
  scrollToRow = false
): void {

  if (
    hoveredFaceRow !== null
  ) {

    getFaceRow(
      hoveredFaceRow
    )?.classList.remove(
      "is-hovered"
    );
  }

  hoveredFaceRow =
    index;

  if (
    index !== null
  ) {

    const row =
      getFaceRow(
        index
      );

    row?.classList.add(
      "is-hovered"
    );

    if (
      row
      &&
      scrollToRow
    ) {
      row.scrollIntoView({
        block: "nearest",
        inline: "nearest"
      });
    }
  }

  refreshFaceRelatedVertexRows();
}


function updateSelectedFaceInfo(): void {

  if (
    !currentParsed
    ||
    selectedFaceRow === null
  ) {

    selectedFaceInfo.textContent =
      "Selected face: none";

    return;
  }

  const face =
    currentParsed
      .geometry
      .brep
      .faces[
        selectedFaceRow
      ];

  if (!face) {
    selectedFaceInfo.textContent =
      "Selected face: none";

    return;
  }

  const faceName =
    currentParsed
      .geometry
      .brep
      .faceNames[
        selectedFaceRow
      ]
    ??
    face.name
    ??
    "-";

  const vertexText =
    face.indices
      .map(
        (index) =>
          `V${index}`
      )
      .join(", ");

  selectedFaceInfo.innerHTML = `
    <strong>
      Selected F${selectedFaceRow}
    </strong>

    <span>
      ${esc(faceName)}
    </span>

    <span>
      ${esc(vertexText)}
    </span>
  `;
}


function setFaceRowSelected(
  index: number | null,
  scrollToRow = false
): void {

  if (
    selectedFaceRow !== null
  ) {

    getFaceRow(
      selectedFaceRow
    )?.classList.remove(
      "is-selected"
    );
  }

  selectedFaceRow =
    index;

  if (
    index !== null
  ) {

    const row =
      getFaceRow(
        index
      );

    row?.classList.add(
      "is-selected"
    );

    if (
      row
      &&
      scrollToRow
    ) {
      row.scrollIntoView({
        block: "nearest",
        inline: "nearest"
      });
    }
  }

  refreshFaceRelatedVertexRows();

  updateSelectedFaceInfo();
}


function renderVertexTable(
  vertices: Vec3[]
): void {

  hoveredVertexRow =
    null;

  vertexCountBadge.textContent =
    String(
      vertices.length
    );

  if (
    vertices.length === 0
  ) {

    vertexTableBody.innerHTML = `
      <tr class="empty-row">
        <td colspan="4">
          No vertices found.
        </td>
      </tr>
    `;

    return;
  }

  vertexTableBody.innerHTML =
    vertices
      .map(
        (
          vertex,
          index
        ) => `
          <tr
            class="vertex-row"
            data-vertex-index="${index}"
          >
            <td class="vertex-id">
              V${index}
            </td>

            <td class="number-cell">
              ${formatCoordinate(vertex.x)}
            </td>

            <td class="number-cell">
              ${formatCoordinate(vertex.y)}
            </td>

            <td class="number-cell">
              ${formatCoordinate(vertex.z)}
            </td>
          </tr>
        `
      )
      .join("");

  const rows =
    vertexTableBody
      .querySelectorAll<HTMLTableRowElement>(
        "tr[data-vertex-index]"
      );

  rows.forEach(
    (row) => {

      const index =
        Number(
          row.dataset.vertexIndex
        );

      row.addEventListener(
        "pointerenter",
        () => {

          setVertexRowHover(
            index
          );

          viewer.setHoveredVertex(
            index
          );
        }
      );

      row.addEventListener(
        "pointerleave",
        () => {

          setVertexRowHover(
            null
          );

          viewer.setHoveredVertex(
            null
          );
        }
      );
    }
  );
}


function renderFaceTable(
  parsed: ParsedSmd
): void {

  hoveredFaceRow =
    null;

  selectedFaceRow =
    null;

  const faces =
    parsed.geometry.brep.faces;

  const faceNames =
    parsed.geometry.brep.faceNames;

  faceCountBadge.textContent =
    String(
      faces.length
    );

  selectedFaceInfo.textContent =
    "Selected face: none";

  if (
    faces.length === 0
  ) {

    faceTableBody.innerHTML = `
      <tr class="empty-row">
        <td colspan="3">
          No faces found.
        </td>
      </tr>
    `;

    return;
  }

  faceTableBody.innerHTML =
    faces
      .map(
        (
          face,
          index
        ) => {

          const name =
            faceNames[index]
            ??
            face.name
            ??
            "-";

          const vertices =
            face.indices
              .map(
                (vertexIndex) =>
                  `V${vertexIndex}`
              )
              .join(", ");

          return `
            <tr
              class="face-row"
              data-face-index="${index}"
            >
              <td class="face-id">
                F${index}
              </td>

              <td
                class="face-name"
                title="${esc(name)}"
              >
                ${esc(name)}
              </td>

              <td
                class="face-vertices"
                title="${esc(vertices)}"
              >
                ${esc(vertices)}
              </td>
            </tr>
          `;
        }
      )
      .join("");

  const rows =
    faceTableBody
      .querySelectorAll<HTMLTableRowElement>(
        "tr[data-face-index]"
      );

  rows.forEach(
    (row) => {

      const index =
        Number(
          row.dataset.faceIndex
        );

      row.addEventListener(
        "pointerenter",
        () => {

          setFaceRowHover(
            index
          );

          viewer.setHoveredFace(
            index
          );
        }
      );

      row.addEventListener(
        "pointerleave",
        () => {

          setFaceRowHover(
            null
          );

          viewer.setHoveredFace(
            null
          );
        }
      );


      /*
       * Intentional V1.0.1 behavior:
       *
       * Table selection works even if
       * Face Select mode is OFF.
       *
       * V1.0.2 fixes this later.
       */
      row.addEventListener(
        "click",
        () => {

          const nextSelection =
            selectedFaceRow ===
            index
              ? null
              : index;

          setFaceRowSelected(
            nextSelection
          );

          viewer.setSelectedFace(
            nextSelection
          );
        }
      );
    }
  );
}


function clearInspector(
  message =
    "Open an SMD file."
): void {

  hoveredVertexRow =
    null;

  hoveredFaceRow =
    null;

  selectedFaceRow =
    null;

  vertexCountBadge.textContent =
    "0";

  faceCountBadge.textContent =
    "0";

  vertexTableBody.innerHTML = `
    <tr class="empty-row">
      <td colspan="4">
        ${esc(message)}
      </td>
    </tr>
  `;

  faceTableBody.innerHTML = `
    <tr class="empty-row">
      <td colspan="3">
        ${esc(message)}
      </td>
    </tr>
  `;

  selectedFaceInfo.textContent =
    "Selected face: none";
}


function decodeBuffer(
  buffer: ArrayBuffer
): string {

  try {
    return new TextDecoder(
      "windows-1252"
    ).decode(buffer);

  } catch {

    return new TextDecoder(
      "utf-8"
    ).decode(buffer);
  }
}


function resetInteractionModes(): void {

  verticesVisible =
    false;

  verticesButton.textContent =
    "Show vertices";

  verticesButton.classList.remove(
    "active"
  );

  viewer.setVerticesVisible(
    false
  );


  faceSelectMode =
    false;

  faceSelectButton.textContent =
    "Select faces";

  faceSelectButton.classList.remove(
    "active"
  );

  viewer.setFaceSelectMode(
    false
  );


  viewer.setHoveredVertex(
    null
  );

  viewer.setHoveredFace(
    null
  );

  viewer.setSelectedFace(
    null
  );

  setVertexRowHover(
    null
  );

  setFaceRowHover(
    null
  );

  setFaceRowSelected(
    null
  );
}


function loadText(
  text: string,
  sourceName: string
): void {

  try {

    statusText.textContent =
      `Parsing ${sourceName}...`;

    const parsed =
      parseSmd10125(
        text
      );

    currentParsed =
      parsed;

    viewer.load(
      parsed
    );

    resetInteractionModes();

    updateInfo(
      parsed,
      sourceName
    );

    renderVertexTable(
      parsed
        .geometry
        .brep
        .verticesWorld
    );

    renderFaceTable(
      parsed
    );

    statusText.textContent =
      `Loaded ${sourceName}: ` +
      `${parsed.geometry.brep.verticesWorld.length} vertices, ` +
      `${parsed.geometry.brep.faces.length} faces.`;

  } catch (error) {

    currentParsed =
      null;

    viewer.clear();

    resetInteractionModes();

    const message =
      error instanceof Error
        ? error.message
        : String(error);

    objectInfo.innerHTML =
      `<dt>Error</dt>` +
      `<dd>${esc(message)}</dd>`;

    geometryInfo.innerHTML =
      "";

    clearInspector(
      "Could not inspect geometry."
    );

    statusText.textContent =
      `Could not load ${sourceName}.`;
  }
}


async function loadFile(
  file: File
): Promise<void> {

  const buffer =
    await file.arrayBuffer();

  loadText(
    decodeBuffer(buffer),
    file.name
  );
}


/*
 * 3D vertex -> table
 */
viewer.setVertexHoverHandler(
  (index) => {

    setVertexRowHover(
      index,
      index !== null
    );
  }
);


/*
 * 3D face -> face table
 */
viewer.setFaceHoverHandler(
  (index) => {

    setFaceRowHover(
      index,
      index !== null
    );
  }
);


/*
 * 3D face selection -> table
 */
viewer.setFaceSelectionHandler(
  (index) => {

    setFaceRowSelected(
      index,
      index !== null
    );
  }
);


fileInput.addEventListener(
  "change",
  () => {

    const file =
      fileInput.files?.[0];

    if (file) {
      void loadFile(
        file
      );
    }
  }
);


verticesButton.addEventListener(
  "click",
  () => {

    verticesVisible =
      !verticesVisible;

    viewer.setVerticesVisible(
      verticesVisible
    );

    verticesButton.textContent =
      verticesVisible
        ? "Hide vertices"
        : "Show vertices";

    verticesButton.classList.toggle(
      "active",
      verticesVisible
    );
  }
);


faceSelectButton.addEventListener(
  "click",
  () => {

    faceSelectMode =
      !faceSelectMode;

    viewer.setFaceSelectMode(
      faceSelectMode
    );

    faceSelectButton.textContent =
      faceSelectMode
        ? "Face SELECT : ON"
        : "Select faces";

    faceSelectButton.classList.toggle(
      "active",
      faceSelectMode
    );
  }
);


fitButton.addEventListener(
  "click",
  () =>
    viewer.fit()
);


for (
  const eventName of [
    "dragenter",
    "dragover"
  ]
) {

  viewerElement.addEventListener(
    eventName,
    (event) => {

      event.preventDefault();

      dropOverlay.classList.add(
        "visible"
      );
    }
  );
}


for (
  const eventName of [
    "dragleave",
    "drop"
  ]
) {

  viewerElement.addEventListener(
    eventName,
    (event) => {

      event.preventDefault();

      dropOverlay.classList.remove(
        "visible"
      );
    }
  );
}


viewerElement.addEventListener(
  "drop",
  (event) => {

    const file =
      event.dataTransfer
        ?.files?.[0];

    if (file) {
      void loadFile(
        file
      );
    }
  }
);