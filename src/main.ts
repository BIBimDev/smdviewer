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

            <span
              id="vertexCountBadge"
              class="inspector-badge"
            >
              0
            </span>

          </div>


          <div class="inspector-section-title">
            Vertices
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

        </section>


        <section class="panel notes">

          <h2>
            V1.0.1 development
          </h2>

          <p>
            Day 3 adds a vertex inspector
            table and synchronized hover
            between the table and visible
            3D vertex nodes.
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



function mustQuery<
  T extends Element
>(
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


const fitButton =
  mustQuery<HTMLButtonElement>(
    "#fitButton"
  );


const vertexTableBody =
  mustQuery<HTMLTableSectionElement>(
    "#vertexTableBody"
  );


const vertexCountBadge =
  mustQuery<HTMLElement>(
    "#vertexCountBadge"
  );



const viewer =
  createViewer(
    viewerElement
  );



let verticesVisible =
  false;


let hoveredVertexRow:
  number | null =
  null;



function esc(
  value: unknown
): string {

  return String(
    value ?? "-"
  )
    .replaceAll(
      "&",
      "&amp;"
    )
    .replaceAll(
      "<",
      "&lt;"
    )
    .replaceAll(
      ">",
      "&gt;"
    )
    .replaceAll(
      '"',
      "&quot;"
    );

}



function formatCoordinate(
  value: number
): string {

  /*
   * Avoid displaying:
   *
   * -0
   */

  if (
    Math.abs(value) <
    1e-9
  ) {

    value = 0;

  }


  const rounded =
    Math.round(value);


  if (
    Math.abs(
      value - rounded
    ) < 1e-6
  ) {

    return String(
      rounded
    );

  }


  return value.toFixed(3);
}



function profileSize(
  parsed: ParsedSmd
): string {

  const edges =
    parsed.geometry
      .profileEdges;


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


  return (
    `${width} × ${depth} mm`
  );
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
    <dd>
      ${esc(sourceName)}
    </dd>

    <dt>Format</dt>
    <dd>
      DICAD ASC
    </dd>

    <dt>Block</dt>
    <dd>
      #10125
    </dd>

    <dt>Name</dt>
    <dd>
      ${esc(g.name)}
    </dd>

    <dt>Object ID</dt>
    <dd class="mono">
      ${esc(g.objectId)}
    </dd>

    <dt>Teilart</dt>
    <dd>
      ${esc(g.partType)}
    </dd>

  `;


  geometryInfo.innerHTML = `

    <dt>Profile</dt>
    <dd>
      ${esc(
        profileSize(parsed)
      )}
    </dd>

    <dt>Profile edges</dt>
    <dd>
      ${g.profileEdges.length}
    </dd>

    <dt>Vertices</dt>
    <dd>
      ${b.verticesWorld.length}
    </dd>

    <dt>Faces</dt>
    <dd>
      ${b.faces.length}
    </dd>

    <dt>Planes</dt>
    <dd>
      ${b.planes.length}
    </dd>

    <dt>Face names</dt>
    <dd>
      ${b.faceNames.length}
    </dd>

    <dt>BREP transform</dt>
    <dd>
      ${b.transform
        ? "yes"
        : "no"}
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



/*
 * Find one vertex table row.
 *
 * Example:
 *
 * getVertexRow(8)
 *
 * finds:
 *
 * <tr data-vertex-index="8">
 */
function getVertexRow(
  index: number
): HTMLTableRowElement | null {

  return vertexTableBody
    .querySelector<HTMLTableRowElement>(

      `tr[data-vertex-index="${index}"]`

    );

}



/*
 * Highlight one row in the table.
 *
 * Used by:
 *
 * table -> 3D hover
 *
 * and
 *
 * 3D -> table hover
 */
function setVertexRowHover(
  index: number | null,
  scrollToRow = false
): void {

  /*
   * Remove highlight from
   * previous row.
   */

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


  /*
   * If hover came from the
   * 3D viewer, bring the row
   * into view.
   *
   * We will improve this
   * scrolling implementation
   * later in V1.0.2.
   */

  if (
    scrollToRow
  ) {

    row.scrollIntoView({

      block: "nearest",

      inline: "nearest"

    });

  }

}



/*
 * Create the Vertex Inspector
 * table from BREP world vertices.
 */
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
              ${formatCoordinate(
                vertex.x
              )}
            </td>

            <td class="number-cell">
              ${formatCoordinate(
                vertex.y
              )}
            </td>

            <td class="number-cell">
              ${formatCoordinate(
                vertex.z
              )}
            </td>

          </tr>

        `
      )
      .join("");


  /*
   * Attach table -> 3D hover.
   */

  const rows =
    vertexTableBody
      .querySelectorAll<HTMLTableRowElement>(

        "tr[data-vertex-index]"

      );


  rows.forEach(
    (row) => {

      const index =
        Number(
          row.dataset
            .vertexIndex
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



function clearVertexTable(
  message =
    "Open an SMD file."
): void {

  hoveredVertexRow =
    null;


  vertexCountBadge.textContent =
    "0";


  vertexTableBody.innerHTML = `

    <tr class="empty-row">

      <td colspan="4">
        ${esc(message)}
      </td>

    </tr>

  `;

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



function resetVertexDisplay(): void {

  verticesVisible =
    false;


  verticesButton.textContent =
    "Show vertices";


  verticesButton
    .classList
    .remove(
      "active"
    );


  viewer.setVerticesVisible(
    false
  );


  viewer.setHoveredVertex(
    null
  );


  setVertexRowHover(
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


    viewer.load(
      parsed
    );


    resetVertexDisplay();


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


    statusText.textContent =

      `Loaded ${sourceName}: ` +

      `${parsed.geometry.brep.verticesWorld.length} vertices, ` +

      `${parsed.geometry.brep.faces.length} faces.`;

  } catch (error) {

    viewer.clear();


    resetVertexDisplay();


    const message =

      error instanceof Error

        ? error.message

        : String(error);


    objectInfo.innerHTML =

      `<dt>Error</dt>` +

      `<dd>${esc(message)}</dd>`;


    geometryInfo.innerHTML =
      "";


    clearVertexTable(
      "Could not inspect vertices."
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
 * -------------------------------------------------
 * 3D -> table hover
 * -------------------------------------------------
 *
 * renderer.ts detects which
 * vertex sphere is under the mouse.
 *
 * It sends its V-number here.
 */
viewer.setVertexHoverHandler(
  (index) => {

    setVertexRowHover(

      index,

      index !== null

    );

  }
);



fileInput.addEventListener(
  "change",
  () => {

    const file =
      fileInput
        .files?.[0];


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


    verticesButton
      .classList
      .toggle(
        "active",
        verticesVisible
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


      dropOverlay
        .classList
        .add(
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


      dropOverlay
        .classList
        .remove(
          "visible"
        );

    }

  );

}



viewerElement.addEventListener(
  "drop",
  (event) => {

    const file =
      event
        .dataTransfer
        ?.files?.[0];


    if (file) {

      void loadFile(
        file
      );

    }

  }
);