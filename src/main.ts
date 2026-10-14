import "./style.css";
import { parseSmd10125 } from "./smd/parser10125";
import type { ParsedSmd } from "./smd/types";
import { boundingBoxText, createViewer } from "./viewer/renderer";

const app = document.querySelector<HTMLDivElement>("#app");

if (!app) {
  throw new Error("#app element not found");
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


        <section class="panel notes">

          <h2>V1.0.1 development</h2>

          <p>
            Day 1 adds the vertex inspection
            foundation while keeping the
            original V1 SMD parser unchanged.
          </p>

          <p>
            Other STRAKON blocks are
            intentionally not interpreted yet.
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
    document.querySelector<T>(selector);

  if (!element) {
    throw new Error(
      `Required UI element was not found: ${selector}`
    );
  }

  return element;
}


const viewerElement =
  mustQuery<HTMLElement>("#viewer");

const fileInput =
  mustQuery<HTMLInputElement>("#fileInput");

const objectInfo =
  mustQuery<HTMLElement>("#objectInfo");

const geometryInfo =
  mustQuery<HTMLElement>("#geometryInfo");

const statusText =
  mustQuery<HTMLElement>("#statusText");

const dropOverlay =
  mustQuery<HTMLElement>("#dropOverlay");

const verticesButton =
  mustQuery<HTMLButtonElement>(
    "#verticesButton"
  );

const fitButton =
  mustQuery<HTMLButtonElement>(
    "#fitButton"
  );


const viewer =
  createViewer(viewerElement);


let verticesVisible = false;


function esc(
  value: unknown
): string {

  return String(value ?? "-")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}


function profileSize(
  parsed: ParsedSmd
): string {

  const edges =
    parsed.geometry.profileEdges;

  if (edges.length === 0) {
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
    Math.max(...xs) -
    Math.min(...xs);

  const depth =
    Math.max(...ys) -
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

  verticesVisible = false;

  verticesButton.textContent =
    "Show vertices";

  verticesButton.classList.remove(
    "active"
  );

  viewer.setVerticesVisible(false);
}


function loadText(
  text: string,
  sourceName: string
): void {

  try {

    statusText.textContent =
      `Parsing ${sourceName}...`;

    const parsed =
      parseSmd10125(text);

    viewer.load(parsed);

    resetVertexDisplay();

    updateInfo(
      parsed,
      sourceName
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

    geometryInfo.innerHTML = "";

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


fileInput.addEventListener(
  "change",
  () => {

    const file =
      fileInput.files?.[0];

    if (file) {
      void loadFile(file);
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


fitButton.addEventListener(
  "click",
  () => viewer.fit()
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
      void loadFile(file);
    }

  }
);